use flowi::BaseArena;

fn reuse(base: &mut BaseArena) {
    base.rewind();
    base.temp(|_| ());
}

fn main() {}
