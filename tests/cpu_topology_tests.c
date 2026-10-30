#include "utest/utest.h"
#include <core/arena.h>
#include <core/core.h>
#include <core/cpu_topology.h>
#include <core/log.h>
#include <core/os/os.h>
#include <stdio.h>

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

// CPU ids are the OS's own numbers, so they need not be dense: an id can exceed the CPU count.
#define CPU_ID_LIMIT 1024

UTEST(cpu_topology, query_basic) {
    FlArena* arena = arena_new(.reserved_size = MB(64));

    CpuTopology topo = cpu_query_topology(arena);

    ASSERT_GT(topo.count, 0);
    ASSERT_NE(topo.cpus, nullptr);

    int total_classified = topo.performance_count + topo.efficiency_count;
    int unknown_count = 0;
    for (int i = 0; i < topo.count; i++) {
        if (topo.cpus[i].type == CoreType_Unknown) {
            unknown_count++;
        }
    }
    ASSERT_EQ(total_classified + unknown_count, topo.count);

    fl_arena_destroy(arena);
}

UTEST(cpu_topology, query_cpu_ids_valid) {
    FlArena* arena = arena_new(.reserved_size = MB(64));

    CpuTopology topo = cpu_query_topology(arena);

    for (int i = 0; i < topo.count; i++) {
        ASSERT_GE(topo.cpus[i].cpu_id, 0);
        ASSERT_LT(topo.cpus[i].cpu_id, CPU_ID_LIMIT);
    }

    fl_arena_destroy(arena);
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

#if PLATFORM_LINUX
UTEST(cpu_topology, linux_detects_core_types) {
    FlArena* arena = arena_new(.reserved_size = MB(64));

    CpuTopology topo = cpu_query_topology(arena);

    // Frequency info is missing in VMs/containers without /sys/devices/system/cpu access.
    bool has_frequency_info = false;
    for (int i = 0; i < topo.count; i++) {
        if (topo.cpus[i].max_freq_khz > 0) {
            has_frequency_info = true;
            break;
        }
    }

    if (has_frequency_info) {
        int classified_count = topo.performance_count + topo.efficiency_count;
        ASSERT_GT(classified_count, 0);

        for (int i = 0; i < topo.count; i++) {
            if (topo.cpus[i].max_freq_khz > 0) {
                ASSERT_TRUE(topo.cpus[i].type == CoreType_Performance || topo.cpus[i].type == CoreType_Efficiency);
            }
        }
    }

    fl_arena_destroy(arena);
}

UTEST(cpu_topology, linux_frequency_info) {
    FlArena* arena = arena_new(.reserved_size = MB(64));

    CpuTopology topo = cpu_query_topology(arena);

    for (int i = 0; i < topo.count; i++) {
        if (topo.cpus[i].max_freq_khz > 0) {
            ASSERT_GT(topo.cpus[i].max_freq_khz, 100000);   // > 100 MHz
            ASSERT_LT(topo.cpus[i].max_freq_khz, 10000000); // < 10 GHz
        }
    }

    // max_freq_khz may be 0 in containers/VMs without sysfs access.

    fl_arena_destroy(arena);
}
#endif

#if PLATFORM_WINDOWS || PLATFORM_MACOS
UTEST(cpu_topology, non_linux_returns_unknown) {
    FlArena* arena = arena_new(.reserved_size = MB(64));

    CpuTopology topo = cpu_query_topology(arena);

    for (int i = 0; i < topo.count; i++) {
        ASSERT_EQ((int)topo.cpus[i].type, (int)CoreType_Unknown);
        ASSERT_EQ(topo.cpus[i].max_freq_khz, 0);
    }

    ASSERT_EQ(topo.performance_count, 0);
    ASSERT_EQ(topo.efficiency_count, 0);

    fl_arena_destroy(arena);
}
#endif

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

UTEST(cpu_topology, query_by_type_performance) {
    FlArena* arena = arena_new(.reserved_size = MB(64));

    CpuIdList perf_cpus = fl_cpu_query_by_type(CoreType_Performance, arena);

    CpuTopology topo = cpu_query_topology(arena);
    ASSERT_EQ(perf_cpus.count, topo.performance_count);

    if (perf_cpus.count > 0) {
        ASSERT_NE(perf_cpus.ids, nullptr);

        for (int i = 0; i < perf_cpus.count; i++) {
            ASSERT_GE(perf_cpus.ids[i], 0);
            ASSERT_LT(perf_cpus.ids[i], CPU_ID_LIMIT);

            bool found = false;
            for (int j = 0; j < topo.count; j++) {
                if (topo.cpus[j].cpu_id == perf_cpus.ids[i]) {
                    ASSERT_EQ((int)topo.cpus[j].type, (int)CoreType_Performance);
                    found = true;
                    break;
                }
            }
            ASSERT_TRUE(found);
        }
    }

    fl_arena_destroy(arena);
}

UTEST(cpu_topology, query_by_type_efficiency) {
    FlArena* arena = arena_new(.reserved_size = MB(64));

    CpuIdList eff_cpus = fl_cpu_query_by_type(CoreType_Efficiency, arena);

    CpuTopology topo = cpu_query_topology(arena);
    ASSERT_EQ(eff_cpus.count, topo.efficiency_count);

    if (eff_cpus.count > 0) {
        ASSERT_NE(eff_cpus.ids, nullptr);

        for (int i = 0; i < eff_cpus.count; i++) {
            ASSERT_GE(eff_cpus.ids[i], 0);
            ASSERT_LT(eff_cpus.ids[i], CPU_ID_LIMIT);

            bool found = false;
            for (int j = 0; j < topo.count; j++) {
                if (topo.cpus[j].cpu_id == eff_cpus.ids[i]) {
                    ASSERT_EQ((int)topo.cpus[j].type, (int)CoreType_Efficiency);
                    found = true;
                    break;
                }
            }
            ASSERT_TRUE(found);
        }
    }

    fl_arena_destroy(arena);
}

UTEST(cpu_topology, query_by_type_unknown) {
    FlArena* arena = arena_new(.reserved_size = MB(64));

    CpuIdList unknown_cpus = fl_cpu_query_by_type(CoreType_Unknown, arena);

    CpuTopology topo = cpu_query_topology(arena);

    int expected_unknown = 0;
    for (int i = 0; i < topo.count; i++) {
        if (topo.cpus[i].type == CoreType_Unknown) {
            expected_unknown++;
        }
    }

    ASSERT_EQ(unknown_cpus.count, expected_unknown);

    fl_arena_destroy(arena);
}

UTEST(cpu_topology, query_by_type_leaves_only_the_id_list_in_the_arena) {
    FlArena* arena = arena_new(.reserved_size = MB(64));

    FlArenaStats before;
    arena_stats(arena, &before);

    CpuIdList unknown_cpus = fl_cpu_query_by_type(CoreType_Unknown, arena);

    FlArenaStats after;
    arena_stats(arena, &after);

    // The intermediate topology covers every online CPU and belongs on scratch; only the ids and
    // their alignment padding may land in the caller's arena.
    const u64 max_expected = (u64)unknown_cpus.count * sizeof(int) + 64;
    ASSERT_LE(after.pos - before.pos, max_expected);

    fl_arena_destroy(arena);
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

// Linux is the only platform that performs affinity work; everywhere else the call reports the
// request as unsupported rather than as applied, so a well-formed request is expected to fail there.
#if PLATFORM_LINUX
#define ASSERT_AFFINITY_APPLIED(result) ASSERT_TRUE(result)
#else
#define ASSERT_AFFINITY_APPLIED(result) ASSERT_FALSE(result)
#endif

UTEST(cpu_topology, set_affinity_current_thread) {
    FlArena* arena = arena_new(.reserved_size = MB(64));

    CpuTopology topo = cpu_query_topology(arena);
    ASSERT_GT(topo.count, 0);

    CpuIdList cpus;
    cpus.ids = &topo.cpus[0].cpu_id;
    cpus.count = 1;

    bool result = fl_cpu_set_thread_affinity(NULL, cpus);
    ASSERT_AFFINITY_APPLIED(result);

    fl_arena_destroy(arena);
}

// An empty list asks for no constraint, which the supported path applies by doing nothing.
UTEST(cpu_topology, set_affinity_empty_list) {
    CpuIdList empty_cpus = { .ids = NULL, .count = 0 };

    bool result = fl_cpu_set_thread_affinity(NULL, empty_cpus);
    ASSERT_AFFINITY_APPLIED(result);
}

UTEST(cpu_topology, set_affinity_multiple_cpus) {
    FlArena* arena = arena_new(.reserved_size = MB(64));

    CpuTopology topo = cpu_query_topology(arena);

    if (topo.count >= 2) {
        int cpu_ids[2] = { topo.cpus[0].cpu_id, topo.cpus[1].cpu_id };
        CpuIdList cpus = { .ids = cpu_ids, .count = 2 };

        bool result = fl_cpu_set_thread_affinity(NULL, cpus);
        ASSERT_AFFINITY_APPLIED(result);
    }

    fl_arena_destroy(arena);
}

// A cpu id the platform cannot have must fail outright rather than being dropped from the set and
// the remainder reported as applied. Fails on every platform, for different reasons.
UTEST(cpu_topology, set_affinity_rejects_an_out_of_range_cpu_id) {
    FlArena* arena = arena_new(.reserved_size = MB(64));

    CpuTopology topo = cpu_query_topology(arena);
    ASSERT_GT(topo.count, 0);

    int out_of_range = 1 << 20;
    CpuIdList only_bogus = { .ids = &out_of_range, .count = 1 };
    ASSERT_FALSE(fl_cpu_set_thread_affinity(NULL, only_bogus));

    int mixed[2] = { topo.cpus[0].cpu_id, out_of_range };
    CpuIdList partly_bogus = { .ids = mixed, .count = 2 };
    ASSERT_FALSE(fl_cpu_set_thread_affinity(NULL, partly_bogus));

    fl_arena_destroy(arena);
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

UTEST(cpu_topology, integration_query_and_pin) {
    FlArena* arena = arena_new(.reserved_size = MB(64));

    CpuTopology topo = cpu_query_topology(arena);
    ASSERT_GT(topo.count, 0);

    CpuIdList perf_cpus = fl_cpu_query_by_type(CoreType_Performance, arena);

    if (perf_cpus.count > 0) {
        bool result = fl_cpu_set_thread_affinity(NULL, perf_cpus);
        ASSERT_AFFINITY_APPLIED(result);
    }

    CpuIdList eff_cpus = fl_cpu_query_by_type(CoreType_Efficiency, arena);
    if (eff_cpus.count > 0) {
        bool result = fl_cpu_set_thread_affinity(NULL, eff_cpus);
        ASSERT_AFFINITY_APPLIED(result);
    }

    fl_arena_destroy(arena);
}
