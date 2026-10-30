//! Watchers started from several threads share one process-wide registry, so starting, polling and
//! stopping distinct watchers concurrently must not tear the watcher array or its lazily-built lock.
//!
//! Starting a watcher does not bootstrap the foundation, so the test brings it up first.

#![forbid(unsafe_code)]

use flowi::{FileWatcher, WatchConfig};
use std::sync::{Arc, Barrier};

/// Below the registry's 16 slots, so a round never fails for want of one.
const THREADS: usize = 4;
const ROUNDS: usize = 8;

#[test]
fn distinct_watchers_start_poll_and_stop_concurrently() {
    let _arena = flowi::init(1);

    let root =
        std::env::temp_dir().join(format!("flowi_watcher_concurrent_{}", std::process::id()));
    let _ = std::fs::remove_dir_all(&root);

    let barrier = Arc::new(Barrier::new(THREADS));
    let threads: Vec<_> = (0..THREADS)
        .map(|index| {
            let dir = root.join(index.to_string());
            std::fs::create_dir_all(&dir).expect("a writable temp directory");
            let barrier = Arc::clone(&barrier);

            std::thread::spawn(move || {
                let path = dir.to_str().expect("a UTF-8 temp path").to_owned();

                // Every thread is inside the registry at the same time, not one after another.
                barrier.wait();

                for round in 0..ROUNDS {
                    let watcher = FileWatcher::start(&path, WatchConfig::default())
                        .expect("a watch on an existing directory should start");
                    assert!(watcher.is_active(), "a freshly started watch is active");

                    std::fs::write(dir.join(format!("file_{round}")), b"payload")
                        .expect("a writable temp directory");

                    if watcher.has_changes() {
                        watcher.take_changes();
                    }
                }
            })
        })
        .collect();

    for thread in threads {
        thread.join().expect("no thread should panic or deadlock");
    }

    let _ = std::fs::remove_dir_all(&root);
}
