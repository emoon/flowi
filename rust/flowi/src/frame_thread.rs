//! The one thread that drives the C toolkit's global frame context.
//!
//! The C toolkit keeps a single process-wide UI context, so two applications, or an
//! application and a widget call from another thread, would race it. A live
//! [`Application`](crate::Application) holds a [`FrameClaim`]: no second application can be
//! created while it is held, and the frame entry points refuse to run on any other thread. Each
//! holds the claim lock for its whole C call, so an application cannot come up under them.
//!
//! The generated forwarders of every `#[frame_context]` api block, and the generated scope tokens'
//! drops, open with [`frame_thread_guard`]; the hand-written entry points that reach the context
//! ([`TextInput::show`], [`Theme::update`], [`Theme::colors`], `Slider`, `List`, `DynamicImage::new`,
//! `Menu::region_begin` and the free element-state, nav and focus functions) do the same. Each runs
//! the caller's own conversions first, then takes the guard, then converts into the C types.

use std::sync::{Mutex, MutexGuard, PoisonError};
use std::thread::{self, ThreadId};

use crate::sys;

use crate::generated::widget_config::TextInputConfig;
use crate::{TextInput, Theme};

/// The thread of the live application, or None when there is none.
static FRAME_THREAD: Mutex<Option<ThreadId>> = Mutex::new(None);

/// The palette storage C retains a pointer into after [`Theme::update`] returns.
static PALETTE: Mutex<Option<Box<sys::ThemeColors>>> = Mutex::new(None);

fn locked<T>(mutex: &Mutex<T>) -> MutexGuard<'_, T> {
    mutex.lock().unwrap_or_else(PoisonError::into_inner)
}

/// Proof that the calling thread's application is the only one alive. Released on drop.
pub(crate) struct FrameClaim(());

impl FrameClaim {
    /// Claim the frame context for the calling thread.
    ///
    /// # Panics
    /// Panics if an application is already alive, on this thread or another.
    pub(crate) fn acquire() -> FrameClaim {
        let mut owner = locked(&FRAME_THREAD);
        let taken = owner.is_some();
        if !taken {
            *owner = Some(thread::current().id());
        }
        drop(owner);
        assert!(
            !taken,
            "flowi: an Application is already alive - the C toolkit drives one global frame \
             context, so drop the existing Application before creating another"
        );
        FrameClaim(())
    }
}

impl Drop for FrameClaim {
    fn drop(&mut self) {
        *locked(&FRAME_THREAD) = None;
    }
}

/// Panic unless the calling thread may drive the frame context: always when no application
/// is alive, otherwise only the application's own thread. The returned guard keeps the
/// answer true - no application can be created, or dropped - until the caller is done.
pub(crate) fn frame_thread_guard(entry_point: &str) -> MutexGuard<'static, Option<ThreadId>> {
    let owner = locked(&FRAME_THREAD);
    let wrong_thread = owner.is_some_and(|id| id != thread::current().id());
    if wrong_thread {
        drop(owner);
        panic!("flowi: {entry_point} must run on the thread that owns the live Application");
    }
    owner
}

impl TextInput {
    /// Emit the text-input widget for this handle labelled label and return its interaction Signal.
    /// The Signal's text_changed, submitted (Enter) and cancelled (Escape) are true only on the frame they
    /// happen. Call inside a frame callback.
    ///
    /// # Panics
    /// Panics when called off the thread that owns the live [`Application`](crate::Application).
    #[inline]
    pub fn show_default(&mut self, label: impl AsRef<str>) -> sys::Signal {
        let label = label.as_ref();
        let _frame = frame_thread_guard("TextInput::show_default");
        // SAFETY: self.handle is the live handle fl_text_input_create returned; fl_frame_str copies
        // label's bytes into the frame arena, so the string C receives outlives the call; and
        // _frame keeps the frame context to its owning thread for the call.
        unsafe {
            sys::fl_text_input_show(
                sys::fl_frame_str(label.as_ptr().cast(), label.len()),
                self.handle,
            )
        }
    }

    /// Emit the text-input widget with per-call configuration. config controls placeholder text, width,
    /// maximum length, and shared focus behavior. A zero configuration preserves the default behavior.
    ///
    /// # Panics
    /// Panics when called off the thread that owns the live [`Application`](crate::Application).
    #[inline]
    pub fn show(&mut self, label: impl AsRef<str>, config: TextInputConfig<'_>) -> sys::Signal {
        let label = label.as_ref();
        let _frame = frame_thread_guard("TextInput::show");
        let config = config.to_sys();
        // SAFETY: as show_default; config borrows strings that outlive the call.
        unsafe {
            sys::fl_text_input_show_impl(
                sys::fl_frame_str(label.as_ptr().cast(), label.len()),
                self.handle,
                config,
            )
        }
    }
}

impl Theme {
    /// Inject the resolved theme palette flowi reads when painting widgets, replacing the active base theme.
    /// colors is copied, so a reference to a temporary is fine; each call replaces the previously stored
    /// palette, which stays in effect for every subsequent frame.
    ///
    /// # Panics
    /// Panics when called off the thread that owns the live [`Application`](crate::Application).
    #[inline]
    pub fn update(colors: &sys::ThemeColors) {
        let _frame = frame_thread_guard("Theme::update");
        // SAFETY: ThemeColors is a plain repr(C) palette of color fields that owns nothing, so a
        // bitwise copy is a valid second value.
        let copy = unsafe { core::ptr::read(colors) };
        let mut slot = locked(&PALETTE);
        let stored: *const sys::ThemeColors = match slot.as_mut() {
            Some(palette) => {
                **palette = copy;
                &**palette
            }
            None => &**slot.insert(Box::new(copy)),
        };
        // SAFETY: stored points into a heap box owned by the process-lifetime PALETTE, which is
        // never freed or moved, so the pointer C retains stays valid. The lock excludes concurrent
        // writers, and _frame keeps the in-place overwrite on the thread that
        // also reads the palette while painting.
        unsafe { sys::fl_theme_update(stored) }
    }
}
