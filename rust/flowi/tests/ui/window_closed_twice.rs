use flowi::{Application, Window};

fn close_twice(app: &mut Application, window: Window) {
    app.close_window(window);
    app.close_window(window);
}

fn main() {}
