//! The log bridge routes levels, targets and call sites through the file sink.

mod log_common;

#[test]
fn bridge_routes_levels_and_targets_through_the_file_sink() {
    let sink = log_common::TempLogSink::open("flowi_log_bridge");

    // The default channel level is Trace, so all five emit; log has no Fatal, so Error is
    // the most severe a Rust record reaches.
    log::trace!("trace v={}", 1);
    log::debug!("plain message");
    log::info!("hello {}", 42);
    log::warn!("warn {}%", 90);
    log::error!("code={} name={}", 7, "boom");

    // Named channels come from target:, one channel per target.
    log::info!(target: "AUDIO", "mixer running at {} Hz", 48000);
    log::error!(target: "PLUGIN", "plugin {} failed", "hello");

    assert!(
        !sink.path().is_empty(),
        "no active log file path after open"
    );
    let contents = sink.contents();

    for expected in [
        "trace v=1",
        "plain message",
        "hello 42",
        "warn 90%",
        "code=7 name=boom",
        "mixer running at 48000 Hz",
        "plugin hello failed",
    ] {
        assert!(
            contents.contains(expected),
            "message {expected:?} not rendered: {contents}"
        );
    }

    // The C formatter brackets the channel name, so the untargeted records land on CORE
    // and the targeted ones on their own channels.
    for channel in ["[CORE", "[AUDIO", "[PLUGIN"] {
        assert!(
            contents.contains(channel),
            "channel {channel:?} missing from output: {contents}"
        );
    }

    // Levels map onto flowi's names - Warn onto WARNING, the one that is not a rename.
    for level in ["[TRACE", "[DEBUG", "[INFO", "[WARNING", "[ERROR"] {
        assert!(
            contents.contains(level),
            "level {level:?} missing from output: {contents}"
        );
    }

    // The bridge captures the call site, so records carry this file's name.
    assert!(
        contents.contains("log_bridge.rs:"),
        "source location missing from output: {contents}"
    );
}
