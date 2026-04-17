// object.c — Content-addressable object store
//
// Every piece of data (file contents, directory listings, commits) is stored
// as an "object" named by its SHA-256 hash. Objects are stored under
// .pes/objects/XX/YYYYYY... where XX is the first two hex characters of the
// hash (directory sharding).
//
// PROVIDED functions: compute_hash, object_path, object_exists, hash_to_hex, hex_to_hash
// TODO functions:     object_write, object_read

#include "pes.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>
#include <errno.h>

static inline uint32_t rotr(uint32_t x, int n) { return (x >> n) | (x << (32 - n)); }
static inline uint32_t ch(uint32_t x, uint32_t y, uint32_t z) { return (x & y) ^ (~x & z); }
static inline uint32_t maj(uint32_t x, uint32_t y, uint32_t z) { return (x & y) ^ (x & z) ^ (y & z); }
static inline uint32_t sigma0(uint32_t x) { return rotr(x,2) ^ rotr(x,13) ^ rotr(x,22); }
static inline uint32_t sigma1(uint32_t x) { return rotr(x,6) ^ rotr(x,11) ^ rotr(x,25); }
static inline uint32_t gamma0(uint32_t x) { return rotr(x,7) ^ rotr(x,18) ^ (x>>3); }
static inline uint32_t gamma1(uint32_t x) { return rotr(x,17) ^ rotr(x,19) ^ (x>>10); }

static const uint32_t K[64] = {
    0x428a2f98,0x71374491,0xb5c0fbcf,0xe9b5dba5,0x3956c25b,0x59f111f1,0x923f82a4,0xab1c5ed5,
    0xd807aa98,0x12835b01,0x243185be,0x550c7dc3,0x72be5d74,0x80deb1fe,0x9bdc06a7,0xc19bf174,
    0xe49b69c1,0xefbe4786,0x0fc19dc6,0x240ca1cc,0x2de92c6f,0x4a7484aa,0x5cb0a9dc,0x76f988da,
    0x983e5152,0xa831c66d,0xb00327c8,0xbf597fc7,0xc6e00bf3,0xd5a79147,0x06ca6351,0x14292967,
    0x27b70a85,0x2e1b2138,0x4d2c6df,0x53380d13,0x650a7354,0x766a0abb,0x81c2c92e,0x92722c85,
    0xa2bfe8a1,0xa81a664b,0xc24b8b70,0xc76c51a3,0xd192e819,0xd6990624,0xf40e3585,0x106aa070,
    0x19a4c116,0x1e376c08,0x2748774c,0x34b0bcb5,0x391c0cb3,0x4ed8aa4a,0x5b9cca4f,0x682e6ff3,
    0x748f82ee,0x78a5636f,0x84c87814,0x8cc70208,0x90befffa,0xa4506ceb,0xbef9a3f7,0xc67178f2
};

static void sha256_compute(const uint8_t *data, size_t len, uint8_t *hash_out) {
    uint32_t h[8] = {0x6a09e667,0xbb67ae85,0x3c6ef372,0xa54ff53a,0x510e527f,0x9b05688c,0x1f83d9ab,0x5be0cd19};
    size_t blocks = (len + 9 + 63) / 64;
    uint8_t msg[128];
    for (size_t bi = 0; bi < blocks; bi++) {
        memset(msg, 0, 64);
        size_t off = bi * 64;
        size_t cl = len - off;
        if (cl > 64) cl = 64;
        memcpy(msg, data + off, cl < 64 ? cl : 64);
        if (bi == blocks - 1) {
            msg[cl] = 0x80;
            uint64_t bitlen = len * 8;
            memcpy(msg + 56, &bitlen, 8);
        }
        uint32_t w[64];
        for (int i = 0; i < 16; i++) {
            w[i] = (msg[i*4] << 24) | (msg[i*4+1] << 16) | (msg[i*4+2] << 8) | msg[i*4+3];
        }
        for (int i = 16; i < 64; i++) {
            w[i] = gamma1(w[i-2]) + w[i-7] + gamma0(w[i-15]) + w[i-16];
        }
        uint32_t a=h[0],b=h[1],c=h[2],d=h[3],e=h[4],f=h[5],g=h[6],hh=h[7];
        for(int i=0;i<64;i++){uint32_t t1=hh+sigma1(e)+ch(e,f,g)+K[i]+w[i];uint32_t t2=sigma0(a)+maj(a,b,c);hh=g;g=f;f=e;e=d+t1;d=c;c=b;b=a;a=t1+t2;}
        h[0]+=a;h[1]+=b;h[2]+=c;h[3]+=d;h[4]+=e;h[5]+=f;h[6]+=g;h[7]+=hh;
    }
    for(int i=0;i<8;i++){hash_out[i*4]=h[i]>>24;hash_out[i*4+1]=h[i]>>16;hash_out[i*4+2]=h[i]>>8;hash_out[i*4+3]=h[i];}
}

// ─── PROVIDED ────────────────────────────────────────────────────────────────

void hash_to_hex(const ObjectID *id, char *hex_out) {
    for (int i = 0; i < HASH_SIZE; i++) {
        sprintf(hex_out + i * 2, "%02x", id->hash[i]);
    }
    hex_out[HASH_HEX_SIZE] = '\0';
}

int hex_to_hash(const char *hex, ObjectID *id_out) {
    if (strlen(hex) < HASH_HEX_SIZE) return -1;
    for (int i = 0; i < HASH_SIZE; i++) {
        unsigned int byte;
        if (sscanf(hex + i * 2, "%2x", &byte) != 1) return -1;
        id_out->hash[i] = (uint8_t)byte;
    }
    return 0;
}

void compute_hash(const void *data, size_t len, ObjectID *id_out) {
    sha256_compute((const uint8_t *)data, len, id_out->hash);
}

// Get the filesystem path where an object should be stored.
// Format: .pes/objects/XX/YYYYYYYY...
// The first 2 hex chars form the shard directory; the rest is the filename.
void object_path(const ObjectID *id, char *path_out, size_t path_size) {
    char hex[HASH_HEX_SIZE + 1];
    hash_to_hex(id, hex);
    snprintf(path_out, path_size, "%s/%.2s/%s", OBJECTS_DIR, hex, hex + 2);
}

int object_exists(const ObjectID *id) {
    char path[512];
    object_path(id, path, sizeof(path));
    return access(path, F_OK) == 0;
}

// ─── TODO: Implement these ──────────────────────────────────────────────────

int object_write(ObjectType type, const void *data, size_t len, ObjectID *id_out) {
    const char *type_str;
    if (type == OBJ_BLOB) type_str = "blob";
    else if (type == OBJ_TREE) type_str = "tree";
    else if (type == OBJ_COMMIT) type_str = "commit";
    else return -1;

    char header[128];
    int header_len = snprintf(header, sizeof(header), "%s %zu", type_str, len);
    size_t header_size = header_len + 1;
    size_t total_len = header_size + len;

    char *full_obj = malloc(total_len);
    if (!full_obj) return -1;

    memcpy(full_obj, header, header_size);
    memcpy(full_obj + header_size, data, len);

    compute_hash(full_obj, total_len, id_out);

    char obj_path[512];
    object_path(id_out, obj_path, sizeof(obj_path));

    if (object_exists(id_out)) {
        free(full_obj);
        return 0;
    }

    char shard_dir[256];
    strncpy(shard_dir, obj_path, sizeof(shard_dir) - 1);
    shard_dir[sizeof(shard_dir) - 1] = '\0';
    char *p = strrchr(shard_dir, '/');
    if (p) *p = '\0';

    mkdir(".pes", 0755);
    mkdir(".pes/objects", 0755);
    mkdir(shard_dir, 0755);

    FILE *f = fopen(obj_path, "wb");
    if (!f) {
        free(full_obj);
        return -1;
    }
    fwrite(full_obj, 1, total_len, f);
    fclose(f);
    free(full_obj);

    return 0;
}

int object_read(const ObjectID *id, ObjectType *type_out, void **data_out, size_t *len_out) {
    char obj_path[512];
    object_path(id, obj_path, sizeof(obj_path));

    int fd = open(obj_path, O_RDONLY);
    if (fd < 0) {
        return -1;
    }

    struct stat st;
    if (fstat(fd, &st) < 0) {
        close(fd);
        return -1;
    }
    size_t file_size = st.st_size;

    char *file_data = malloc(file_size);
    if (!file_data) {
        close(fd);
        return -1;
    }

    ssize_t r = read(fd, file_data, file_size);
    close(fd);
    if (r != file_size) {
        free(file_data);
        return -1;
    }

    char *null_pos = memchr(file_data, '\0', file_size);
    if (!null_pos) {
        free(file_data);
        return -1;
    }

    size_t header_len = null_pos - file_data;
    char header[256];
    if (header_len >= sizeof(header)) { free(file_data); return -1; }
    strncpy(header, file_data, header_len);
    header[header_len] = '\0';

    char type_str[16] = {0};
    size_t data_size = 0;
    sscanf(header, "%s %zu", type_str, &data_size);

    ObjectType detected_type;
    if (strcmp(type_str, "blob") == 0) detected_type = OBJ_BLOB;
    else if (strcmp(type_str, "tree") == 0) detected_type = OBJ_TREE;
    else if (strcmp(type_str, "commit") == 0) detected_type = OBJ_COMMIT;
    else { free(file_data); return -1; }

    char *data_start = null_pos + 1;
    size_t actual_data_size = file_size - (data_start - file_data);

    ObjectID computed_hash;
    compute_hash(file_data, file_size, &computed_hash);

    if (memcmp(computed_hash.hash, id->hash, HASH_SIZE) != 0) {
        free(file_data);
        return -1;
    }

    *type_out = detected_type;
    *data_out = malloc(data_size);
    if (!*data_out) { free(file_data); return -1; }
    memcpy(*data_out, data_start, data_size);
    *len_out = data_size;

    free(file_data);
    return 0;
}