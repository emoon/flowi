fn needs_copy<T: Copy>() {}

fn main() {
    needs_copy::<flowi::ListResult>();
}
