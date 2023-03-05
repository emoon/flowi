//use flowi::application::Application;
use flowi::font::Font;
use flowi::manual::Application;
use flowi::manual::Color;
use flowi::style::StyleColor;
use flowi::window::{HoveredFlags, WindowFlags};
use flowi::Flowi;

struct App {
    dummy: u32,
    //image: flowi::image::Image,
    shader: flowi::shader::Shader,
    //_font: Font,
    //icons: Font,
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
    let font = flowi.font();

    window.begin(
        "Hello, world!",
        WindowFlags::NO_TITLE_BAR | WindowFlags::MENU_BAR,
    );
    cursor.set_pos_y(110.0);
    text.show("Hello, world11111!");

    /*
    font.push(app.icons);
    let mut test_string = char::from_u32(0xe900).unwrap().to_string();
    test_string.push(' ');
    test_string.push(char::from_u32(0xe901).unwrap());
    text.show(&test_string);
    */

    /*
    if button.image_with_text(app.image, "test") {
        println!("Clicked!");
    }
    */

    //font.pop();

    /*
    if item.is_hovered(HoveredFlags::RECT_ONLY) {
        println!("Hovered!");
    }
    */

    style.push_color(
        StyleColor::HeaderHovered,
        Color {
            r: 241.0 / 255.0,
            g: 105.0 / 255.0,
            b: 49.0 / 255.0,
            a: 1.0,
        },
    );

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
    let settings = flowi::application_settings::ApplicationSettings { some_data: 0 };
    let flowi_app = Application::new(&settings).unwrap();
    let io = flowi_app.io();
    //let flowi_app = Application::new_from_lib("mylib.so", &settings).unwrap();
    //let flowi = Flowi::new_from_dynamic("mylib.so", "Test", "Test").unwrap();
    //let font = flowi.font();

    let mut app = App {
        /*
        image: io
            .load_image_from_url("/home/emoon/code/projects/rust_minifb/resources/uv.png")
            .unwrap(),
        */
        shader: io
            .load_fragment_shader_comp("../../../data/shaders/fs_ocornut_imgui.sc")
            .unwrap(),
        //_font: font.new_from_file("../../../data/Montserrat-Bold.ttf", 32).unwrap(),
        //_font: font.new_from_file("../../../data/Montserrat-Bold.ttf", 32).unwrap(),
        /*
        _font: font
            .new_from_file(
                "/home/emoon/code/projects/flowi/data/Montserrat-Bold.ttf",
                32,
            )
            .unwrap(),
        icons: font
            .new_from_file_range(
                "/home/emoon/code/projects/flowi/data/svgs.ttf",
                28,
                0xe900,
                0xe905,
            )
            .unwrap(),
        */
        dummy: 0,
    };

    if !flowi_app.main_loop_ud(&mut app, main_loop) {
        //println!("Failed to create main application");
    }
}
