#include "utest/utest.h"
#include <core/arena.h>
#include <core/core.h>
#include <core/cpu_topology.h>
#include <core/log.h>
#include <core/os/os.h>
#include <stdio.h>

// CPU ids are the OS's own numbers, so they need not be dense: an id can exceed the CPU count.
#define CPU_ID_LIMIT 1024

static int count_unknown_cpus(const CpuTopology* topo) {
    int unknown_count = 0;
    for (int i = 0; i < topo->count; i++) {
        if (topo->cpus[i].type == CoreType_Unknown) {
            unknown_count++;
        }
    }
    return unknown_count;
}

static const CpuInfo* find_cpu(const CpuTopology* topo, int cpu_id) {
    for (int i = 0; i < topo->count; i++) {
        if (topo->cpus[i].cpu_id == cpu_id) {
            return &topo->cpus[i];
        }
    }
    return nullptr;
}

static void expect_listed_cpu(int* utest_result, const CpuTopology* topo, int cpu_id, CoreType type) {
    ASSERT_GE(cpu_id, 0);
    ASSERT_LT(cpu_id, CPU_ID_LIMIT);

    const CpuInfo* cpu = find_cpu(topo, cpu_id);
    bool found = cpu != nullptr;
    if (found) {
        ASSERT_EQ((int)cpu->type, (int)type);
    }
    ASSERT_TRUE(found);
}

static void expect_id_list_of_type(int* utest_result, CpuIdList list, const CpuTopology* topo, CoreType type) {
    if (list.count <= 0) {
        return;
    }

    ASSERT_NE(list.ids, nullptr);

    for (int i = 0; i < list.count; i++) {
        expect_listed_cpu(utest_result, topo, list.ids[i], type);
    }
}

UTEST(cpu_topology, query_basic) {
    FlArena* arena = arena_new(.reserved_size = MB(64));

    CpuTopology topo = cpu_query_topology(arena);

    ASSERT_GT(topo.count, 0);
    ASSERT_NE(topo.cpus, nullptr);

    int total_classified = topo.performance_count + topo.efficiency_count;
    int unknown_count = count_unknown_cpus(&topo);
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

#if PLATFORM_LINUX
static bool any_cpu_has_frequency(const CpuTopology* topo) {
    for (int i = 0; i < topo->count; i++) {
        if (topo->cpus[i].max_freq_khz > 0) {
            return true;
        }
    }
    return false;
}

static void expect_frequency_cpus_classified(int* utest_result, const CpuTopology* topo) {
    int classified_count = topo->performance_count + topo->efficiency_count;
    ASSERT_GT(classified_count, 0);

    for (int i = 0; i < topo->count; i++) {
        if (topo->cpus[i].max_freq_khz > 0) {
            ASSERT_TRUE(topo->cpus[i].type == CoreType_Performance || topo->cpus[i].type == CoreType_Efficiency);
        }
    }
}

static void expect_plausible_frequency(int* utest_result, const CpuInfo* cpu) {
    if (cpu->max_freq_khz > 0) {
        ASSERT_GT(cpu->max_freq_khz, 100000);   // > 100 MHz
        ASSERT_LT(cpu->max_freq_khz, 10000000); // < 10 GHz
    }
}

UTEST(cpu_topology, linux_detects_core_types) {
    FlArena* arena = arena_new(.reserved_size = MB(64));

    CpuTopology topo = cpu_query_topology(arena);

    // Frequency info is missing in VMs/containers without /sys/devices/system/cpu access.
    if (any_cpu_has_frequency(&topo)) {
        expect_frequency_cpus_classified(utest_result, &topo);
    }

    fl_arena_destroy(arena);
}

UTEST(cpu_topology, linux_frequency_info) {
    FlArena* arena = arena_new(.reserved_size = MB(64));

    CpuTopology topo = cpu_query_topology(arena);

    for (int i = 0; i < topo.count; i++) {
        expect_plausible_frequency(utest_result, &topo.cpus[i]);
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

UTEST(cpu_topology, query_by_type_performance) {
    FlArena* arena = arena_new(.reserved_size = MB(64));

    CpuIdList perf_cpus = fl_cpu_query_by_type(CoreType_Performance, arena);

    CpuTopology topo = cpu_query_topology(arena);
    ASSERT_EQ(perf_cpus.count, topo.performance_count);

    expect_id_list_of_type(utest_result, perf_cpus, &topo, CoreType_Performance);

    fl_arena_destroy(arena);
}

UTEST(cpu_topology, query_by_type_efficiency) {
    FlArena* arena = arena_new(.reserved_size = MB(64));

    CpuIdList eff_cpus = fl_cpu_query_by_type(CoreType_Efficiency, arena);

    CpuTopology topo = cpu_query_topology(arena);
    ASSERT_EQ(eff_cpus.count, topo.efficiency_count);

    expect_id_list_of_type(utest_result, eff_cpus, &topo, CoreType_Efficiency);

    fl_arena_destroy(arena);
}

UTEST(cpu_topology, query_by_type_unknown) {
    FlArena* arena = arena_new(.reserved_size = MB(64));

    CpuIdList unknown_cpus = fl_cpu_query_by_type(CoreType_Unknown, arena);

    CpuTopology topo = cpu_query_topology(arena);

    ASSERT_EQ(unknown_cpus.count, count_unknown_cpus(&topo));

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

// Linux is the only platform that performs affinity work; everywhere else the call reports the
// request as unsupported rather than as applied, so a well-formed request is expected to fail there.
#if PLATFORM_LINUX
#define ASSERT_AFFINITY_APPLIED(result) ASSERT_TRUE(result)
#else
#define ASSERT_AFFINITY_APPLIED(result) ASSERT_FALSE(result)
#endif

static void expect_affinity_applied(int* utest_result, CpuIdList cpus) {
    bool result = fl_cpu_set_thread_affinity(nullptr, cpus);
    ASSERT_AFFINITY_APPLIED(result);
}

UTEST(cpu_topology, set_affinity_current_thread) {
    FlArena* arena = arena_new(.reserved_size = MB(64));

    CpuTopology topo = cpu_query_topology(arena);
    ASSERT_GT(topo.count, 0);

    CpuIdList cpus;
    cpus.ids = &topo.cpus[0].cpu_id;
    cpus.count = 1;

    expect_affinity_applied(utest_result, cpus);

    fl_arena_destroy(arena);
}

// An empty list asks for no constraint, which the supported path applies by doing nothing.
UTEST(cpu_topology, set_affinity_empty_list) {
    CpuIdList empty_cpus = { .ids = nullptr, .count = 0 };

    expect_affinity_applied(utest_result, empty_cpus);
}

UTEST(cpu_topology, set_affinity_multiple_cpus) {
    FlArena* arena = arena_new(.reserved_size = MB(64));

    CpuTopology topo = cpu_query_topology(arena);

    if (topo.count >= 2) {
        int cpu_ids[2] = { topo.cpus[0].cpu_id, topo.cpus[1].cpu_id };
        CpuIdList cpus = { .ids = cpu_ids, .count = 2 };

        expect_affinity_applied(utest_result, cpus);
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
    ASSERT_FALSE(fl_cpu_set_thread_affinity(nullptr, only_bogus));

    int mixed[2] = { topo.cpus[0].cpu_id, out_of_range };
    CpuIdList partly_bogus = { .ids = mixed, .count = 2 };
    ASSERT_FALSE(fl_cpu_set_thread_affinity(nullptr, partly_bogus));

    fl_arena_destroy(arena);
}

UTEST(cpu_topology, integration_query_and_pin) {
    FlArena* arena = arena_new(.reserved_size = MB(64));

    CpuTopology topo = cpu_query_topology(arena);
    ASSERT_GT(topo.count, 0);

    CpuIdList perf_cpus = fl_cpu_query_by_type(CoreType_Performance, arena);

    if (perf_cpus.count > 0) {
        expect_affinity_applied(utest_result, perf_cpus);
    }

    CpuIdList eff_cpus = fl_cpu_query_by_type(CoreType_Efficiency, arena);
    if (eff_cpus.count > 0) {
        expect_affinity_applied(utest_result, eff_cpus);
    }

    fl_arena_destroy(arena);
}
