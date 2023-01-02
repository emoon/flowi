use flowi::Flowi;
use flowi::window::WindowFlags;
use flowi::application::Application;

struct App {
    image: flowi::image::Image,
}

fn main_loop(flowi: &Flowi, app: &mut App) {
    let ui = flowi.ui();
    let window = flowi.window();
    let cursor = flowi.cursor();
    let text = flowi.text();

    window.begin("Hello, world!", WindowFlags::NO_TITLE_BAR);
    cursor.set_pos_y(110.0);
    text.show("Hello, world!");
    ui.image(app.image);
    window.end();
}

fn main() {
    let flowi = Flowi::new("Test", "Test").unwrap();

    let mut app = App {
        image: flowi.image().create_from_file("/home/emoon/code/projects/rust_minifb/resources/uv.png").unwrap(),
    };

    Application::main_loop_ud(&mut app, main_loop);
}
