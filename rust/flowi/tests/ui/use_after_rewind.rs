//! A value allocated with &self alloc must not survive a &mut self rewind:
//! the outstanding shared borrow makes rewind a borrow error.
//! Line numbers below are pinned by the expected use_after_rewind.stderr.

use flowi::Arena;

fn main() {
    let mut arena = Arena::new(1 << 20);
    let r = arena.alloc(1u32);
    arena.rewind();
    let _ = *r;
}
