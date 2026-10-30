//! flowi core keeps a channel-name pointer in its table for the rest of the process, so
//! the bridge owns the names it registers rather than borrowing the record's.

mod log_common;

#[test]
fn channel_names_outlive_the_record_that_named_them() {
    let sink = log_common::TempLogSink::open("flowi_log_channels");

    {
        let transient = String::from("TRANSIENT");
        log::info!(target: transient.as_str(), "registered from a temporary");
    }
    // A second record on the same channel resolves through the cache.
    log::info!(target: "TRANSIENT", "still the same channel");

    let contents = sink.contents();

    assert_eq!(
        contents.matches("[TRANSIENT").count(),
        2,
        "channel name did not survive its record: {contents}"
    );
}
