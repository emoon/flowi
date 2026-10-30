//! Only one Application may be alive in the process, and the entry points that touch the frame
//! context from outside a frame only run on the thread that owns it.

use std::panic::{catch_unwind, AssertUnwindSafe};
use std::thread;

use flowi::{
    action_pressed, col, consume_nav_down, consume_nav_left, consume_nav_right, consume_nav_up,
    element_state, element_state_id, fixed, hot_element, nav_down_pressed, nav_frame,
    nav_up_pressed, set_focus, set_focus_id, Action, Application, Button, Color, Cursor,
    DynamicImage, DynamicImageBuffer, EmbeddedSettings, ImageOpts, List, ListConfig, ListOpts,
    Menu, Popup, PopupConfig, Slider, SliderConfig, TextInput, TextInputConfig, Theme,
    WidgetConfig, Window, WindowSettings,
};

/// Whether f panics with a message containing needle.
fn panics<R>(needle: &str, f: impl FnOnce() -> R) -> bool {
    catch_unwind(AssertUnwindSafe(f)).is_err_and(|payload| {
        let message = payload
            .downcast_ref::<String>()
            .map(String::as_str)
            .or_else(|| payload.downcast_ref::<&str>().copied());
        message.is_some_and(|m| m.contains(needle))
    })
}

extern "C" {
    /// Closes the innermost popup scope on the calling thread's UI context.
    fn fl_popup_end();
}

const ALIVE: &str = "already alive";
const THREAD: &str = "must run on the thread that owns the live Application";

#[test]
fn one_application_per_process_and_its_thread_owns_the_frame() {
    let mut first = Application::new(200, 150, true);
    let palette = Theme::colors();

    assert!(
        panics(ALIVE, || Application::new(100, 100, true)),
        "a second windowed Application must be refused while one is alive"
    );
    assert!(
        panics(ALIVE, || Application::embedded(EmbeddedSettings::new(
            64, 64
        ))),
        "an embedded Application must be refused while one is alive"
    );
    thread::scope(|scope| {
        let other = scope.spawn(|| panics(ALIVE, || Application::new(100, 100, true)));
        assert!(
            other.join().unwrap(),
            "a second Application on another thread must be refused"
        );

        let off_thread_input = scope.spawn(|| {
            let mut input = TextInput::new();
            panics(THREAD, || input.show_default("field"))
        });
        assert!(
            off_thread_input.join().unwrap(),
            "TextInput::show must refuse to run off the application's thread"
        );

        // Outside a frame, a frame-string copy asserts in C, so reaching one from this thread
        // before the rejection would abort the process.
        let off_thread_configured = scope.spawn(|| {
            let mut input = TextInput::new();
            let config = TextInputConfig {
                allowed_chars: "0123456789",
                placeholder: "digits",
                widget: WidgetConfig {
                    id_override: "digits_field",
                    ..Default::default()
                },
                ..Default::default()
            };
            panics(THREAD, || input.show("field", config))
        });
        assert!(
            off_thread_configured.join().unwrap(),
            "a configured TextInput::show must be refused before it touches the frame"
        );

        let off_thread_reader = scope.spawn(|| panics(THREAD, Theme::colors));
        assert!(
            off_thread_reader.join().unwrap(),
            "Theme::colors must refuse to run off the application's thread"
        );

        // A generated forwarder.
        let off_thread_button =
            scope.spawn(|| panics(THREAD, || Button::new("off thread", Default::default())));
        assert!(
            off_thread_button.join().unwrap(),
            "a generated widget forwarder must refuse to run off the application's thread"
        );

        let off_thread_cursor = scope.spawn(|| panics(THREAD, || Cursor::set_visible(false)));
        assert!(
            off_thread_cursor.join().unwrap(),
            "a generated Cursor forwarder must refuse to run off the application's thread"
        );

        let off_thread_pop = scope.spawn(|| panics(THREAD, Theme::pop));
        assert!(
            off_thread_pop.join().unwrap(),
            "a generated Theme forwarder must refuse to run off the application's thread"
        );

        let off_thread_theme = scope.spawn(|| panics(THREAD, || Theme::update(&palette)));
        assert!(
            off_thread_theme.join().unwrap(),
            "Theme::update must refuse to run off the application's thread"
        );
    });

    thread::scope(|scope| {
        let off_thread = |name: &str, f: fn() -> bool| {
            let refused = scope.spawn(f).join().unwrap();
            assert!(
                refused,
                "{name} must refuse to run off the application's thread"
            );
        };

        off_thread("Slider::float", || {
            let mut value = 0.5;
            panics(THREAD, || {
                Slider::float("v", &mut value, 0.0, 1.0, SliderConfig::default())
            })
        });
        off_thread("Slider::int", || {
            let mut value = 5;
            panics(THREAD, || {
                Slider::int("v", &mut value, 0, 10, SliderConfig::default())
            })
        });
        off_thread("List::begin", || {
            panics(THREAD, || {
                List::begin("list", &[1, 2], None, ListConfig::default())
            })
        });
        off_thread("List::end", || panics(THREAD, List::end));
        off_thread("List::show", || {
            panics(THREAD, || {
                List::show("list", &[], None, ListOpts::default())
            })
        });
        off_thread("DynamicImage::new", || {
            let buffer = DynamicImageBuffer::new(2, 2);
            let opts = ImageOpts {
                color: Color {
                    r: 255,
                    g: 255,
                    b: 255,
                    a: 255,
                },
                fill_container: false,
            };
            // SAFETY: the call is refused before it reaches the renderer, which never reads the buffer.
            panics(THREAD, || unsafe { DynamicImage::new(&buffer, opts) })
        });
        off_thread("Menu::region_begin", || {
            panics(THREAD, || Menu::region_begin("region", "menu"))
        });
        off_thread("the element state queries", || {
            panics(THREAD, || element_state("el"))
                && panics(THREAD, || element_state_id(flowi::element_id("el", 0)))
                && panics(THREAD, hot_element)
        });
        off_thread("the nav queries", || {
            panics(THREAD, nav_frame)
                && panics(THREAD, || action_pressed(Action::None))
                && panics(THREAD, nav_up_pressed)
                && panics(THREAD, nav_down_pressed)
        });
        off_thread("the nav consumers", || {
            panics(THREAD, consume_nav_up)
                && panics(THREAD, consume_nav_down)
                && panics(THREAD, consume_nav_left)
                && panics(THREAD, consume_nav_right)
        });
        off_thread("the focus setters", || {
            panics(THREAD, || set_focus("el"))
                && panics(THREAD, || set_focus_id(flowi::element_id("el", 0)))
        });
    });

    // A scope token dropped off the thread that opened it is refused before it closes the C scope,
    // which the test then closes itself.
    let mut scope_refused = false;
    assert!(first.tick(&mut scope_refused, |refused| {
        col! { "anchor", width: fixed!(20.0), height: fixed!(20.0) {}}
        let token = Popup::begin("anchor", PopupConfig::default());
        *refused = thread::scope(|scope| {
            scope
                .spawn(move || panics(THREAD, move || drop(token)))
                .join()
                .unwrap()
        });
        // SAFETY: closes the scope token opened above, whose refused drop left it open.
        unsafe { fl_popup_end() };
    }));
    assert!(
        scope_refused,
        "a PopupScope must refuse to drop off the application's thread"
    );

    for red in [0x12, 0x34] {
        let mut changed = Theme::colors();
        changed.text.primary.r = red;
        Theme::update(&changed);
        assert_eq!(
            Theme::colors().text.primary.r,
            red,
            "the palette passed to update should be the one C reads"
        );
    }

    let window = first.open_window(WindowSettings::new("second", 100, 100), (), |_| {});
    first.close_window(window);

    drop(first);

    // The slot is free again once the application is gone.
    let mut second = Application::new(200, 150, true);
    let stale: Window = second.open_window(WindowSettings::new("kept", 100, 100), (), |_| {});
    drop(second);

    let mut third = Application::new(200, 150, true);
    assert!(
        panics("different Application", || third.close_window(stale)),
        "a Window from a dropped Application must not be closed against a new one"
    );
}
