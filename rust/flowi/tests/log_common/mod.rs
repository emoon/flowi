//! Helpers shared by the log integration tests.

#![allow(dead_code)]

use std::path::PathBuf;

use flowi::{install_log_bridge, LogFile};

/// The log bridge installed and writing to a file sink in a per-process temp directory, which is
/// removed on drop.
pub struct TempLogSink {
    dir: PathBuf,
    file: Option<LogFile>,
}

impl TempLogSink {
    /// Creates `<temp>/<name>_<pid>`, installs the bridge and opens the file sink there.
    pub fn open(name: &str) -> TempLogSink {
        let dir = std::env::temp_dir().join(format!("{name}_{}", std::process::id()));
        std::fs::create_dir_all(&dir).expect("create temp log dir");
        let dir_str = dir.to_str().expect("utf-8 temp path");

        install_log_bridge();
        let file = LogFile::open(dir_str).expect("file logging failed to initialize");
        TempLogSink {
            dir,
            file: Some(file),
        }
    }

    pub fn path(&self) -> String {
        self.file().path()
    }

    /// Flushes the sink and reads back everything written so far.
    pub fn contents(&self) -> String {
        let file = self.file();
        file.flush();
        std::fs::read_to_string(file.path()).expect("read log file")
    }

    fn file(&self) -> &LogFile {
        self.file.as_ref().expect("sink is open until drop")
    }
}

impl Drop for TempLogSink {
    fn drop(&mut self) {
        drop(self.file.take());
        let _ = std::fs::remove_dir_all(&self.dir);
    }
}
