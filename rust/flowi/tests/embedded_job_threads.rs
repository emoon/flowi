//! The job_threads setting is read only by the entry point that brings the foundation up, so
//! this test must make the process's first flowi call.

#![forbid(unsafe_code)]

use flowi::{Application, EmbeddedSettings, Jobs};

/// Differs from the default of one worker.
const JOB_THREADS: i32 = 2;

#[test]
fn with_job_threads_sizes_the_job_system() {
    let _app = Application::embedded(EmbeddedSettings::new(32, 32).with_job_threads(JOB_THREADS))
        .expect("embedded flowi");

    assert_eq!(
        Jobs::num_threads(),
        JOB_THREADS,
        "the requested worker count should have sized the job system"
    );
}
