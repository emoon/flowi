//! Anything allocated in a scratch scope lives only for the closure, so it must
//! not escape into the surrounding frame. The higher-ranked `for<'s>` lifetime
//! makes the returned &'s mut T unnameable as the scope's result type.

use flowi::Arena;

fn main() {
    let escaped = Arena::scratch(|s| s.alloc(1u32));
    let _ = *escaped;
}
