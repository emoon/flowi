//! Anything allocated in a temp scope is freed when the closure returns, so it
//! must not escape. The higher-ranked `for<'t>` lifetime on the closure makes the
//! returned &'t mut T unnameable as the scope's result type - a compile error.

use flowi::Arena;

fn main() {
    let mut arena = Arena::new(1 << 20);
    let escaped = arena.temp(|t| t.alloc(1u32));
    let _ = *escaped;
}
