//! A panic inside a frame callback aborts the process instead of unwinding across the C
//! boundary. The parent re-runs this test binary with an env gate; the child ticks a
//! panicking closure and aborts; the parent asserts the child died with SIGABRT.

use std::env;
use std::process::Command;

const CHILD_GATE: &str = "FL_UI_PANIC_ABORT_CHILD";

#[test]
fn panicking_callback_aborts() {
    if env::var_os(CHILD_GATE).is_some() {
        child_panics_in_frame();
        return; // unreachable in practice - the line above aborts the process
    }

    let exe = env::current_exe().expect("locate test binary");
    // Captured so a swallowed panic surfaces its message.
    let output = Command::new(exe)
        .env(CHILD_GATE, "1")
        .args(["--exact", "panicking_callback_aborts"])
        .output()
        .expect("spawn self-exec child");
    let status = output.status;
    let stderr = String::from_utf8_lossy(&output.stderr);

    #[cfg(unix)]
    {
        use std::os::unix::process::ExitStatusExt;
        // SIGABRT == 6.
        assert_eq!(
            status.signal(),
            Some(6),
            "child should die with SIGABRT; got {status:?}\n--- child stderr ---\n{stderr}"
        );
    }
    #[cfg(not(unix))]
    assert!(
        !status.success(),
        "child should not exit cleanly; got {status:?}\n--- child stderr ---\n{stderr}"
    );
}

fn child_panics_in_frame() {
    let mut app = flowi::Application::new(8, 8, true);
    let mut state = ();
    app.tick(&mut state, |_: &mut ()| {
        panic!("intentional panic in frame callback")
    });
    // Reaching here means the panic was swallowed; exit without aborting so the
    // parent's SIGABRT assertion fails.
    eprintln!("BUG: tick returned after a panicking callback");
    std::process::exit(0);
}
