use glfw::Action;
use flowi_core::{input::{Key, Input}, ApplicationSettings};
use crate::application::Window;
use raw_window_handle::{HasRawWindowHandle, RawWindowHandle};

fn translate_glfw_to_flowi_key(key: glfw::Key) -> Option<Key> {
    match key {
        glfw::Key::A => Some(Key::A),
        glfw::Key::B => Some(Key::B),
        glfw::Key::C => Some(Key::C),
        glfw::Key::D => Some(Key::D),
        glfw::Key::E => Some(Key::E),
        glfw::Key::F => Some(Key::F),
        glfw::Key::G => Some(Key::G),
        glfw::Key::H => Some(Key::H),
        glfw::Key::I => Some(Key::I),
        glfw::Key::J => Some(Key::J),
        glfw::Key::K => Some(Key::K),
        glfw::Key::L => Some(Key::L),
        glfw::Key::M => Some(Key::M),
        glfw::Key::N => Some(Key::N),
        glfw::Key::O => Some(Key::O),
        glfw::Key::P => Some(Key::P),
        glfw::Key::Q => Some(Key::Q),
        glfw::Key::R => Some(Key::R),
        glfw::Key::S => Some(Key::S),
        glfw::Key::T => Some(Key::T),
        glfw::Key::U => Some(Key::U),
        glfw::Key::V => Some(Key::V),
        glfw::Key::W => Some(Key::W),
        glfw::Key::X => Some(Key::X),
        glfw::Key::Y => Some(Key::Y),
        glfw::Key::Z => Some(Key::Z),
        glfw::Key::Num0 => Some(Key::Keypad0),
        glfw::Key::Num1 => Some(Key::Keypad1),
        glfw::Key::Num2 => Some(Key::Keypad2),
        glfw::Key::Num3 => Some(Key::Keypad3),
        glfw::Key::Num4 => Some(Key::Keypad4),
        glfw::Key::Num5 => Some(Key::Keypad5),
        glfw::Key::Num6 => Some(Key::Keypad6),
        glfw::Key::Num7 => Some(Key::Keypad7),
        glfw::Key::Num8 => Some(Key::Keypad8),
        glfw::Key::Num9 => Some(Key::Keypad9),
        glfw::Key::Escape => Some(Key::Escape),
        glfw::Key::LeftControl => Some(Key::LeftCtrl),
        glfw::Key::LeftShift => Some(Key::LeftShift),
        glfw::Key::LeftAlt => Some(Key::LeftAlt),
        glfw::Key::LeftSuper => Some(Key::LeftSuper),
        glfw::Key::RightControl => Some(Key::RightCtrl),
        glfw::Key::RightShift => Some(Key::RightShift),
        glfw::Key::RightAlt => Some(Key::RightAlt),
        glfw::Key::RightSuper => Some(Key::RightSuper),
        glfw::Key::Menu => Some(Key::Menu),
        glfw::Key::LeftBracket => Some(Key::LeftBracket),
        glfw::Key::RightBracket => Some(Key::RightBracket),
        glfw::Key::Semicolon => Some(Key::Semicolon),
        glfw::Key::Comma => Some(Key::Comma),
        glfw::Key::Period => Some(Key::Period),
        glfw::Key::Apostrophe => Some(Key::Apostrophe),
        glfw::Key::Slash => Some(Key::Slash),
        glfw::Key::Backslash => Some(Key::Backslash),
        glfw::Key::GraveAccent => Some(Key::GraveAccent),
        glfw::Key::Equal => Some(Key::Equal),
        glfw::Key::Minus => Some(Key::Minus),
        glfw::Key::Space => Some(Key::Space),
        glfw::Key::Enter => Some(Key::Enter),
        glfw::Key::Backspace => Some(Key::Backspace),
        glfw::Key::Tab => Some(Key::Tab),
        glfw::Key::PageUp => Some(Key::PageUp),
        glfw::Key::PageDown => Some(Key::PageDown),
        glfw::Key::End => Some(Key::End),
        glfw::Key::Home => Some(Key::Home),
        glfw::Key::Insert => Some(Key::Insert),
        glfw::Key::Delete => Some(Key::Delete),
        //glfw::Key::Add => Some(Key::KeypadAdd),
        //glfw::Key::Subtract => Some(Key::KeypadSubtract),
        //glfw::Key::Multiply => Some(Key::Multiply),
        //glfw::Key::Divide => Some(Key::Divide),
        glfw::Key::Left => Some(Key::LeftArrow),
        glfw::Key::Right => Some(Key::RightArrow),
        glfw::Key::Up => Some(Key::UpArrow),
        glfw::Key::Down => Some(Key::DownArrow),
        glfw::Key::Kp0 => Some(Key::Keypad0),
        glfw::Key::Kp1 => Some(Key::Keypad1),
        glfw::Key::Kp2 => Some(Key::Keypad2),
        glfw::Key::Kp3 => Some(Key::Keypad3),
        glfw::Key::Kp4 => Some(Key::Keypad4),
        glfw::Key::Kp5 => Some(Key::Keypad5),
        glfw::Key::Kp6 => Some(Key::Keypad6),
        glfw::Key::Kp7 => Some(Key::Keypad7),
        glfw::Key::Kp8 => Some(Key::Keypad8),
        glfw::Key::Kp9 => Some(Key::Keypad9),
        glfw::Key::F1 => Some(Key::F1),
        glfw::Key::F2 => Some(Key::F2),
        glfw::Key::F3 => Some(Key::F3),
        glfw::Key::F4 => Some(Key::F4),
        glfw::Key::F5 => Some(Key::F5),
        glfw::Key::F6 => Some(Key::F6),
        glfw::Key::F7 => Some(Key::F7),
        glfw::Key::F8 => Some(Key::F8),
        glfw::Key::F9 => Some(Key::F9),
        glfw::Key::F10 => Some(Key::F10),
        glfw::Key::F11 => Some(Key::F11),
        glfw::Key::F12 => Some(Key::F12),
        glfw::Key::Pause => Some(Key::Pause),
        _ => None,
    }
}

pub(crate) struct GlfwWindow {
    glfw: glfw::Glfw,
    window: glfw::PWindow,
    events: glfw::GlfwReceiver<(f64, glfw::WindowEvent)>,
    time: f64,
    should_close: bool,
}

impl GlfwWindow {
    fn update_input(&mut self) {
        self.glfw.poll_events();
        for (_, event) in glfw::flush_messages(&self.events) {
            match event {
                glfw::WindowEvent::Key(key, _, action, _) => {
                    if let Some(key) = translate_glfw_to_flowi_key(key) {
                        // TODO: Hack to close window with esc
                        if key == Key::Escape && action == Action::Press { 
                            self.should_close = true;
                        }
                        Input::add_key_event(key, action == Action::Press);
                    } else {
                        println!("Unknown key: {:?}", key);
                    }
                }

                glfw::WindowEvent::Focus(focus) => {
                    Input::add_focus_event(focus);
                }

                glfw::WindowEvent::CursorPos(x, y) => {
                    Input::add_mouse_pos_event(x as f32, y as f32);
                }

                glfw::WindowEvent::MouseButton(button, action, _) => {
                    Input::add_mouse_button_event(button as _, action == Action::Press);
                }

                /*
                glfw::WindowEvent::FileDrop(paths) => {
                    self.input.file_drop(paths);
                }
                */

                _ => {}
            }
        }
    }

    // TODO: Support Emscripten pad
    fn update_pad(&mut self) {
        let digital_buttons = [
            (Key::GamepadBack, glfw::GamepadButton::ButtonBack, 6),
            (Key::GamepadStart, glfw::GamepadButton::ButtonStart, 7),
            (Key::GamepadFaceLeft, glfw::GamepadButton::ButtonX, 2),  // Xbox X, PS Square
            (Key::GamepadFaceRight, glfw::GamepadButton::ButtonB, 1),  // Xbox B, PS Circle,
            (Key::GamepadFaceUp, glfw::GamepadButton::ButtonY, 3),    // Xbox Y, PS Triangle
            (Key::GamepadFaceDown, glfw::GamepadButton::ButtonA, 0),  // Xbox A, PS Cross

            (Key::GamepadDpadLeft, glfw::GamepadButton::ButtonDpadLeft, 14),
            (Key::GamepadDpadRight, glfw::GamepadButton::ButtonDpadRight, 12),
            (Key::GamepadDpadUp, glfw::GamepadButton::ButtonDpadUp, 11),
            (Key::GamepadDpadDown, glfw::GamepadButton::ButtonDpadDown, 13),

            (Key::GamepadL1, glfw::GamepadButton::ButtonLeftBumper, 4),
            (Key::GamepadR1, glfw::GamepadButton::ButtonRightBumper, 5),
            (Key::GamepadL3, glfw::GamepadButton::ButtonLeftThumb, 8),
            (Key::GamepadR3, glfw::GamepadButton::ButtonRightThumb, 9),

            //(Key::Guide, glfw::GamepadButton::Guide, 8),
        ];

        let analog_buttons = [
            (Key::GamepadL2, glfw::GamepadAxis::AxisLeftTrigger, 4, -0.75, 1.0),
            (Key::GamepadR2, glfw::GamepadAxis::AxisRightTrigger, 5, -0.75, 1.0),
            (Key::GamepadLStickLeft, glfw::GamepadAxis::AxisLeftX, 0, -0.25, -1.0),
            (Key::GamepadLStickRight, glfw::GamepadAxis::AxisLeftX, 0, 0.25, 1.0),
            (Key::GamepadLStickUp, glfw::GamepadAxis::AxisLeftY, 1, -0.25, -1.0),
            (Key::GamepadLStickDown, glfw::GamepadAxis::AxisLeftY, 1, 0.25, 1.0),
            (Key::GamepadRStickLeft, glfw::GamepadAxis::AxisRightX, 2, -0.25, -1.0),
            (Key::GamepadRStickRight, glfw::GamepadAxis::AxisRightX, 2, 0.25, 1.0),
            (Key::GamepadRStickUp, glfw::GamepadAxis::AxisRightY, 3, -0.25, -1.0),
            (Key::GamepadRStickDown, glfw::GamepadAxis::AxisRightY, 3, 0.25, 1.0),
        ];

        let joystick = self.glfw.get_joystick(glfw::JoystickId::Joystick1);

        if let Some(state) = joystick.get_gamepad_state() {
            for (key, button, _index) in digital_buttons.iter() {
                Input::add_key_event(*key, state.get_button_state(*button) == Action::Press);
            }

            for (key, axis, _index, min, max) in analog_buttons.iter() {
                let value = state.get_axis(*axis);
                let v = value - *min / *max - *min;
                Input::add_key_analog_event(*key, v > 0.0, v.clamp(0.0, 1.0)); 
            }
        }
    }

    fn update_modifiers(&mut self) {
        let ctrl = self.window.get_key(glfw::Key::LeftControl) == Action::Press
            || self.window.get_key(glfw::Key::RightControl) == Action::Press;
        let shift = self.window.get_key(glfw::Key::LeftShift) == Action::Press
            || self.window.get_key(glfw::Key::RightShift) == Action::Press;
        let alt = self.window.get_key(glfw::Key::LeftAlt) == Action::Press
            || self.window.get_key(glfw::Key::RightAlt) == Action::Press;
        let s = self.window.get_key(glfw::Key::LeftSuper) == Action::Press
            || self.window.get_key(glfw::Key::RightSuper) == Action::Press;

        Input::add_key_event(Key::LeftCtrl, ctrl);
        Input::add_key_event(Key::LeftShift, shift);
        Input::add_key_event(Key::LeftAlt, alt);
        Input::add_key_event(Key::LeftSuper, s);
    }

    fn update_mouse_data(&mut self) {
        // Make sure we have focus
        if !self.window.is_focused() {
            return;
        } 

        let (x, y) = self.window.get_cursor_pos();
        Input::add_mouse_pos_event(x as _, y as _);
    }
}

impl Window for GlfwWindow {
    fn new(settings: &ApplicationSettings) -> Self {
        let mut glfw = glfw::init_no_callbacks().unwrap();
        glfw.window_hint(glfw::WindowHint::ClientApi(glfw::ClientApiHint::NoApi));

        let width = core::cmp::max(settings.width as _, 800);
        let height = core::cmp::max(settings.height as _, 600);

        let (mut window, events) = glfw
            .create_window(width, height, "Flowi", glfw::WindowMode::Windowed)
            .expect("Failed to create GLFW window.");

        window.set_key_polling(true);
        window.set_mouse_button_polling(true);

        Self {
            glfw,
            window,
            events,
            time: 0.0,
            should_close: false,
        }
    }

    fn update(&mut self) {
        let current_time = f64::max(self.time + 0.00001, self.glfw.get_time());
        let delta_time = if self.time > 0.0 { current_time - self.time } else { 1.0 / 60.0 };

        let display_size = self.window.get_framebuffer_size();
        let window_size = self.window.get_size();

        Input::update_screen_size_time(
            display_size.0 as _, 
            display_size.1 as _, 
            window_size.0 as _,
            window_size.1 as _,
            delta_time as _);

        self.time = current_time;

        self.update_input();
        self.update_mouse_data();
        self.update_modifiers();
        self.update_pad();
    }

    fn raw_window_handle(&self) -> RawWindowHandle {
        self.window.raw_window_handle()
    }

    fn is_focused(&self) -> bool {
        self.window.is_focused()
    }

    fn should_close(&mut self) -> bool {
        self.should_close
    }
}


