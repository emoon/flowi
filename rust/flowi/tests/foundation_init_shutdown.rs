//! init adopts a foundation that is already up and shutdown is a no-op when it is already
//! down, so neither a doubled call nor a bring-up after a teardown may misbehave.

use flowi::Jobs;

const CYCLES: usize = 4;

/// Bigger than anything in the process that is not a flowi arena: an arena reserves 2 GB up
/// front, while the largest reservation anyone else makes is the allocator's own 1 GB region.
#[cfg(target_os = "linux")]
const ARENA_SIZED_KB: u64 = 3 * 1024 * 1024 / 2;

/// How many arena-sized address-space reservations the process holds, or None off Linux,
/// where there is no /proc/self/maps to count them in.
#[cfg(target_os = "linux")]
fn arena_reservations() -> Option<usize> {
    let maps = std::fs::read_to_string("/proc/self/maps").expect("/proc/self/maps is readable");
    Some(
        maps.lines()
            .filter_map(|line| {
                let (start, end) = line.split_once(' ')?.0.split_once('-')?;
                let start = u64::from_str_radix(start, 16).ok()?;
                let end = u64::from_str_radix(end, 16).ok()?;
                Some(end.saturating_sub(start) / 1024)
            })
            .filter(|size_kb| *size_kb >= ARENA_SIZED_KB)
            .count(),
    )
}

#[cfg(not(target_os = "linux"))]
fn arena_reservations() -> Option<usize> {
    None
}

#[test]
fn the_foundation_survives_a_full_up_down_up_cycle() {
    let first = flowi::init(2);
    let first_raw = first.as_raw();
    assert!(!first_raw.is_null(), "the foundation should come up");
    assert_eq!(Jobs::num_threads(), 2, "the first bring-up sizes the jobs");

    let adopted = flowi::init(1);
    assert_eq!(
        adopted.as_raw(),
        first_raw,
        "a second init adopts the running foundation"
    );
    assert_eq!(Jobs::num_threads(), 2, "adopting ignores the new count");
    drop((first, adopted));

    // SAFETY: the only views were dropped above, and this single-test binary holds no other
    // handle and runs no other thread.
    unsafe { flowi::shutdown() };
    // Already down: the second call has nothing left to destroy and must not double-free.
    // SAFETY: as above - the foundation is already down and nothing uses it.
    unsafe { flowi::shutdown() };

    let second = flowi::init(1);
    assert!(
        !second.as_raw().is_null(),
        "the foundation should come back up after a shutdown"
    );
    assert_eq!(
        Jobs::num_threads(),
        1,
        "a fresh bring-up sizes the jobs again"
    );
    // Proves the count below is measuring something: a live foundation holds the main arena
    // and the caller thread's two scratch arenas.
    if let Some(live) = arena_reservations() {
        assert!(
            live >= 3,
            "a live foundation should hold its arena reservations"
        );
    }

    drop(second);
    // SAFETY: the only view was dropped above; nothing else uses the foundation.
    unsafe { flowi::shutdown() };

    for _ in 0..CYCLES {
        let arena = flowi::init(1);
        assert!(!arena.as_raw().is_null(), "every cycle should come up");
        drop(arena);
        // SAFETY: the cycle's only view was dropped above; nothing else uses the foundation.
        unsafe { flowi::shutdown() };
    }

    if let Some(stranded) = arena_reservations() {
        assert_eq!(
            stranded, 0,
            "shutdown must hand every arena reservation back, not just survive a re-init"
        );
    }
}
