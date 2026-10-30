//! VFS cases driven by the fake VfsApi table in [`super::fake`].

use super::fake::*;
use super::mount::MOUNT_ERRORS;
use super::*;
use std::sync::Arc;

/// Bring-up passes the caller's arena straight through and reports what the C
/// says - a refusal (a job system too large for the handle array, say) is a
/// false the caller has to act on, not something the wrapper swallows.
#[test]
fn register_builtin_drivers_reaches_the_c_once_per_call() {
    reset_fake(FakeState::default());
    Vfs::register_builtin_drivers();
    assert_eq!(with_fake(|f| f.builtin_registrations), 1);
}

#[test]
fn init_forwards_the_arena_and_reports_refusal() {
    reset_fake(FakeState::default());
    let arena = crate::Arena::new(1 << 16);
    assert!(Vfs::init(&arena));
    assert_eq!(
        with_fake(|f| f.init_arena),
        arena.as_raw(),
        "the caller's arena must reach the C unchanged"
    );

    reset_fake(FakeState {
        fail: true,
        ..Default::default()
    });
    assert!(!Vfs::init(&arena), "a refused bring-up must report false");
}

// -----------------------------------------------------------------------
// Mounting.

#[test]
fn mount_maps_every_error_status_to_its_own_variant() {
    // The same table MountError::from_status and its Display are generated from, so a
    // status that gained no mapping cannot pass here either.
    for (status, expected, text) in MOUNT_ERRORS {
        reset_fake(FakeState {
            mount_status: *status,
            ..Default::default()
        });
        assert_eq!(
            Vfs::mount("/anything").err(),
            Some(*expected),
            "status {status:?}"
        );
        assert_eq!(expected.to_string(), *text, "status {status:?}");
    }
}

/// The handle is what a caller schedules its reads after, so it must be the
/// mount's own - not a fresh or invalid one. The zero case is the mount a
/// driver resolved without scheduling anything, and it has to survive the trip
/// intact rather than being turned into some "not ready yet" stand-in: the job
/// system reads 0 as "no dependency", which is exactly right for that mount.
#[test]
fn a_mount_reports_its_own_bring_up_job() {
    reset_fake(FakeState {
        mount_job_handle: 0x2a,
        ..Default::default()
    });
    assert_eq!(Vfs::mount("/data").expect("mount").job_handle().0, 0x2a);

    reset_fake(FakeState::default());
    assert_eq!(
        Vfs::mount("/data").expect("mount").job_handle(),
        crate::JobHandle::INVALID,
        "a mount with no async bring-up must report the no-dependency handle"
    );
}

#[test]
fn a_success_status_with_no_mount_is_a_failure_not_a_null_mount() {
    reset_fake(FakeState {
        mount_null: true,
        ..Default::default()
    });
    assert_eq!(Vfs::mount("/anything").err(), Some(MountError::MountFailed));
}

#[test]
fn a_successful_mount_closes_exactly_once_on_drop() {
    reset_fake(FakeState::default());
    {
        let mount = Vfs::mount("/data").expect("mount");
        assert_eq!(mount.source_path(), "/fake/source");
        assert_eq!(
            mount.info(),
            MountInfo {
                path: "/fake/source".to_string(),
                is_ready: true
            }
        );
        assert_eq!(mount.status(), MountStatus::Ready);
        assert!(mount.is_ready());
        assert_eq!(with_fake(|f| f.mount_close_count), 0);
    }
    assert_eq!(with_fake(|f| f.mount_close_count), 1);
}

#[test]
fn a_leaked_mount_is_never_closed_and_is_shareable() {
    reset_fake(FakeState::default());
    let leaked: &'static Mount = Vfs::mount("/data").expect("mount").leak();
    let (first, second) = (leaked, leaked);
    assert_eq!(first.source_path(), second.source_path());
    assert_eq!(with_fake(|f| f.mount_close_count), 0);
}

#[test]
fn mount_with_forwards_its_options() {
    reset_fake(FakeState::default());
    let options = MountOptions {
        cache_dir: "/tmp/cache",
        cache_validation: true,
    };
    let lowered = options.to_sys();
    assert!(lowered.cache_validation);
    assert_eq!(lowered.cache_dir.into_string(), "/tmp/cache");
    assert!(Vfs::mount_with("/data", options).is_ok());
}

// -----------------------------------------------------------------------
// The frame-loop surface.

#[test]
fn read_polls_pending_then_completes() {
    reset_fake(FakeState {
        polls_until_ready: 2,
        data: b"hello".to_vec(),
        ..Default::default()
    });
    let mount = Vfs::mount("/data").expect("mount");

    let mut handle = mount.read("foo.txt").expect("launch");
    assert!(matches!(handle.poll(), Poll::Pending));
    assert!(matches!(handle.poll(), Poll::Pending));
    match handle.poll() {
        Poll::Ready(Ok(data)) => assert_eq!(data.as_slice(), b"hello"),
        other => panic!("expected ready-ok, got {other:?}"),
    }

    drop(handle);
    assert_eq!(with_fake(|f| f.close_count), 1);
}

#[test]
fn read_failure_propagates() {
    reset_fake(FakeState {
        fail: true,
        ..Default::default()
    });
    let mount = Vfs::mount("/data").expect("mount");
    let mut handle = mount.read("missing.txt").expect("launch");
    assert_eq!(handle.poll(), Poll::Ready(Err(FlError::GenericError)));
}

#[test]
fn invalid_launch_is_none_not_sentinel() {
    reset_fake(FakeState::default());
    let mount = Vfs::mount("/data").expect("mount");
    with_fake(|f| f.launch_invalid = true);
    assert!(mount.read("foo.txt").is_none());
    assert!(mount.listing("/", 1).is_none());
    assert!(mount.read_with("foo.txt", |bytes| bytes.len()).is_none());
}

#[test]
#[should_panic(expected = "handle polled after completion")]
fn poll_after_ready_panics() {
    reset_fake(FakeState {
        data: b"x".to_vec(),
        ..Default::default()
    });
    let mount = Vfs::mount("/data").expect("mount");
    let mut handle = mount.read("foo.txt").expect("launch");
    assert!(matches!(handle.poll(), Poll::Ready(Ok(_))));
    let _ = handle.poll();
}

#[test]
fn drop_before_completion_closes_without_blocking() {
    // is_ready would never fire (the op stays pending); if Drop join-spun on
    // completion this test would hang. It returns, and the ticket is closed.
    reset_fake(FakeState {
        polls_until_ready: i32::MAX,
        ..Default::default()
    });
    let mount = Vfs::mount("/data").expect("mount");
    let mut handle = mount.read("slow.txt").expect("launch");
    assert!(matches!(handle.poll(), Poll::Pending));
    drop(handle);
    assert_eq!(with_fake(|f| f.close_count), 1);
}

#[test]
fn read_empty_data_completes_without_dereferencing_null() {
    // Empty file: size == 0 (with a non-null dangling data pointer, as an
    // empty Vec yields). The poll must take the zero-length branch - never
    // calling from_raw_parts on that pointer - and yield an empty VfsData.
    reset_fake(FakeState::default());
    let mount = Vfs::mount("/data").expect("mount");
    let mut handle = mount.read("empty.txt").expect("launch");
    match handle.poll() {
        Poll::Ready(Ok(data)) => {
            assert!(data.is_empty());
            assert_eq!(data.len(), 0);
        }
        other => panic!("expected ready-ok, got {other:?}"),
    }
}

#[test]
fn a_listing_owns_its_entries_and_outlives_the_close() {
    reset_fake(FakeState {
        entries: vec![entry("sub", true), entry("game.zip", false)],
        ..Default::default()
    });
    let mount = Vfs::mount("/data").expect("mount");

    let list = {
        let mut handle = mount.listing(".", 1).expect("launch");
        match handle.poll() {
            Poll::Ready(Ok(list)) => list,
            other => panic!("expected ready-ok, got {other:?}"),
        }
        // handle drops here: the C ticket is closed and its entry storage is
        // gone. The list below must still read cleanly.
    };
    assert_eq!(with_fake(|f| f.close_count), 1);
    // Overwrite the fake's entry storage the way a recycled listing would.
    reset_fake(FakeState {
        entries: vec![entry("unrelated", false)],
        ..Default::default()
    });

    assert_eq!(list.len(), 2);
    assert!(!list.is_empty());
    let names: Vec<&str> = list.iter().map(|entry| entry.name()).collect();
    assert_eq!(names, ["sub", "game.zip"]);
    let second = list.iter().nth(1).expect("second entry");
    assert_eq!(second.size(), 42);
    assert_eq!(second.attributes(), 0x8000);
    assert!(!second.is_directory());
    assert!(second.is_archive());
    assert!(list.iter().next().expect("first entry").is_directory());
}

#[test]
fn an_empty_listing_succeeds_with_no_entries() {
    reset_fake(FakeState::default());
    let mount = Vfs::mount("/data").expect("mount");
    let mut handle = mount.listing(".", 1).expect("launch");
    match handle.poll() {
        Poll::Ready(Ok(list)) => {
            assert!(list.is_empty());
            assert_eq!(list.len(), 0);
            assert_eq!(list.iter().count(), 0);
        }
        other => panic!("expected ready-ok, got {other:?}"),
    }
}

#[test]
fn listing_failure_propagates() {
    reset_fake(FakeState {
        fail: true,
        ..Default::default()
    });
    let mount = Vfs::mount("/data").expect("mount");
    let mut handle = mount.listing("/", 1).expect("launch");
    assert_eq!(handle.poll(), Poll::Ready(Err(FlError::GenericError)));
}

#[test]
fn listing_drop_closes_ticket() {
    reset_fake(FakeState {
        entries: vec![entry("one", false)],
        ..Default::default()
    });
    let mount = Vfs::mount("/data").expect("mount");
    let handle = mount.listing("/", 1).expect("launch");
    drop(handle);
    assert_eq!(with_fake(|f| f.close_count), 1);
}

// -----------------------------------------------------------------------
// The closure-transported read.

#[test]
fn read_with_runs_the_closure_on_the_read_bytes() {
    reset_fake(FakeState {
        data: b"one\ntwo\n".to_vec(),
        ..Default::default()
    });
    let mount = Vfs::mount("/data").expect("mount");

    // A transform that produces an owned, non-Copy value: what it hands back
    // has to survive the trip through the slot intact.
    let mut read = mount
        .read_with("list.txt", |bytes| {
            String::from_utf8_lossy(bytes)
                .lines()
                .map(str::to_string)
                .collect::<Vec<String>>()
        })
        .expect("launch");
    match read.poll() {
        Poll::Ready(Ok(lines)) => assert_eq!(lines, ["one", "two"]),
        other => panic!("expected the transformed value, got {other:?}"),
    }
    assert_eq!(with_fake(|f| f.last_callback_size), 8);
    drop(read);
    assert_eq!(with_fake(|f| f.close_count), 1);
}

/// A completed read dropped without being polled still holds the value the
/// transform produced; the close hands the slot back and frees it, value and all.
#[test]
fn a_completed_reads_slot_is_freed_at_its_close() {
    reset_fake(FakeState {
        data: b"x".to_vec(),
        ..Default::default()
    });
    let mount = Vfs::mount("/data").expect("mount");
    let witness = Arc::new(());
    let produced = Arc::clone(&witness);
    let read = mount
        .read_with("list.txt", move |_bytes| produced)
        .expect("launch");
    assert_eq!(
        Arc::strong_count(&witness),
        2,
        "the slot holds the value the transform produced"
    );
    drop(read);
    assert_eq!(Arc::strong_count(&witness), 1, "the close freed the slot");
    assert_eq!(with_fake(|f| f.close_count), 1);
}

#[test]
fn read_with_reports_a_panicking_transform_as_a_failed_read() {
    reset_fake(FakeState {
        data: b"junk".to_vec(),
        ..Default::default()
    });
    let mount = Vfs::mount("/data").expect("mount");
    let mut read = mount
        .read_with("list.txt", |_bytes| -> u32 { panic!("transform blew up") })
        .expect("launch");
    assert_eq!(read.poll(), Poll::Ready(Err(FlError::GenericError)));
}

#[test]
fn read_with_does_not_run_the_transform_on_a_failed_read() {
    reset_fake(FakeState::default());
    let mount = Vfs::mount("/data").expect("mount");
    let witness = Arc::new(());
    let captured = Arc::clone(&witness);
    let ran = Arc::new(std::sync::atomic::AtomicBool::new(false));
    let ran_in = Arc::clone(&ran);
    // Held, so the test can deliver the failure the way the worker does.
    with_fake(|f| f.hold_callback = true);
    let mut read = mount
        .read_with("missing.txt", move |_bytes| {
            let _keep = &captured;
            ran_in.store(true, std::sync::atomic::Ordering::SeqCst);
        })
        .expect("launch");
    run_held_callback(None);
    assert_eq!(read.poll(), Poll::Ready(Err(FlError::GenericError)));
    assert!(!ran.load(std::sync::atomic::Ordering::SeqCst));
    assert_eq!(
        Arc::strong_count(&witness),
        1,
        "the closure was dropped uncalled"
    );
}

#[test]
fn read_with_runs_the_transform_on_an_empty_file() {
    reset_fake(FakeState::default());
    let mount = Vfs::mount("/data").expect("mount");
    with_fake(|f| f.hold_callback = true);
    let mut read = mount
        .read_with("empty.txt", |bytes| bytes.len())
        .expect("launch");
    run_held_callback(Some(&[]));
    assert_eq!(read.poll(), Poll::Ready(Ok(0)));
}

/// A read dropped before the worker reached it: the close is non-blocking, so
/// the transform - and the slot it lives in - must outlive the drop for as long
/// as the worker can still call back, and be freed by that callback.
#[test]
fn read_with_dropped_mid_flight_keeps_its_slot_until_the_worker_lets_go() {
    reset_fake(FakeState {
        polls_until_ready: i32::MAX,
        hold_callback: true,
        data: b"x".to_vec(),
        ..Default::default()
    });
    let mount = Vfs::mount("/data").expect("mount");
    // The transform captures an Arc, so the strong count proves when the closure
    // (and with it the slot) is freed.
    let witness = Arc::new(());
    let captured = Arc::clone(&witness);
    let mut read = mount
        .read_with("slow.txt", move |_bytes| {
            let _ = &captured;
        })
        .expect("launch");
    assert!(matches!(read.poll(), Poll::Pending));
    assert_eq!(Arc::strong_count(&witness), 2);

    drop(read);
    assert_eq!(with_fake(|f| f.close_count), 1);
    assert_eq!(
        Arc::strong_count(&witness),
        2,
        "the slot outlives the dropped read while the worker can still reach it"
    );

    // The worker gets there and reports the read failed (no data): that hand-off
    // is what frees the transform.
    run_held_callback(None);
    assert_eq!(Arc::strong_count(&witness), 1);
}

/// The other order the race can settle in: the worker completes a read whose
/// poller has already dropped it. The transform runs on the bytes, its value has
/// no reader, and the slot is freed - not touched after free.
#[test]
fn read_with_dropped_mid_flight_survives_the_worker_completing_it() {
    reset_fake(FakeState {
        polls_until_ready: i32::MAX,
        hold_callback: true,
        ..Default::default()
    });
    let mount = Vfs::mount("/data").expect("mount");
    let witness = Arc::new(std::sync::atomic::AtomicUsize::new(0));
    let captured = Arc::clone(&witness);
    let read = mount
        .read_with("slow.txt", move |bytes| {
            captured.store(bytes.len(), std::sync::atomic::Ordering::Release);
        })
        .expect("launch");
    drop(read);
    assert_eq!(Arc::strong_count(&witness), 2);

    run_held_callback(Some(b"late"));
    assert_eq!(
        witness.load(std::sync::atomic::Ordering::Acquire),
        4,
        "the transform ran on the completed read's bytes"
    );
    assert_eq!(Arc::strong_count(&witness), 1);
}

/// A mount closed under a live read frees the read's handle, slot included:
/// the read must then poll Pending for good rather than reach the freed slot.
#[test]
fn a_mapped_read_outliving_its_mount_polls_pending_and_frees_its_slot_once() {
    reset_fake(FakeState {
        polls_until_ready: i32::MAX,
        hold_callback: true,
        ..Default::default()
    });
    let mount = Vfs::mount("/data").expect("mount");
    let witness = Arc::new(());
    let captured = Arc::clone(&witness);
    let mut read = mount
        .read_with("slow.txt", move |_bytes| {
            let _ = &captured;
        })
        .expect("launch");
    assert!(matches!(read.poll(), Poll::Pending));

    drop(mount);
    assert_eq!(
        Arc::strong_count(&witness),
        1,
        "the mount's teardown freed the slot"
    );
    assert!(matches!(read.poll(), Poll::Pending));
    assert!(matches!(read.poll(), Poll::Pending));

    drop(read);
    assert_eq!(with_fake(|f| f.close_count), 1);
}

/// A launch the C side refuses never reaches the callback, so the reference
/// handed to it has to come back rather than leak.
#[test]
fn read_with_refused_launch_frees_the_slot() {
    reset_fake(FakeState {
        launch_invalid: true,
        ..Default::default()
    });
    let mount = Vfs::mount("/data").expect("mount");
    let witness = Arc::new(());
    let captured = Arc::clone(&witness);
    assert!(mount
        .read_with("nope.txt", move |_bytes| {
            let _ = &captured;
        })
        .is_none());
    assert_eq!(Arc::strong_count(&witness), 1);
}

#[test]
#[should_panic(expected = "handle polled after completion")]
fn read_with_yields_its_value_exactly_once() {
    reset_fake(FakeState {
        data: b"x".to_vec(),
        ..Default::default()
    });
    let mount = Vfs::mount("/data").expect("mount");
    let mut read = mount
        .read_with("list.txt", |bytes| bytes.len())
        .expect("launch");
    assert_eq!(read.poll(), Poll::Ready(Ok(1)));
    let _ = read.poll();
}

// -----------------------------------------------------------------------
// The worker surface.

#[test]
fn a_worker_view_reads_blocking_on_the_thread_that_holds_it() {
    reset_fake(FakeState {
        // Never ready by polling: only the fake's wait flips it ready, which is
        // what makes this exercise the blocking surface rather than a poll.
        polls_until_ready: i32::MAX,
        data: b"payload".to_vec(),
        entries: vec![entry("a", false), entry("b", true)],
        ..Default::default()
    });
    let mount = Vfs::mount("/data").expect("mount");
    // SAFETY: the mount outlives every use of the view below.
    let view = unsafe { mount.worker_view() };

    let data = view.read("file.bin").expect("blocking read");
    assert_eq!(data.as_slice(), b"payload");
    let list = view.listing(".", 1).expect("blocking listing");
    assert_eq!(list.len(), 2);
    assert_eq!(with_fake(|f| f.wait_count), 2);
    assert_eq!(with_fake(|f| f.close_count), 2);

    // The queries reached through Deref are the same ones Mount carries.
    assert_eq!(view.source_path(), "/fake/source");

    // And the view is Send: a job body can own one. That it works once
    // moved is the next test.
    fn assert_send<T: Send>(_: &T) {}
    assert_send(&view);
    let cloned = view.clone();
    assert_send(&cloned);
}

#[test]
fn a_worker_view_moved_to_another_thread_reads_through_it() {
    // The fake table is per-thread, so the spawned thread installs its own.
    reset_fake(FakeState {
        polls_until_ready: i32::MAX,
        data: b"from the worker".to_vec(),
        entries: vec![entry("a", false), entry("b", true)],
        ..Default::default()
    });
    let mount = Vfs::mount("/data").expect("mount");
    // SAFETY: the mount outlives the joined thread below, so it is open for
    // every use of the view.
    let view = unsafe { mount.worker_view() };

    let worker = std::thread::spawn(move || {
        reset_fake(FakeState {
            polls_until_ready: i32::MAX,
            data: b"from the worker".to_vec(),
            entries: vec![entry("a", false), entry("b", true)],
            ..Default::default()
        });
        let data = view.read("file.bin").expect("blocking read on the worker");
        let list = view
            .listing(".", 1)
            .expect("blocking listing on the worker");
        // The mount queries reached through Deref work here too.
        let source = view.source_path().to_string();
        (
            data.into_bytes(),
            list.len(),
            source,
            with_fake(|f| f.wait_count),
        )
    });

    let (bytes, count, source, waits) = worker.join().expect("the worker thread panicked");
    assert_eq!(bytes, b"from the worker");
    assert_eq!(count, 2);
    assert_eq!(source, "/fake/source");
    // Both operations reached their wait on that thread rather than polling.
    assert_eq!(waits, 2);
}

#[test]
fn a_blocking_read_of_a_missing_file_is_an_error_not_a_hang() {
    reset_fake(FakeState {
        launch_invalid: true,
        ..Default::default()
    });
    let mount = Vfs::mount("/data").expect("mount");
    // SAFETY: the mount outlives the view.
    let view = unsafe { mount.worker_view() };
    assert_eq!(view.read("missing").unwrap_err(), FlError::GenericError);
    assert_eq!(
        view.listing("missing", 1).unwrap_err(),
        FlError::GenericError
    );
    assert_eq!(
        view.read_prefix("missing", 16).unwrap_err(),
        FlError::GenericError
    );
    // Nothing was launched, so nothing was waited on or closed.
    assert_eq!(with_fake(|f| f.wait_count), 0);
    assert_eq!(with_fake(|f| f.close_count), 0);
}

#[test]
fn read_prefix_bounds_the_read_and_keeps_a_short_file_whole() {
    reset_fake(FakeState {
        prefix: b"0123456789".to_vec(),
        ..Default::default()
    });
    let mount = Vfs::mount("/data").expect("mount");
    // SAFETY: the mount outlives the view.
    let view = unsafe { mount.worker_view() };

    // Asked for less than the file holds: exactly that many bytes come back.
    assert_eq!(view.read_prefix("blob.bin", 4).expect("prefix"), b"0123");
    assert_eq!(with_fake(|f| f.prefix_requested), 4);
    // Asked for more: the short read is the whole file, not an error.
    assert_eq!(
        view.read_prefix("blob.bin", 64).expect("prefix"),
        b"0123456789"
    );
    // Both prefix reads open a file handle and chain a read onto it, so each
    // closes two tickets.
    assert_eq!(with_fake(|f| f.close_count), 4);
    // A zero-byte request never reaches the VFS at all.
    assert_eq!(view.read_prefix("blob.bin", 0).expect("prefix"), b"");
    assert_eq!(with_fake(|f| f.close_count), 4);
}

#[test]
fn read_chunks_walks_the_file_in_order_and_closes_every_ticket() {
    reset_fake(FakeState {
        prefix: b"0123456789".to_vec(),
        ..Default::default()
    });
    let mount = Vfs::mount("/data").expect("mount");
    // SAFETY: the mount outlives the view.
    let view = unsafe { mount.worker_view() };

    let mut pieces: Vec<Vec<u8>> = Vec::new();
    view.read_chunks("blob.bin", 4, |bytes| pieces.push(bytes.to_vec()))
        .expect("chunks");
    // Full chunks, then the short tail; the empty read that ends the file is not handed on.
    assert_eq!(pieces, [b"0123".to_vec(), b"4567".to_vec(), b"89".to_vec()]);
    // Four reads (three with bytes, one at the end) and the file handle itself.
    assert_eq!(with_fake(|f| f.close_count), 5);
    // A zero chunk size is refused before anything is opened.
    assert!(view.read_chunks("blob.bin", 0, |_| {}).is_err());
    assert_eq!(with_fake(|f| f.close_count), 5);
}
