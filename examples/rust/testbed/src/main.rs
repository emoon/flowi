use flowi::Application;
use flowi::Flowi;
use flowi::ui::WindowFlags;

struct App {
    image: flowi::image::Image,
    flowi: Flowi,
}

fn main_loop(_flowi: &Flowi, app: &mut App) {
    let ui = app.flowi.ui();

    ui.window_begin("Hello, world!", WindowFlags::None);
    ui.image(app.image);
    ui.end();
}

fn main() {
    let flowi = Flowi::new("Test", "Test").unwrap();

    let mut app = App {
        image: flowi.image().create_from_file("/home/emoon/code/projects/rust_minifb/resources/uv.png").unwrap(),
        flowi,
    };

    Application::main_loop_ud(&mut app, main_loop);
}
