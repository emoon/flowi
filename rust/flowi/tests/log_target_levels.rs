//! set_target_level floors drop records below their level.

use flowi::set_target_level;

mod log_common;

#[test]
fn target_floors_drop_records_below_their_level() {
    let sink = log_common::TempLogSink::open("flowi_log_targets");

    // A prefix covers everything under it.
    set_target_level("noisy_dep", log::LevelFilter::Warn);

    log::debug!(target: "noisy_dep::inner", "chatty detail nobody asked for");
    log::info!(target: "noisy_dep", "routine progress");
    log::warn!(target: "noisy_dep", "something actually went wrong");
    log::debug!(target: "QUIET", "kept because nothing floors this target");

    let contents = sink.contents();

    for dropped in ["chatty detail nobody asked for", "routine progress"] {
        assert!(
            !contents.contains(dropped),
            "floored record {dropped:?} reached the sink: {contents}"
        );
    }
    for kept in [
        "something actually went wrong",
        "kept because nothing floors this target",
    ] {
        assert!(
            contents.contains(kept),
            "record {kept:?} was dropped by an unrelated floor: {contents}"
        );
    }
}
