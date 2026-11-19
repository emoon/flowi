//! What a watcher reports: whether a start succeeds, and the path and kind of the changes it hands back.
//!
//! Starting a watcher does not bootstrap the foundation, so each test brings it up first.

#![forbid(unsafe_code)]

use flowi::{FileChangeType, FileWatcher, WatchConfig};
use std::path::PathBuf;
use std::time::{Duration, Instant};

/// Generous for slow CI hardware; a passing run returns as soon as the change lands.
const DEADLINE: Duration = Duration::from_secs(5);

fn scratch_dir(name: &str) -> PathBuf {
    std::env::temp_dir().join(format!("flowi_watcher_{name}_{}", std::process::id()))
}

/// Scans one batch, appending every path to `seen`; the kind and directory flag of the last change named
/// `file_name`, if any.
fn scan_batch(
    batch: &flowi::FileChanges,
    file_name: &str,
    seen: &mut Vec<String>,
) -> Option<(FileChangeType, bool)> {
    let mut found = None;
    for change in batch.iter() {
        seen.push(change.path().to_owned());
        if change.path().ends_with(file_name) {
            found = Some((change.change_types(), change.is_directory()));
        }
    }
    found
}

/// Collects batches until a change named `file_name` shows up (the platform may split one write across
/// several); the paths seen so far when the deadline passes.
fn await_change(watcher: &FileWatcher, file_name: &str) -> Result<(FileChangeType, bool), Vec<String>> {
    let started = Instant::now();
    let mut seen = Vec::new();
    while started.elapsed() < DEADLINE {
        if !watcher.has_changes() {
            std::thread::sleep(Duration::from_millis(5));
            continue;
        }
        let batch = watcher.take_changes();
        if let Some(found) = scan_batch(&batch, file_name, &mut seen) {
            return Ok(found);
        }
    }
    Err(seen)
}

#[test]
fn starting_on_a_missing_directory_returns_none() {
    let _arena = flowi::init(1);

    let missing = scratch_dir("missing");
    let _ = std::fs::remove_dir_all(&missing);

    let watcher = FileWatcher::start(
        missing.to_str().expect("a UTF-8 temp path"),
        WatchConfig::default(),
    );
    assert!(
        watcher.is_none(),
        "a directory that does not exist cannot be watched"
    );
}

#[test]
fn a_created_file_is_reported_with_its_path() {
    let _arena = flowi::init(1);

    let dir = scratch_dir("created");
    let _ = std::fs::remove_dir_all(&dir);
    std::fs::create_dir_all(&dir).expect("a writable temp directory");

    let watcher = FileWatcher::start(
        dir.to_str().expect("a UTF-8 temp path"),
        WatchConfig::default(),
    )
    .expect("a watch on an existing directory should start");
    assert!(
        !watcher.has_changes(),
        "nothing has happened in the directory yet"
    );

    std::fs::write(dir.join("created.txt"), b"payload").expect("a writable temp directory");

    let (change_types, is_directory) = await_change(&watcher, "created.txt")
        .unwrap_or_else(|seen| panic!("no change named created.txt arrived; saw {seen:?}"));
    assert!(
        change_types.contains(FileChangeType::CREATED),
        "got {change_types:?}"
    );
    assert!(!is_directory, "created.txt is a file");

    drop(watcher);
    let _ = std::fs::remove_dir_all(&dir);
}
