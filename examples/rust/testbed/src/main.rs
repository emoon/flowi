use flowi::Flowi;
use flowi::window::{WindowFlags, HoveredFlags};
use flowi::manual::Color;
use flowi::style::StyleColor;
use flowi::application::Application;
use flowi::font::Font;

struct App {
    image: flowi::image::Image,
    _font: Font,
}

fn main_loop(flowi: &Flowi, app: &mut App) {
    let ui = flowi.ui();
    let window = flowi.window();
    let cursor = flowi.cursor();
    let text = flowi.text();
    let menu = flowi.menu();
    let button = flowi.button();
    let item = flowi.item();
    let style = flowi.style();

    window.begin("Hello, world!", WindowFlags::NO_TITLE_BAR | WindowFlags::MENU_BAR);
    cursor.set_pos_y(110.0);
    text.show("Hello, world!");
    if button.image_with_text(app.image, "Click me!") {
        println!("Clicked!");
    }

    if item.is_hovered(HoveredFlags::RECT_ONLY) {
        println!("Hovered!");
    }

    style.push_color(StyleColor::HeaderHovered,
        Color { r: 241.0 / 255.0, g: 105.0 / 255.0, b: 49.0 / 255.0, a: 1.0 });

    if menu.begin_main_bar() {
        if menu.begin("File  ", true) {
            if menu.item("New Song") {
                println!("Quit");
            }
            menu.end();
        }

        if menu.begin("Edit  ", true) {
            menu.item("Undo Changed Value");
            menu.end();
        }

        if menu.begin("View  ", true) {
            menu.item("Show Fullscreen");
            menu.end();
        }
        menu.end_main_bar();
    }

    style.pop_color();

    //ui.image(app.image);
    window.end();
}

fn main() {
    let flowi = Flowi::new("Test", "Test").unwrap();
    let font = flowi.font();

    let mut app = App {
        image: flowi.image().create_from_file("/home/emoon/code/projects/rust_minifb/resources/uv.png").unwrap(),
        _font: font.new_from_file("../../../data/Montserrat-Bold.ttf", 32).unwrap(),
    };

    if !Application::main_loop_ud(&mut app, main_loop) {
        println!("Failed to create main application");
    }
}
