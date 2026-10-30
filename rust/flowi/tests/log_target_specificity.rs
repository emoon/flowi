//! When two set_target_level floors both match a record's target, the more specific one
//! wins - whichever order they were installed in - and re-setting a prefix replaces its
//! level rather than leaving the old one behind.

use flowi::set_target_level;

mod log_common;

#[test]
fn longest_matching_prefix_wins_regardless_of_order() {
    let sink = log_common::TempLogSink::open("flowi_log_specific");

    // Specific first, then broader.
    set_target_level("quiet_dep::loud", log::LevelFilter::Debug);
    set_target_level("quiet_dep", log::LevelFilter::Warn);
    log::debug!(target: "quiet_dep::loud", "specific floor installed first");
    log::debug!(target: "quiet_dep::other", "only the broad floor covers this");

    // Broader first, then specific.
    set_target_level("hushed_dep", log::LevelFilter::Warn);
    set_target_level("hushed_dep::loud", log::LevelFilter::Debug);
    log::debug!(target: "hushed_dep::loud", "specific floor installed second");
    log::debug!(target: "hushed_dep::other", "again just the broad floor");

    // Re-setting a prefix replaces its level instead of stacking behind the old entry.
    set_target_level("hushed_dep::loud", log::LevelFilter::Warn);
    log::debug!(target: "hushed_dep::loud", "silenced by the replacement floor");

    let contents = sink.contents();

    for kept in [
        "specific floor installed first",
        "specific floor installed second",
    ] {
        assert!(
            contents.contains(kept),
            "a broader floor overrode the more specific one: {contents}"
        );
    }
    for dropped in [
        "only the broad floor covers this",
        "again just the broad floor",
        "silenced by the replacement floor",
    ] {
        assert!(
            !contents.contains(dropped),
            "floored record {dropped:?} reached the sink: {contents}"
        );
    }
}
