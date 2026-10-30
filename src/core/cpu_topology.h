#pragma once

#include "types.h"

struct FlArena;
struct Thread;

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// CPU Topology Detection
//
// Linux: detects core types by reading CPU frequency from /sys/devices/system/cpu/
// macOS/Windows: returns all cores as Unknown, and affinity setting always fails as unsupported

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

typedef enum {
    CoreType_Unknown,
    CoreType_Performance, // e.g. ARM Cortex-A76, Intel P-cores
    CoreType_Efficiency,  // e.g. ARM Cortex-A55, Intel E-cores
} CoreType;

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

typedef struct {
    int cpu_id; // OS CPU ID, as used for affinity setting
    CoreType type;
    int max_freq_khz; // 0 if unknown
} CpuInfo;

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

typedef struct {
    CpuInfo* cpus;
    int count;
    int performance_count;
    int efficiency_count;
} CpuTopology;

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

typedef struct {
    int* ids;
    int count;
} CpuIdList;

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Detects available CPUs and classifies them by type. The result is allocated from arena.

CpuTopology cpu_query_topology(struct FlArena* arena);

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Decide a CPU's online state from its sysfs 'online' attribute (Linux only). A missing
// /sys/devices/system/cpu/cpuN/online file means the kernel exposes no hotplug state for that CPU,
// which counts as online. Otherwise only a value of 1 is online; pass 0 for an unparsable file.

#if PLATFORM_LINUX
bool cpu_online_from_sysfs(bool file_exists, int value);
#endif

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// CPU IDs matching a core type, allocated from arena.

CpuIdList fl_cpu_query_by_type(CoreType type, struct FlArena* arena);

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Pins a thread (nullptr for the current one) to run only on the given CPUs. Returns true only when
// the pinning was applied; false on error, on a cpu id the platform does not have, and on platforms
// with no affinity support. An empty list is a no-op that succeeds where affinity is supported.

bool fl_cpu_set_thread_affinity(struct Thread* thread, CpuIdList cpus);
