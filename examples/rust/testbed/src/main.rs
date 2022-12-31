use flowi::Application;
use flowi::Flowi;

fn main_loop(ui: &Flowi) {
    println!("Hello, world!");
}

fn main() {
    let flowi = Flowi::new("Test", "Test").unwrap();
    Application::main_loop(main_loop);
}
