# VFS: one lock per mount, single-handle tree nodes, refcounted operations

## Status

accepted. Supersedes ADR-0068 (handle lifetime). Decision 1 was amended after stage 2 shipped: the first wording
removed the tree and with it paths that cross an archive, which the VFS exists to provide.

## Context

The VFS stays fully asynchronous: nothing on the main thread waits, `vfs_close` never blocks, and every
operation runs on the job system. The concurrency machinery wrapped around that contract has grown with each
race found, and now outweighs the work it protects:

- **Three locks plus an ordering rule.** `mount_lock`, `handle_lock` and a per-mount `tree_lock`, with
  "handle_lock before tree_lock, never reversed, never across I/O". `vfs_mount_close` takes them in a
  different order again and has to release one before taking the next.
- **A tree edited from three places under its own lock.** Listings, archive loads and the file watcher all edit
  `VfsTreeNode`s. Every reader copies fields out under `tree_lock` and must not keep a node pointer across the unlock.
  Two workers loading the same archive node can close the plugin handles the other is still using. The tree is
  right; the lock split around it is the problem.
- **Per-worker plugin handle clones.** `MAX_JOB_THREADS = 2` clones of every handle, indexed by worker, and
  `vfs_init` refuses a job system with more workers.
- **Non-refcounted handles in a global map.** The lifetime contract (ADR-0068) needed a `closed` flag, a
  reclaim sweep run from three places, `pending_dispatches` counters for the window between creating a job and
  recording it, a `dispatch_pending` snapshot field, two-pass walks of the map, and a documented exception for
  `vfs_read`/`vfs_write`. About two thirds of `tests/unit_tests_vfs.c` exists to pin these invariants.
- **Driver I/O under `mount_lock`.** `vfs_mount_with_options` runs the whole resolve and archive load while
  holding the global mount lock when called from a worker.
- **Features nothing uses.** See "Removed" below.

Callers use a small part of this. Every `Vfs::mount` in the embedding host mounts one directory, one archive file
or one HTTP base URL, then lists and reads inside it, and none writes, watches or filters through the VFS. The
first draft also concluded that nothing reads through an archive inside a directory mount and deleted the tree. That
was a survey of today's call sites, not of the contract: a mount must resolve any valid path, however many archives it
passes through, and the tree is how it does so.

## Decision

### 1. A mount keeps a tree of nodes, with a single handle and one lock

A mount holds the source path, the mount options and a tree of nodes. The root node carries the driver and the one
handle the source resolved to; the mount job resolves it once, including archives nested inside the mount path
(`a.zip/inner.lha`), and publishes one atomic status (pending / ready / error).

A path below the mount may pass through any number of archives (`dir/a.zip/b.zip/sub/c.lha/file`). Each segment of
a path met is a node. A node that is a file an archive driver can open becomes an archive node: it carries that
driver and a handle, opened in place when the archive is a file on disk and otherwise from a copy in memory. Resolving
a path walks it from the root, switches to the driver of every archive node it passes, and hands the rest of the path
to the nearest node that carries a driver. That is also how a path inside an archive finds its driver without the
caller mounting each layer: the walk finds the archive, loads it once and keeps it. A segment the driver reports as no
file is remembered as a directory, so it is asked only once. A file that is no archive, or an archive that does not open
(corrupt), is remembered the same way: it is not entered, the path resolves below it as a plain path and the driver
reports the file missing. A file that is replaced by an archive later is not noticed until the mount is reopened. A mount can also be made directly on a nested path (`dir/a.zip/b.zip`); its root is
that archive.

What stage 2 removed stays removed. Nodes are never freed once made and change only when one gains its archive or is
found to be a directory; there is no node pool, no sorted child list and no mutation by listings or by a file watcher. A listing asks the driver
for the directory, sorts directories first then natural order, and builds the result in its own arena, with no
per-entry probe. A node holds **one** handle: there are no per-worker clones, because every driver call on a mount is
serialized.

The one per-mount lock (decision 2) guards the walk, the opening of an archive, the edit of a node and the
driver call that follows. A node therefore cannot change while another worker has copied its handle out, which
was the race that the separate `tree_lock` and the clone dance produced. No global lock is held across any of it.

### 2. Driver calls on a mount are serialized by one mutex

One mutex per mount guards calls into the driver for that mount. Per-worker handle clones, `MAX_JOB_THREADS`
and the worker-index plumbing are removed, as is the `clone` slot of `FlVfsPlugin`. The cost is that two
parallel reads from the same archive or HTTP mount run one after the other. Archive decoding is sequential per
handle anyway, and a slow driver blocks only its own mount.

### 3. Operations are refcounted objects

Each operation (`Op`) owns its path, result arena, status and job handle, and carries a reference count. The
slot table that turns a `u32` into an `Op`, and the operation's own job, each hold one reference. A mount
holds a reference per live operation, so closing a mount marks it closing, refuses new operations, and frees
it when the last reference drops. No scan of a global map.

The public contract is unchanged:

- IDs stay `u32`; `0` is invalid. A stale or already-closed ID is a no-op, never undefined behavior.
- `vfs_close` returns immediately. If the job is still running, the op is released when the job finishes.
- A read-all's release hook runs exactly once, after the job and the callback, outside any lock.
- `vfs_mount_read_all_prepare` followed by `vfs_dispatch` still lets the caller record the ID before the job
  can run. The op exists and is referenced before it is scheduled, so no pending-dispatch counter is needed.
- A caller-chosen ID can still be reused after close (`reuse_handle`, used by the image system).
- Results stay readable from ready until `vfs_close`.

The refcount is internal. Callers never pin or unpin; they still have one ticket and one close.

How the contract is met:

- One lock, `handle_lock`, guards the registry: the mount list, each mount's list of live ops and its closing flag, the
  slot table and the id counter. It is a leaf, held for edits and lookups only. A lookup takes a reference on the op
  before it unlocks, so no raw op pointer outlives the lock. An op is destroyed by whichever thread drops the last
  reference, outside every lock: driver file close, result free, the release hook, then its reference on the mount.
- `vfs_close` unlists the id and drops the slot's reference, from any thread, and never waits. A job that is still
  running holds its own reference, so the op, and a read-all's hook, end with the job on its worker. The one close that
  waits is a `vfs_read` handle, whose job writes into memory the caller lent: it waits on the main thread and is refused
  on a worker, which cannot wait.
- A closed id reads as absent at once but stays reserved until its op is destroyed: a `retiring` set, filled when the
  slot is unlisted and emptied by the op's destruction, makes `reuse_handle` of an id whose old op is still queued or
  running refuse with a warning, as before, so a reload under the same id cannot race the closed op's completion. Reuse
  succeeds once the old op is gone.
- A chained read owns a reference on its file from creation until it has run. Closing the file's id in between leaves the
  driver handle open until the read ends, which replaces the old window counter on the file handle.
- A mount lists every op that is not yet destroyed. `vfs_mount_close` marks the mount closing, so `vfs_op_create` refuses
  new ops, unlists the ids of the ops it has and drops their slot references. On the main thread it then waits for the
  jobs of what is left, until the last op has been destroyed, which is how the old guarantee that every release hook has
  run when the close returns still holds. From a worker it returns without waiting. The mount closes its root handle and
  frees itself on the last reference, so an op's file handle is always closed before the root it was opened from. A closed
  mount stays on the mount list until it is freed, so `vfs_wait_all` and `vfs_destroy` still cover the ops it has running.
- Known remaining wait: `vfs_mount_close` on the main thread still waits (`fl_jobs_wait`, then a short poll) until the
  mount's last op has ended. The baseline did the same, and the Rust `Mount` drop relies on it: every read's release
  hook has run, and the slots it frees are back, when the close returns. Making it non-blocking is possible, since
  correctness only needs the reference counts, but it would let those hooks run after the Rust owner is gone, so that
  change is deferred until a caller needs it.
- `vfs_is_ready` reports an explicit `done` flag the job sets after publishing its result, not the state of a job handle.
  An op that has been prepared but not dispatched is therefore not ready, where the old check treated "no job yet" as
  finished.

`vfs_wait_all` is kept. `fl_host` calls it after mounting the working directory, because the first frame may issue a
load against that mount and nothing public returns the mount's own job to wait on; `vfs_destroy` and the tests use it
too. It collects the jobs of every live op under `handle_lock` and waits for them outside it. The ops it waits for
include ones whose id was already closed but whose job has not finished, which is what a shutdown needs.

### Removed

No caller in flowi or the embedding host uses these (verified by search; tests excepted):

- Writing: `vfs_write`, `vfs_write_no_copy`, `FlVfsOpenFlags_Write`/`Create`/`Truncate`/`Append`, the driver
  `write` slot and the Write capability.
- File watching: `vfs_mount_enable_watching`, `FlVfsMountOptions.enable_file_watching`, the driver
  `enable_watching` slot and capability, mount `version`, `vfs_is_listing_current`, `FlVfsFileList.mount_version`,
  and the watcher half of `vfs_update`.
- Filtered listing: `vfs_get_listing_filtered`, `vfs_apply_fuzzy_filter`, `filter_needle`. Callers filter
  the returned list themselves, as the file browser does.
- Cancellation flag: `vfs_cancel` and `cancel_requested`. Cancellation is drop (ADR-0070), which closes the ticket
  without blocking. Only tests call `vfs_cancel`.
- Dead helpers: `vfs_dump_tree`, `vfs_schedule_job`, the internal `vfs_get_size` and unused mount fields.

`vfs_update` has no job left once watching and the reclaim sweep are gone; its call sites are removed.

Kept because something uses it: `vfs_set_priority` and the prepare/dispatch pair and ID reuse (image
system), `vfs_get_status`, `vfs_mount_get_job_handle`, `vfs_mount_open` plus `vfs_read` (the host's
prefix and chunked reads), and depth listings.

## Consequences

- `handle_lock`, `tree_lock` and `mount_lock` collapse into one lock around the slot table plus one mutex per
  mount. There is no lock ordering rule between them.
- The reclaim sweep, `closed`, `closed_count`, `pending_dispatches`, the tree-walk worker checks and the
  clone-failure paths are deleted rather than fixed.
- Listing no longer opens every non-directory entry with every driver to compute `is_archive`. The flag is kept,
  because the embedding host's browse listing reads it to pick the entries it hashes as archives, and it is now decided
  by name, at every level (inside archives too): a file is an archive when a registered archive-format driver (capability
  `FlVfsDriverCapabilities_Archive`) claims its name through `supports_path`, which does no I/O. An archive that
  carries an archive extension but does not open (corrupt, or truncated) is therefore reported as an archive where
  the probe reported it as a plain file; mounting it still fails and says so. Resolving a path asks the driver
  once per new path segment whether it is a file (`get_size`), which for a network driver is a request, and the answer
  is kept in the tree, for an archive that opened and for one that did not.
- A mount holds every archive it has entered until it closes. An archive opened from memory keeps its copy for that
  long, and a file-backed one keeps its file open. The tree does not shrink and has no eviction; a mount that
  walks through many large archives is closed and reopened by its owner.
- Opening an archive reads it while holding the mount's driver lock, so the first operation to pass through
  an archive delays the others on that mount for as long as the read takes.
- `FlVfsPlugin` loses the `clone`, `write` and `enable_watching` slots, `FlVfsDriverCapabilities` loses `Clone`,
  and `FlVfsFileList` loses `mount_version`. That is an ABI break: `FL_VFS_PLUGIN_API_VERSION` is bumped,
  the embedding host's httpfs driver and its use of `mount_version` change in the same sync, and the generated bindings
  and the Rust fake are regenerated.
- Reads through one mount from several workers serialize at the driver. The mutex covers open, read, get_size, list
  and the tree; closing a file handle does not take it, so `vfs_close` on the main thread never waits behind a worker's driver
  call. A driver therefore has to allow `close` of one file handle while another handle of the same mount
  is in use. It no longer has to allow a file handle's `close` after the root's: every op holds its mount, so the root
  closes only after the last file handle opened from it.
- The tests that pin the old invariants (deferred close windows, watcher removal, per-worker clone failure) go away with the code. The ones that pin the contract stay and are the acceptance
  criteria: close from a worker, close racing a chained read, a read of a closed handle, mount close with work in
  flight, release hook exactly once, churn across threads, and the Rust `vfs_drop_returns_while_gated`.

## Considered options

- **Drop the tree; mount each archive by its own path.** The first wording of decision 1. Rejected: the caller has to
  know where the archives are, and the VFS exists so that it does not. A smaller table of open archives was also
  built and worked; the tree was kept because walking up to the node that carries the driver is the design, and
  its resolution logic is subtle enough that rewriting it was a risk with nothing to gain.
- **No mounts, path-only calls.** Rejected: every call would re-resolve, and re-parse an archive on each read.
  Callers list and read repeatedly inside one archive (an archive scan) or one HTTP base.
- **Per-operation driver clones instead of a mutex.** Rejected for now: it needs a thread-safe clone from every
  driver, costs an archive reopen per operation, and nothing shows lock contention to justify it. A driver that
  proves slow under the mutex can opt into clones later.
- **Keep non-refcounted handles (ADR-0068).** Rejected: its own consequences list is the problem statement above.
  ADR-0068 rejected refcounting because it would push pin and unpin onto callers; the refcount here is internal.
