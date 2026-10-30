//! The log-crate bridge onto flowi core's channel logging, plus the RAII file sink.
//!
//! Rust code logs with the log crate - log::info!("..."), log::warn!(target: "AUDIO", "...").
//! Each record's target names a flowi log channel 1:1; a record without an explicit target
//! goes to "CORE", the default channel. Rust and native records share one sink with
//! identical formatting, and per-channel enable/level settings filter both alike.
//!
//! [`install_log_bridge`] connects the log crate to that sink. [`init`](crate::init) and
//! every other flowi entry point install it; a dlopen'd Rust cdylib has its own log global
//! and must install it for itself.
//!
//! Every record reaches the sink by default, third-party crates included.
//! [`set_target_level`] sets a per-target floor that drops a record before it is formatted.

use crate::sys;
use std::collections::BTreeMap;
use std::ffi::CString;
use std::ptr;
use std::sync::{Mutex, Once};

/// The channel a record without an explicit target lands on.
const DEFAULT_CHANNEL: &str = "CORE";

/// Channel name -> id cache. Keys are leaked (see [`channel_id`]), one per distinct target.
static CHANNELS: Mutex<BTreeMap<&'static str, u32>> = Mutex::new(BTreeMap::new());

/// Target-prefix floors, keyed by prefix.
static TARGET_LEVELS: Mutex<BTreeMap<&'static str, log::LevelFilter>> = Mutex::new(BTreeMap::new());

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

/// The id name is registered under, registering it on first use. The name is leaked:
/// the native channel table keeps the pointer for the rest of the process.
fn channel_id(name: &str) -> u32 {
    let mut channels = CHANNELS.lock().unwrap_or_else(|e| e.into_inner());
    if let Some(id) = channels.get(name) {
        return *id;
    }

    let name: &'static str = Box::leak(name.to_owned().into_boxed_str());
    // SAFETY: name is 'static, so it outlives the channel-table entry that stores it.
    let id = unsafe { sys::fl_log_register_channel(sys::RawStr::from_static(name)) };
    channels.insert(name, id);
    id
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

/// Map a log level onto flowi's. LogLevel::Fatal has no log counterpart.
fn to_fl_level(level: log::Level) -> sys::LogLevel {
    match level {
        log::Level::Error => sys::LogLevel::Error,
        log::Level::Warn => sys::LogLevel::Warning,
        log::Level::Info => sys::LogLevel::Info,
        log::Level::Debug => sys::LogLevel::Debug,
        log::Level::Trace => sys::LogLevel::Trace,
    }
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

/// The log::Log impl: render Rust-side, then emit on the record's channel.
struct ChannelLogger;

impl log::Log for ChannelLogger {
    /// Applies only the [`set_target_level`] floors; channel level and enable flag are applied
    /// by the native emitter.
    fn enabled(&self, metadata: &log::Metadata<'_>) -> bool {
        let floors = TARGET_LEVELS.lock().unwrap_or_else(|e| e.into_inner());
        // The longest matching prefix wins.
        match floors
            .iter()
            .filter(|(prefix, _)| metadata.target().starts_with(*prefix))
            .max_by_key(|(prefix, _)| prefix.len())
        {
            Some((_, level)) => metadata.level() <= *level,
            None => true,
        }
    }

    fn log(&self, record: &log::Record<'_>) {
        // A direct log::logger().log(...) call bypasses the macros' enabled check.
        if !self.enabled(record.metadata()) {
            return;
        }

        // A record with no explicit target carries the module path as its target.
        let target = record.target();
        let channel = if Some(target) == record.module_path() {
            DEFAULT_CHANNEL
        } else {
            target
        };

        let message = std::fmt::format(*record.args());
        // A file name with an interior NUL is dropped from the location.
        let file = record.file().and_then(|path| CString::new(path).ok());

        // SAFETY: the channel id is one flowi core handed back (or a benign invalid id it
        // rejects), file is either null or NUL-terminated, and both it and the borrowed
        // message outlive this synchronous call.
        unsafe {
            sys::fl_log_c_message_formatted(
                channel_id(channel),
                to_fl_level(record.level()),
                file.as_ref().map_or(ptr::null(), |path| path.as_ptr()),
                record.line().unwrap_or(0) as i32,
                sys::RawStr::borrow(&message),
            );
        }
    }

    fn flush(&self) {
        // SAFETY: a plain global call with no arguments to validate.
        unsafe { sys::fl_log_flush() }
    }
}

static LOGGER: ChannelLogger = ChannelLogger;

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

/// Route this binary's log records at flowi core's channel logging.
///
/// Until it runs, log sits at [`LevelFilter::Off`](log::LevelFilter::Off) and records are
/// discarded. Every flowi entry point installs the bridge as the foundation comes up; a host
/// that logs before it first reaches flowi must call this first. A dlopen'd Rust cdylib has
/// its own log global and must always call it.
///
/// Idempotent. If another logger is already installed it is kept, and flowi's records go
/// there instead.
pub fn install_log_bridge() {
    static INSTALL: Once = Once::new();
    INSTALL.call_once(|| {
        if log::set_logger(&LOGGER).is_ok() {
            // Channel filtering happens natively, so pass every level through.
            log::set_max_level(log::LevelFilter::Trace);
        }
    });
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

/// Drop records whose target starts with prefix below level.
///
/// A log target defaults to the emitting module's path, so "ureq" covers a whole
/// dependency and "ureq::tls" one part of it.
///
/// When two floors both match a target, the longer prefix wins regardless of the order they
/// were set, so ("ureq", Warn) plus ("ureq::tls", Debug) keeps the TLS layer at Debug.
/// Setting the same prefix again replaces its level. Records are dropped before formatting.
pub fn set_target_level(prefix: &'static str, level: log::LevelFilter) {
    let mut floors = TARGET_LEVELS.lock().unwrap_or_else(|e| e.into_inner());
    floors.insert(prefix, level);
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

/// The on-disk log sink, open for as long as this guard lives.
///
/// [`open`](LogFile::open) starts writing every emitted record, native and Rust alike, to a
/// file under base_dir; dropping the guard flushes and closes it. The sink is process-wide:
/// calling `open` again while a guard is alive closes the earlier file and switches the sink
/// to the new one, and dropping any guard closes whichever file is currently open. Hold a
/// single guard for the lifetime of the program.
#[derive(Debug)]
pub struct LogFile {
    _private: (),
}

impl LogFile {
    /// Open the on-disk sink under base_dir, or None when the file could not be created.
    ///
    /// An empty base_dir selects flowi's default data directory. On failure logging stays
    /// console-only.
    pub fn open(base_dir: &str) -> Option<LogFile> {
        // SAFETY: the borrowed base_dir outlives this synchronous call, which copies it.
        let opened = unsafe { sys::fl_log_file_init(sys::RawStr::borrow(base_dir)) };
        opened.then_some(LogFile { _private: () })
    }

    /// The path of the file being written.
    pub fn path(&self) -> String {
        // SAFETY: the returned string points into the log arena, which outlives this call; the
        // conversion copies out of it.
        unsafe { sys::fl_log_file_get_path() }.into_string()
    }

    /// Flush buffered output to disk without closing the sink.
    #[inline]
    pub fn flush(&self) {
        // SAFETY: a plain global call with no arguments to validate.
        unsafe { sys::fl_log_flush() }
    }
}

impl Drop for LogFile {
    fn drop(&mut self) {
        // SAFETY: a plain global call, valid with no file open.
        unsafe { sys::fl_log_file_shutdown() }
    }
}
