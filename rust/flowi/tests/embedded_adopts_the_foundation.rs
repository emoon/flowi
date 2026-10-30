//! An embedded host brings the foundation up itself, then hands it to an Application, which
//! adopts the running foundation rather than starting a second one.

#![forbid(unsafe_code)]

use flowi::{Application, EmbeddedSettings, Jobs};

const BOOTSTRAP_JOB_THREADS: i32 = 4;

#[test]
fn an_embedded_application_adopts_an_already_running_foundation() {
    let bootstrap = flowi::init(BOOTSTRAP_JOB_THREADS);
    assert_eq!(
        Jobs::num_threads(),
        BOOTSTRAP_JOB_THREADS,
        "the first bring-up decides the worker count"
    );

    // Differs from BOOTSTRAP_JOB_THREADS, so a re-run bring-up would show in the count below.
    let app = Application::embedded(EmbeddedSettings::new(32, 32).with_job_threads(1))
        .expect("embedded flowi");

    assert_eq!(
        app.arena().as_raw(),
        bootstrap.as_raw(),
        "an application must adopt the running foundation's arena, not strand it behind a second one"
    );
    assert_eq!(
        Jobs::num_threads(),
        BOOTSTRAP_JOB_THREADS,
        "adopting leaves the worker count the first bring-up chose"
    );
}
