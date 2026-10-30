# VFS handle lifetime: snapshot reads, job-pinned frees

## Status

superseded by ADR-0074

`vfs_lookup_handle` used to return a raw `VfsHandleData*` whose protecting lock released at
function return, so every caller raced a concurrent `vfs_close` freeing the same struct. We
decided the lifetime contract has exactly two legs:

1. **Outside a job**: no raw `VfsHandleData*` escapes `handle_lock`. Callers get a value-copy
   (snapshot) of the fields they need, taken under the lock; all field mutations happen under
   the lock via helpers. Result *payloads* (`FlVfsData`, `FlVfsFileList`) remain valid from
   operation-ready until `vfs_close` — close is the single invalidation point, and reading a
   result concurrently with closing the same handle is caller error (debug-asserted, not
   defended).
2. **Inside a job**: the worker may hold the raw pointer for the op's whole duration, because
   no free path runs while the job does. The mount-close sweep and watcher-driven removal wait
   for the job. A plain `vfs_close` on the main thread does not: it marks the handle closed -
   absent to every caller, id not recycled, job left to finish - and a reclaim sweep
   (`vfs_update`, `vfs_wait_all`, the start of the next `vfs_close`) frees it once its jobs are
   observed finished. The one close that still waits is of a `vfs_read` / `vfs_write` op, whose
   job runs against memory the caller lent it and takes the close as leave to free.

## Considered options

- **Refcounted handles** (lookup pins, caller unpins): rejected — the caller population is ~15
  internal sites; a pin/unpin protocol is machinery every future call site can get wrong, for
  a public-concurrent-API problem the module doesn't have.
- **Owner-thread-only contract** (only the issuing thread may close, after wait): rejected —
  it re-labels the race as user error while flowi's own job workers already violate it.
