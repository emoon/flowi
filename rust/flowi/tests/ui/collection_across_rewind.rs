//! An ArenaVec borrows the arena for its whole life, so holding one across a
//! rewind (which needs &mut self) is a borrow error - the collection cannot
//! outlive the memory it points into.

use flowi::{Arena, ArenaVec};

fn main() {
    let mut arena = Arena::new(1 << 20);
    let mut v = ArenaVec::new(&arena);
    v.push(1u32);
    arena.rewind();
    let _ = v.len();
}
