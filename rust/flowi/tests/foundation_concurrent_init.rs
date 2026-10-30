//! Constructors racing each other all adopt the one foundation, and a shutdown between rounds
//! takes it fully down so the next round brings up a fresh one.

const CYCLES: usize = 4;

#[test]
fn concurrent_constructors_all_adopt_the_one_foundation() {
    const THREADS: usize = 8;

    for _ in 0..CYCLES {
        let arenas: Vec<usize> = std::thread::scope(|scope| {
            let handles: Vec<_> = (0..THREADS)
                .map(|_| scope.spawn(|| flowi::init(1).as_raw() as usize))
                .collect();
            handles.into_iter().map(|h| h.join().unwrap()).collect()
        });
        assert!(arenas[0] != 0, "the foundation should come up");
        assert!(
            arenas.iter().all(|a| *a == arenas[0]),
            "racing constructors must share one arena, not bring up several"
        );
        // SAFETY: the scoped threads have joined and dropped their views, and nothing else in
        // this single-test binary uses the foundation.
        unsafe { flowi::shutdown() };
        assert!(!flowi::is_up(), "shutdown takes the foundation down");
    }
}
