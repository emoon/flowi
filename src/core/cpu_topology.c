#include "cpu_topology.h"
#include "arena.h"
#include "core.h"
#include "log.h"
#include "os/os.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#if PLATFORM_LINUX || PLATFORM_MACOS
#include <unistd.h>
#endif

#if PLATFORM_WINDOWS
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#endif

static LogChannelId CPU_TOPOLOGY_ID = LOG_CHANNEL_INVALID;

static void ensure_log_channel(void) {
    if (CPU_TOPOLOGY_ID == LOG_CHANNEL_INVALID) {
        CPU_TOPOLOGY_ID = fl_log_register_channel(FL_S("CPU_TOPOLOGY"));
    }
}

#if PLATFORM_LINUX

bool cpu_online_from_sysfs(bool file_exists, int value) {
    if (!file_exists) {
        return true;
    }

    return value == 1;
}

static bool is_cpu_online(int cpu_id) {
    // cpu0 is always online and may not have an 'online' file
    if (cpu_id == 0) {
        return true;
    }

    char path[256];
    snprintf(path, sizeof(path), "/sys/devices/system/cpu/cpu%d/online", cpu_id);

    FILE* f = fopen(path, "r");
    if (!f) {
        return cpu_online_from_sysfs(false, 0);
    }

    int online = 0;
    if (fscanf(f, "%d", &online) != 1) {
        online = 0;
    }

    fclose(f);
    return cpu_online_from_sysfs(true, online);
}

static int read_cpu_max_freq(int cpu_id) {
    char path[256];

    // scaling_max_freq is the governor limit, which reflects the actual operating frequency;
    // cpuinfo_max_freq (hardware max) is identical for all cores of the same type.
    snprintf(path, sizeof(path), "/sys/devices/system/cpu/cpu%d/cpufreq/scaling_max_freq", cpu_id);

    FILE* f = fopen(path, "r");
    if (!f) {
        return 0; // CPU doesn't have frequency info (might be offline or no cpufreq)
    }

    int freq_khz = 0;
    if (fscanf(f, "%d", &freq_khz) != 1) {
        freq_khz = 0;
    }

    fclose(f);
    return freq_khz;
}

static void count_core(CpuTopology* topo, CpuInfo* cpu, CoreType type) {
    cpu->type = type;

    if (type == CoreType_Performance) {
        topo->performance_count++;
    } else {
        topo->efficiency_count++;
    }
}

static int max_frequency(const int* frequencies, int count) {
    int max_freq = frequencies[0];

    for_count(i, count) {
        if (frequencies[i] > max_freq) {
            max_freq = frequencies[i];
        }
    }

    return max_freq;
}

// Cores within 10% of the max frequency are Performance, below that Efficiency. Cores without
// frequency info stay Unknown.
static void classify_by_threshold(CpuTopology* topo, int threshold) {
    for_count(i, topo->count) {
        CpuInfo* cpu = &topo->cpus[i];

        if (cpu->max_freq_khz <= 0) {
            continue;
        }

        count_core(topo, cpu, cpu->max_freq_khz < threshold ? CoreType_Efficiency : CoreType_Performance);

        logc_debug(CPU_TOPOLOGY_ID, "CPU %d: %d KHz, type=%s", cpu->cpu_id, cpu->max_freq_khz,
                   cpu->type == CoreType_Performance ? "Performance" : "Efficiency");
    }
}

static void classify_single_frequency(CpuTopology* topo) {
    for_count(i, topo->count) {
        if (topo->cpus[i].max_freq_khz > 0) {
            count_core(topo, &topo->cpus[i], CoreType_Performance);
        }
    }
}

// valid_freq_count == 0: no cpufreq at all, all cores stay CoreType_Unknown
static void classify_cores(CpuTopology* topo, const int* frequencies, int valid_freq_count) {
    if (valid_freq_count == 1) {
        classify_single_frequency(topo);
        return;
    }

    if (valid_freq_count > 1) {
        int max_freq = max_frequency(frequencies, valid_freq_count);
        int threshold = max_freq - (max_freq / 10);
        logc_debug(CPU_TOPOLOGY_ID, "Max CPU frequency: %d KHz, threshold: %d KHz", max_freq, threshold);
        classify_by_threshold(topo, threshold);
    }
}

static int collect_online_cpus(CpuInfo* cpus, int* frequencies, int max_cpu_id, int* valid_freq_count) {
    int online_count = 0;

    for_count(i, max_cpu_id) {
        if (!is_cpu_online(i)) {
            continue;
        }

        int freq = read_cpu_max_freq(i);

        cpus[online_count].cpu_id = i;
        cpus[online_count].max_freq_khz = freq;
        cpus[online_count].type = CoreType_Unknown;
        online_count++;

        if (freq > 0) {
            frequencies[(*valid_freq_count)++] = freq;
        }
    }

    return online_count;
}

CpuTopology cpu_query_topology(FlArena* arena) {
    ensure_log_channel();

    CpuTopology result = { 0 };

    // Use _SC_NPROCESSORS_CONF (configured) not _SC_NPROCESSORS_ONLN (online) to get the full
    // CPU ID range: offlined cores leave holes in the ID range, so the online count truncates it.
    int max_cpu_id = (int)sysconf(_SC_NPROCESSORS_CONF);
    if (max_cpu_id <= 0) {
        logc_error(CPU_TOPOLOGY_ID, "Failed to get CPU count");
        return result;
    }

    arena_scratch_auto(temp);
    CpuInfo* temp_cpus = arena_alloc_array(temp.arena, CpuInfo, max_cpu_id);
    int* frequencies = arena_alloc_array(temp.arena, int, max_cpu_id);
    int valid_freq_count = 0;
    int online_count = collect_online_cpus(temp_cpus, frequencies, max_cpu_id, &valid_freq_count);

    if (online_count == 0) {
        logc_warning(CPU_TOPOLOGY_ID, "No online CPUs found");
        return result;
    }

    result.cpus = arena_alloc_array(arena, CpuInfo, online_count);
    result.count = online_count;
    memcpy(result.cpus, temp_cpus, (size_t)online_count * sizeof(CpuInfo));

    classify_cores(&result, frequencies, valid_freq_count);

    logc_info(CPU_TOPOLOGY_ID, "CPU topology: %d total (%d performance, %d efficiency, %d unknown)", result.count,
              result.performance_count, result.efficiency_count,
              result.count - result.performance_count - result.efficiency_count);

    return result;
}

#else // PLATFORM_WINDOWS || PLATFORM_MACOS

// Windows and macOS report every core as Unknown.

CpuTopology cpu_query_topology(FlArena* arena) {
    ensure_log_channel();

    CpuTopology result = { 0 };

#if PLATFORM_WINDOWS
    SYSTEM_INFO sys_info;
    GetSystemInfo(&sys_info);
    int cpu_count = (int)sys_info.dwNumberOfProcessors;
#else // macOS
    int cpu_count = (int)sysconf(_SC_NPROCESSORS_ONLN);
#endif

    if (cpu_count <= 0) {
        logc_error(CPU_TOPOLOGY_ID, "Failed to get CPU count");
        return result;
    }

    result.cpus = arena_alloc_array(arena, CpuInfo, cpu_count);
    result.count = cpu_count;

    for_count(i, cpu_count) {
        result.cpus[i].cpu_id = i;
        result.cpus[i].type = CoreType_Unknown;
        result.cpus[i].max_freq_khz = 0;
    }

    logc_info(CPU_TOPOLOGY_ID, "CPU topology: %d cores (all Unknown on this platform)", result.count);

    return result;
}

#endif

bool fl_cpu_set_thread_affinity(Thread* thread, CpuIdList cpus) {
    return os_thread_set_affinity(thread, cpus.ids, cpus.count);
}

CpuIdList fl_cpu_query_by_type(CoreType type, FlArena* arena) {
    CpuIdList result = { 0 };

    // The topology covers every online CPU; only the matching ids below outlive this call, so it
    // goes on scratch rather than the caller's arena.
    arena_scratch_auto_conflict(temp, arena);
    CpuTopology topo = cpu_query_topology(temp.arena);

    int match_count = 0;
    for_count(i, topo.count) {
        if (topo.cpus[i].type == type) {
            match_count++;
        }
    }

    if (match_count == 0) {
        return result;
    }

    result.ids = arena_alloc_array(arena, int, match_count);
    result.count = match_count;

    int idx = 0;
    for_count(i, topo.count) {
        if (topo.cpus[i].type == type) {
            result.ids[idx++] = topo.cpus[i].cpu_id;
        }
    }

    return result;
}
