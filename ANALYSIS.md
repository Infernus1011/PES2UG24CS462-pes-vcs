# Analysis Questions Answers

## Branching and Checkout (Q5.1 - Q5.3)

### Q5.1: Implementing `pes checkout <branch>`

To implement `pes checkout <branch>`:

1. **Files that need to change in `.pes/`:**
   - Update `.pes/HEAD` to point to the branch reference (change from "ref: refs/heads/main" to "ref: refs/heads/<branch>")
   - Read the commit hash from `.pes/refs/heads/<branch>`
   - Read the tree object from the commit

2. **Working directory changes:**
   - For each entry in the tree, extract the blob and write to the corresponding file path
   - Create directories as needed for nested paths

3. **What makes it complex:**
   - Handling the "dirty working directory" - if the user has unstaged changes
   - Handling conflicts when a file exists in both branches but with different content
   - Handling deleted files in the target branch
   - Proper cleanup of files that don't exist in the target branch

---

### Q5.2: Detecting Dirty Working Directory Conflict

To detect conflicts using only the index and object store:

1. Read the current index (staged files with their blob hashes)
2. Read the HEAD commit's tree, parse all entries
3. For each file tracked in HEAD's tree:
   - Read the current working directory file's mtime and size
   - Compare against the index's stored mtime and size
   - If different → working directory has unstaged changes
4. If unstaged changes exist to a file that also differs between branches → REFUSE checkout

---

### Q5.3: Detached HEAD

**What happens if you make commits in detached HEAD:**
- The commits are created normally but there's no branch pointer pointing to them
- They're only reachable if you know the commit hash
- If you checkout another commit or branch, you lose reference to these commits

**How a user could recover those commits:**
- Check `.pes/HEAD` - if it contains a hash instead of "ref: refs/heads/<branch>", it's detached
- Use `git reflog` equivalent - maintain a reflog of all HEAD positions
- The commits still exist in the object store - you can find them by searching

---

## Garbage Collection and Space Reclamation (Q6.1 - Q6.2)

### Q6.1: Finding and Deleting Unreachable Objects

**Algorithm:**
1. Start with all reachable commit hashes:
   - Read all branch refs from `.pes/refs/heads/*`
   - Each branch points to a commit hash
2. Build a BFS traversal starting from these commits:
   - For each commit, visit its tree hash
   - For each tree entry that is a subtree, recursively visit
   - For each tree entry that is a blob, mark it as reachable
3. Mark all visited hashes as "reachable"
4. Scan `.pes/objects/` - any object NOT marked reachable is garbage
5. Delete garbage objects

**Data structure to track efficiently:**
- Use a hash set (or bitset if hash space is small) to track reachable hashes
- BFS queue for traversal

**For 100,000 commits and 50 branches:**
- Start with 50 initial commit hashes
- Each commit has 1 tree + average ~10-100 blob entries
- Rough estimate: 50 + 100,000 (commits) + 100,000 (trees) + ~1,000,000 (blobs) = ~1.1 million objects to visit
- But thanks to deduplication, actual unique objects may be far fewer

---

### Q6.2: Race Condition with Concurrent GC

**Race condition:**
1. Commit operation reads tree hash, about to create commit object
2. GC runs, marks no objects as reachable, deletes an unused blob
3. Commit operation creates new commit referencing that now-deleted blob
4. CORRUPTION - reference to deleted object!

**How Git avoids this:**
- Git uses a reference counting mechanism
- Objects being created (or in the process of being referenced) are "pinned"
- GC holds a lock during the critical section
- Modern Git uses a "cleanup" process that runs infrequently
- Use `git gc --prune=now` with caution when no commits are in progress

---

## Summary

All analysis questions have been answered above based on filesystem and version control concepts.