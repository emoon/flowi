//! loc_fmt! / loc_plural! end to end against the example string tables: a mixed-type argument
//! list substitutes into the default language's text, and a count picks the plural variant.

use flowi::{loc_fmt, loc_plural, Application, Localization, Text, Vfs};

const TABLE_DIR: &str = concat!(
    env!("CARGO_MANIFEST_DIR"),
    "/../../examples/data/localization"
);

// The table read is asynchronous and no frame waits on it, so the first frames render the keys
// themselves.
const MAX_TICKS: usize = 600;

#[derive(Default)]
struct Probe {
    greeting: String,
    mixed: String,
    one: String,
    many: String,
    plural_args: String,
    no_args: String,
}

#[test]
fn the_macros_format_and_pluralize_through_the_active_table() {
    let mut app = Application::new(64, 64, true);
    // Leaked: the localization state reads through this mount until the app tears down, and the
    // app's teardown is what frees the VFS the mount belongs to.
    let mount = Vfs::mount(TABLE_DIR)
        .expect("mounting the example localization data")
        .leak();

    // SAFETY: mount outlives the app; the manifest path is read during the call only.
    unsafe {
        Localization::init(mount, "manifest.json", false);
    }

    let mut probe = Probe::default();
    let mut ticks = 0;
    while ticks < MAX_TICKS {
        ticks += 1;
        assert!(
            app.tick(&mut probe, |p: &mut Probe| {
                let name = String::from("Ada");
                p.greeting = loc_fmt!("SID_greeting", &name);
                p.mixed = loc_fmt!(
                    "SID_fmt_eight",
                    -1i8,
                    2u16,
                    3i64,
                    4.5f32,
                    5.25f64,
                    "six",
                    7usize,
                    8u64
                );
                p.one = loc_plural!("SID_file_count", 1);
                p.many = loc_plural!("SID_file_count", 3usize);
                p.plural_args = loc_plural!("SID_plural_args", 3, "Docs");
                p.no_args = loc_fmt!("SID_settings_title");

                // Inline as a widget text argument, the way a call site actually spells it.
                Text::emit(loc_plural!("SID_file_count", 2));
            }),
            "the headless app should keep running"
        );

        if probe.greeting != "SID_greeting" {
            break;
        }
    }

    assert_eq!(
        probe.greeting, "Hello, Ada!",
        "the table lands within a bounded number of frames and a borrowed String substitutes"
    );
    assert_eq!(
        probe.mixed, "-1 2 3 4.5 5.25 six 7 8",
        "one argument list mixes signed, unsigned, float and string arguments"
    );
    assert_eq!(
        probe.one, "1 file",
        "a count of 1 selects the `one` variant"
    );
    assert_eq!(
        probe.many, "3 files",
        "a count of 3 selects the `other` variant"
    );
    assert_eq!(
        probe.plural_args, "3 files in Docs",
        "the count is the first slot and the arguments after it shift by one"
    );
    assert_eq!(
        probe.no_args, "Settings",
        "an empty argument list is a valid expansion"
    );
}
