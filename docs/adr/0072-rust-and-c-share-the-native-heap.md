# Rust and C share Flowi's native heap

Status: accepted

## Decision

`flowi_core_sys` installs a Rust `GlobalAlloc` backed by the existing exported
`fl_heap_*` C functions. The `global-allocator` feature is opt-in in
`flowi_core_sys`, `flowi_core`, `flowi_sys` and `flowi`. Enabling it covers Rust collections in the application,
its dependencies, and Flowi-consuming Rust plugins without compiling another
copy of mimalloc. Static hosts export the heap API; shared-library consumers
resolve it from the same Flowi library. Allocation works before `fl_init` and
after `fl_destroy` because the heap has no application lifecycle dependency.

Ordinary allocations use native alloc/calloc/realloc/free. Over-aligned
allocations use the aligned alloc/free pair; resizing allocates, copies, and
frees because the public heap API has no aligned realloc operation. Failure
leaves the original block live. Rust ownership and FFI destruction contracts
remain unchanged: a shared heap does not make arbitrary C buffers into Rust
collections, and arenas/pools/virtual memory retain their own lifetimes.

The native policy applies to both languages: mimalloc normally, the system heap
under ASan/TSan and on Windows. C malloc replacement defaults on in standalone
supported Unix builds; macOS/Windows and sanitizers keep interposition disabled.
That replacement does not promise to change private allocators in third-party
libraries or operating-system memory APIs. No blanket speedup is claimed without
workload measurements.

Applications providing a different Rust allocator leave `global-allocator`
disabled throughout their dependency graph. Consumers that want the standard
Flowi heap enable `features = ["global-allocator"]`. Windowing and allocator
selection are independent.

The optional `allocation-probe` feature adds thread-local counting to this same
allocator for allocation-free hot-path tests. A downstream host enables it as a dev
dependency, replacing its separate system-backed test allocator. Production
builds contain no counter or callback. The probe observes Rust allocations on
the measuring thread, not direct C heap calls.
