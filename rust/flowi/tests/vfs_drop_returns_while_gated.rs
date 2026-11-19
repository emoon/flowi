//! Dropping an in-flight read on the UI thread returns while the operation is still
//! unfinished. The sole job worker is parked inside a `read_with` transform behind a gate
//! this thread opens only after the drop has returned. The dropped read's own transform slot
//! is still held while the worker can reach it, and released exactly once when the VFS frees
//! the handle.

use std::path::Path;
use std::sync::atomic::{AtomicBool, Ordering};
use std::sync::Arc;
use std::time::{Duration, Instant};

use flowi::{Application, EmbeddedSettings, Poll, Vfs};

const DEADLINE: Duration = Duration::from_secs(10);

fn wait_until(what: &str, mut done: impl FnMut() -> bool) {
    let deadline = Instant::now() + DEADLINE;
    while !done() {
        assert!(Instant::now() < deadline, "timed out waiting for {what}");
        std::thread::sleep(Duration::from_millis(1));
    }
}

/// Turns a blocking close into a failure instead of a hang.
fn spawn_watchdog() -> Arc<AtomicBool> {
    let finished = Arc::new(AtomicBool::new(false));
    let watched = Arc::clone(&finished);
    std::thread::spawn(move || {
        std::thread::sleep(DEADLINE * 3);
        if !watched.load(Ordering::Acquire) {
            eprintln!("dropping the gated handle never returned");
            std::process::abort();
        }
    });
    finished
}

/// Polls a parked read to completion once its gate has opened.
fn poll_parked_len(parker: &mut flowi::MappedRead<usize>) -> usize {
    let deadline = Instant::now() + DEADLINE;
    loop {
        match parker.poll() {
            Poll::Ready(value) => return value.expect("the parked read"),
            Poll::Pending => {
                assert!(Instant::now() < deadline, "the parked read never completed");
                std::thread::sleep(Duration::from_millis(1));
            }
        }
    }
}

fn drop_in_flight_read(dir: &Path) {
    let mount = Vfs::mount(dir.to_str().expect("utf-8 temp path")).expect("mount");
    wait_until("the mount", || mount.is_ready());

    let parked = Arc::new(AtomicBool::new(false));
    let gate = Arc::new(AtomicBool::new(false));
    let (parked_flag, gate_flag) = (Arc::clone(&parked), Arc::clone(&gate));
    let mut parker = mount
        .read_with("parked.txt", move |bytes| {
            parked_flag.store(true, Ordering::Release);
            while !gate_flag.load(Ordering::Acquire) {
                std::thread::sleep(Duration::from_millis(1));
            }
            bytes.len()
        })
        .expect("launch the parking read");
    wait_until("the worker to park", || parked.load(Ordering::Acquire));

    // Queued behind the parked worker: it cannot run until the gate opens. The transform
    // captures an Arc, so its strong count says whether the slot is still alive.
    let witness = Arc::new(AtomicBool::new(false));
    let victim_ran = Arc::clone(&witness);
    let victim = mount
        .read_with("dropped.txt", move |_bytes| {
            victim_ran.store(true, Ordering::Release)
        })
        .expect("launch the read to drop");
    drop(victim);
    // The drop has returned; the rest checks that it returned early.
    assert!(
        !witness.load(Ordering::Acquire),
        "the dropped read has not run: the worker is still parked"
    );
    assert_eq!(
        Arc::strong_count(&witness),
        2,
        "the transform is still held for the worker after the drop"
    );

    gate.store(true, Ordering::Release);
    assert_eq!(poll_parked_len(&mut parker), b"parked".len());
    drop(parker);
    // Closing the mount drains the dropped read's job and reclaims its handle.
    drop(mount);
    assert!(
        witness.load(Ordering::Acquire),
        "the dropped read still ran to completion on the worker"
    );
    assert_eq!(
        Arc::strong_count(&witness),
        1,
        "the transform was handed back exactly once"
    );
}

#[test]
fn dropping_an_in_flight_handle_returns_while_the_read_is_gated() {
    let finished = spawn_watchdog();

    // With one worker, the parked transform gates everything launched after it.
    let _app = Application::embedded(EmbeddedSettings::new(32, 32).with_job_threads(1))
        .expect("embedded flowi");

    let dir = std::env::temp_dir().join(format!("flowi-vfs-drop-{}", std::process::id()));
    std::fs::create_dir_all(&dir).expect("temp dir");
    std::fs::write(dir.join("parked.txt"), b"parked").expect("parked file");
    std::fs::write(dir.join("dropped.txt"), b"dropped").expect("dropped file");

    drop_in_flight_read(&dir);

    finished.store(true, Ordering::Release);
    std::fs::remove_dir_all(&dir).expect("remove temp dir");
}
