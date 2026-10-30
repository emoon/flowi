//! Typed, borrow-checked wrapper over flowi core's bump/region arena allocator.
//!
//! [`Arena::alloc`] borrows the arena shared and returns a &mut T tied to that
//! borrow; [`Arena::rewind`] takes &mut self. A live allocation therefore makes
//! rewind a borrow error, and the arena collections ([`ArenaVec`],
//! [`ArenaString`]) inherit it because they hold a &'a Arena.
//!
//! [`Arena::temp`] (&mut self) and the thread-local [`Arena::scratch`] /
//! [`Arena::scratch_avoiding`] hand the closure a &Arena under a higher-ranked
//! lifetime, so nothing allocated in the scope can escape it. A scratch scope opened
//! while the thread already has one open on the same arena joins that scope instead of
//! rewinding it, so an outer scope's allocations are never freed under it.
//!
//! Drop never runs on arena contents. Values are moved in and abandoned at
//! rewind/destroy (leaked, never dropped).
//!
//! [`ArenaVec`] / [`ArenaString`] grow allocate-copy-abandon and freeze via
//! into_slice() / into_str(); [`Arena::format`] and [`afmt!`](crate::afmt!) build strings
//! through [`core::fmt::Write`].

use core::fmt;
use core::marker::PhantomData;
use core::mem;
use core::ptr;
use core::slice;
use core::str;
use std::cell::RefCell;
use std::collections::HashMap;
use std::ffi::{CStr, CString};
use std::sync::{Mutex, OnceLock, PoisonError};

use crate::sys::{
    fl_arena_alloc_raw, fl_arena_alloc_raw_zero, fl_arena_create, fl_arena_destroy, fl_arena_rewind,
    fl_arena_scratch_begin, fl_arena_scratch_begin_conflict, fl_arena_scratch_end, fl_arena_temp_begin,
    fl_arena_temp_end,
};
use crate::sys::{Arena as FlArena, TempArena};

/// A typed handle onto a flowi core arena.
///
/// Holds a raw host-owned pointer, so it is !Send + !Sync: an arena belongs to
/// the thread that created it. An Arena obtained from [`Arena::new`] owns the C
/// arena and destroys it on drop; the scoped views handed to temp/scratch
/// closures borrow and never destroy.
pub struct Arena {
    raw: *mut FlArena,
    owned: bool,
    _marker: PhantomData<*const ()>,
}

/// A shared view of the foundation's main arena, as [`init`](crate::init) hands out.
///
/// Any number of these can exist at once, so it derefs to a shared [`Arena`] only: it can
/// allocate, but the exclusive operations ([`Arena::rewind`], [`Arena::temp`]) that would free
/// another view's live allocations are not reachable through it. Never destroys the arena;
/// that is [`shutdown`](crate::shutdown)'s job.
pub struct BaseArena(Arena);

impl BaseArena {
    /// Wrap the foundation's main arena.
    ///
    /// # Safety
    /// raw must be the live main arena of the foundation, which stays up for as long as the
    /// returned view and every allocation made through it exist.
    pub(crate) unsafe fn new(raw: *mut FlArena) -> BaseArena {
        BaseArena(Arena::borrowed(raw))
    }
}

impl core::ops::Deref for BaseArena {
    type Target = Arena;

    #[inline]
    fn deref(&self) -> &Arena {
        &self.0
    }
}

/// The NUL-terminated form of a caller's source path, for the memory tracker. Each
/// distinct path is terminated once and kept for the life of the process, since the
/// tracker stores the pointer.
fn tracked_file(path: &'static str) -> *const core::ffi::c_char {
    static PATHS: OnceLock<Mutex<HashMap<&'static str, &'static CStr>>> = OnceLock::new();

    let mut paths = PATHS
        .get_or_init(|| Mutex::new(HashMap::new()))
        .lock()
        .unwrap_or_else(PoisonError::into_inner);
    paths
        .entry(path)
        .or_insert_with(|| {
            // A path with an interior NUL falls back to the empty string.
            let terminated = CString::new(path).unwrap_or_default();
            Box::leak(terminated.into_boxed_c_str())
        })
        .as_ptr()
}

// mut_from_ref: every allocation is a fresh, disjoint block, so two &mut from the same &Arena never
// alias; rewind/Drop take &mut self, so no allocation is live when the arena is reset or destroyed.
#[allow(clippy::mut_from_ref)]
impl Arena {
    /// Reserve a new arena of reserved_size virtual bytes (committed on demand).
    /// The returned Arena owns the C arena and destroys it on drop.
    #[track_caller]
    pub fn new(reserved_size: u64) -> Arena {
        let caller = core::panic::Location::caller();
        // SAFETY: tracked_file returns a NUL-terminated C string that lives for the
        // rest of the process, as the tracker (which stores the pointer) requires.
        let raw = unsafe {
            fl_arena_create(
                reserved_size,
                tracked_file(caller.file()),
                caller.line() as i32,
            )
        };
        Arena {
            raw,
            owned: true,
            _marker: PhantomData,
        }
    }

    /// Wrap a borrowed scope arena (a temp/scratch view). Never destroyed on drop.
    #[inline]
    fn borrowed(raw: *mut FlArena) -> Arena {
        Arena {
            raw,
            owned: false,
            _marker: PhantomData,
        }
    }

    /// Wrap an arena the caller already owns through the raw C API, for example one
    /// received across an FFI boundary. Never destroyed on drop.
    ///
    /// # Safety
    /// raw must be a live arena that outlives the returned handle and every
    /// allocation made through it.
    #[inline]
    pub unsafe fn from_raw_borrowed(raw: *mut FlArena) -> Arena {
        Arena::borrowed(raw)
    }

    /// The raw `*mut FlArena` this handle drives (for conflict-avoidance and FFI).
    #[inline]
    pub fn as_raw(&self) -> *mut FlArena {
        self.raw
    }

    /// Allocate raw, aligned, uninitialized bytes. Returns an aligned non-null
    /// pointer; size == 0 yields a dangling aligned pointer without touching the
    /// arena.
    #[inline]
    fn alloc_raw(&self, size: usize, align: usize) -> *mut u8 {
        if size == 0 {
            return align as *mut u8;
        }
        // SAFETY: fl_arena_alloc_raw is a linked flowi-core export; the arena pointer
        // is valid for the life of self. The C allocator returns fresh,
        // non-overlapping memory.
        let p = unsafe { fl_arena_alloc_raw(self.raw, size as u64, align as u64) };
        p as *mut u8
    }

    /// Allocate zero-initialized, aligned bytes for count elements of T. Panics on a count whose byte
    /// size does not fit a usize.
    #[inline]
    fn alloc_raw_zeroed<T>(&self, count: usize) -> *mut u8 {
        let size = mem::size_of::<T>()
            .checked_mul(count)
            .expect("arena array size overflow");
        if size == 0 {
            return mem::align_of::<T>() as *mut u8;
        }
        // SAFETY: as alloc_raw, but the C side clears the block to zero.
        let p = unsafe { fl_arena_alloc_raw_zero(self.raw, size as u64, mem::align_of::<T>() as u64) };
        p as *mut u8
    }

    /// Move value into the arena and return a unique reference to it.
    ///
    /// The reference borrows the arena shared, so any number of allocations can
    /// coexist, but [`rewind`](Arena::rewind) (which takes &mut self) cannot run
    /// while one is live. value's destructor never runs - it is abandoned at
    /// rewind/destroy.
    #[inline]
    pub fn alloc<T>(&self, value: T) -> &mut T {
        let ptr = self.alloc_raw(mem::size_of::<T>(), mem::align_of::<T>()) as *mut T;
        // SAFETY: ptr is fresh, aligned, and sized for one T; ptr::write
        // moves value in without dropping the uninitialized destination.
        unsafe {
            ptr::write(ptr, value);
            &mut *ptr
        }
    }

    /// Allocate a zero-initialized T. Gated on [`bytemuck::Zeroable`]: the bound
    /// is the proof that an all-zero bit pattern is a valid T.
    #[inline]
    pub fn alloc_zeroed<T: bytemuck::Zeroable>(&self) -> &mut T {
        let ptr = self.alloc_raw_zeroed::<T>(1) as *mut T;
        // SAFETY: the block is `size_of::<T>()` zeroed, aligned bytes and T:
        // Zeroable guarantees all-zero is a valid T.
        unsafe { &mut *ptr }
    }

    /// Copy a slice into the arena and return a unique reference to the copy.
    #[inline]
    pub fn alloc_slice_copy<T: Copy>(&self, src: &[T]) -> &mut [T] {
        let ptr = self.alloc_raw(mem::size_of_val(src), mem::align_of::<T>()) as *mut T;
        // SAFETY: ptr addresses src.len() fresh, aligned T slots that do not
        // overlap src; T: Copy so the bitwise copy leaves src valid.
        unsafe {
            ptr::copy_nonoverlapping(src.as_ptr(), ptr, src.len());
            slice::from_raw_parts_mut(ptr, src.len())
        }
    }

    /// Copy a string into the arena and return a reference to the copy.
    #[inline]
    pub fn alloc_str(&self, src: &str) -> &mut str {
        let bytes = self.alloc_slice_copy(src.as_bytes());
        // SAFETY: bytes is a byte-for-byte copy of a valid &str.
        unsafe { str::from_utf8_unchecked_mut(bytes) }
    }

    /// Reset the arena to empty, reusing its memory. &mut self is what makes this
    /// safe: the borrow checker proves no allocation handed out by alloc (or any
    /// live [`ArenaVec`]/[`ArenaString`]) is still borrowed.
    #[inline]
    pub fn rewind(&mut self) {
        // SAFETY: fl_arena_rewind is a linked flowi-core export; self.raw is valid for self.
        unsafe { fl_arena_rewind(self.raw) }
    }

    /// Run f over a scope arena savepoint, restoring it (via end) when f
    /// returns - including on unwind, since ScopeGuard is a live local. The
    /// higher-ranked `for<'s>` lifetime means the scope arena (and anything
    /// borrowing it) cannot escape f.
    fn run_scoped<R>(
        mark: TempArena,
        end: unsafe extern "C" fn(TempArena),
        f: impl for<'s> FnOnce(&'s Arena) -> R,
    ) -> R {
        let scope = Arena::borrowed(mark.arena);
        // Dropped after f (reverse declaration order), so end runs once the
        // scope's allocations are dead - on normal return and on panic alike.
        let _guard = ScopeGuard { mark, end };
        f(&scope)
    }

    /// Run f in a temporary scope: everything it allocates into the scope arena
    /// is freed when f returns and cannot escape it.
    pub fn temp<R>(&mut self, f: impl for<'t> FnOnce(&'t Arena) -> R) -> R {
        // SAFETY: fl_arena_temp_begin is a linked flowi-core export; self.raw is valid for self.
        let mark = unsafe { fl_arena_temp_begin(self.raw) };
        Self::run_scoped(mark, fl_arena_temp_end, f)
    }

    /// Run f over a scratch savepoint. If the thread already has a scope open on that
    /// arena, f runs inside it and nothing is rewound until the outermost scope ends.
    fn run_scratch<R>(mark: TempArena, f: impl for<'s> FnOnce(&'s Arena) -> R) -> R {
        let scope = Arena::borrowed(mark.arena);
        // Dropped after f, so the savepoint is released once the scope's allocations are dead.
        let _scratch = ScratchScope::enter(mark);
        f(&scope)
    }

    /// Run f with a thread-local scratch arena scope. Everything it allocates is
    /// freed when the outermost scratch scope on the arena returns, and cannot escape f.
    /// Nested calls share the enclosing scope's arena and lifetime.
    pub fn scratch<R>(f: impl for<'s> FnOnce(&'s Arena) -> R) -> R {
        // SAFETY: fl_arena_scratch_begin is a linked flowi-core export; it lazily
        // initializes the thread-local scratch arena on first use.
        let mark = unsafe { fl_arena_scratch_begin() };
        Self::run_scratch(mark, f)
    }

    /// Like [`scratch`](Arena::scratch), but the scratch arena chosen avoids
    /// conflict - use when the scratch result is copied into conflict so the
    /// two are never the same underlying arena.
    pub fn scratch_avoiding<R>(conflict: &Arena, f: impl for<'s> FnOnce(&'s Arena) -> R) -> R {
        // SAFETY: fl_arena_scratch_begin_conflict is a linked flowi-core export; conflict.raw is valid for conflict.
        let mark = unsafe { fl_arena_scratch_begin_conflict(conflict.raw) };
        Self::run_scratch(mark, f)
    }

    /// Build a string in the arena from format arguments (see [`afmt!`](crate::afmt!)).
    #[inline]
    pub fn format(&self, args: fmt::Arguments<'_>) -> &str {
        let mut s = ArenaString::new(self);
        // ArenaString's write_str is infallible, so write_fmt cannot fail.
        let _ = fmt::Write::write_fmt(&mut s, args);
        s.into_str()
    }
}

impl Drop for Arena {
    fn drop(&mut self) {
        if self.owned {
            // SAFETY: we own this arena and hand out no reference that outlives
            // self (alloc's borrows are tied to &self), so nothing dangles.
            unsafe { fl_arena_destroy(self.raw) }
        }
    }
}

/// Restores a temp/scratch scope on drop - so the scope ends even if the closure
/// panics and unwinds.
struct ScopeGuard {
    mark: TempArena,
    end: unsafe extern "C" fn(TempArena),
}

impl Drop for ScopeGuard {
    fn drop(&mut self) {
        // SAFETY: mark was produced by the matching _begin slot and is ended
        // exactly once, here. TempArena's fields are Copy, so we reconstruct
        // the by-value savepoint without moving out of &mut self.
        unsafe {
            (self.end)(TempArena {
                arena: self.mark.arena,
                pos: self.mark.pos,
            })
        }
    }
}

thread_local! {
    /// The scratch arenas with a scope open on this thread.
    static OPEN_SCRATCH: RefCell<Vec<*mut FlArena>> = const { RefCell::new(Vec::new()) };
}

/// One scratch scope. Owns the savepoint only when it is the outermost scope open on its
/// arena; a nested one holds nothing and leaves the enclosing scope's savepoint to end.
struct ScratchScope {
    outermost: Option<ScopeGuard>,
}

impl ScratchScope {
    fn enter(mark: TempArena) -> ScratchScope {
        let first = OPEN_SCRATCH.with(|open| {
            let mut open = open.borrow_mut();
            let first = !open.contains(&mark.arena);
            if first {
                open.push(mark.arena);
            }
            first
        });
        ScratchScope {
            outermost: first.then(|| ScopeGuard {
                mark,
                end: fl_arena_scratch_end,
            }),
        }
    }
}

impl Drop for ScratchScope {
    fn drop(&mut self) {
        if let Some(guard) = &self.outermost {
            let arena = guard.mark.arena;
            OPEN_SCRATCH.with(|open| open.borrow_mut().retain(|a| *a != arena));
        }
    }
}

/// A growable vector backed by an arena.
///
/// Borrows the arena (&'a Arena) for its whole life, so it cannot be held across
/// a [`rewind`](Arena::rewind). Growth is allocate-copy-abandon: a full buffer is
/// replaced by a larger fresh arena block, the elements bit-copied over, and the
/// old block left as dead space until the arena is rewound. Contents are never
/// dropped; freeze the finished run with [`into_slice`](ArenaVec::into_slice).
pub struct ArenaVec<'a, T> {
    arena: &'a Arena,
    ptr: *mut T,
    len: usize,
    cap: usize,
    _marker: PhantomData<T>,
}

impl<'a, T> ArenaVec<'a, T> {
    /// A new empty vector that allocates from arena on first push.
    #[inline]
    pub fn new(arena: &'a Arena) -> Self {
        ArenaVec {
            arena,
            ptr: ptr::dangling_mut::<T>(),
            len: 0,
            cap: 0,
            _marker: PhantomData,
        }
    }

    /// A new empty vector with room for cap elements before the first regrowth.
    #[inline]
    pub fn with_capacity(arena: &'a Arena, cap: usize) -> Self {
        let mut v = ArenaVec::new(arena);
        if cap > 0 {
            v.grow_to(cap);
        }
        v
    }

    /// Number of elements pushed.
    #[inline]
    pub fn len(&self) -> usize {
        self.len
    }

    /// Whether no elements have been pushed.
    #[inline]
    pub fn is_empty(&self) -> bool {
        self.len == 0
    }

    /// Allocate a fresh block of new_cap elements, move the existing ones over,
    /// and abandon the old block.
    fn grow_to(&mut self, new_cap: usize) {
        let bytes = mem::size_of::<T>()
            .checked_mul(new_cap)
            .expect("ArenaVec capacity overflow");
        let new_ptr = self.arena.alloc_raw(bytes, mem::align_of::<T>()) as *mut T;
        // SAFETY: the new block holds `new_cap >= self.len` fresh T slots that do
        // not overlap the old block; bit-copying moves the elements and abandons
        // the originals (never dropped, consistent with arena leak semantics).
        unsafe {
            ptr::copy_nonoverlapping(self.ptr, new_ptr, self.len);
        }
        self.ptr = new_ptr;
        self.cap = new_cap;
    }

    /// Append an element, regrowing (allocate-copy-abandon) if the buffer is full.
    #[inline]
    pub fn push(&mut self, value: T) {
        if self.len == self.cap {
            let new_cap = if self.cap == 0 {
                4
            } else {
                self.cap.checked_mul(2).expect("ArenaVec capacity overflow")
            };
            self.grow_to(new_cap);
        }
        // SAFETY: `len < cap` after the grow, so ptr.add(len) is in-bounds and
        // uninitialized; ptr::write moves value in without dropping.
        unsafe {
            ptr::write(self.ptr.add(self.len), value);
        }
        self.len += 1;
    }

    /// The elements pushed so far.
    #[inline]
    pub fn as_slice(&self) -> &[T] {
        // SAFETY: ptr addresses len initialized, contiguous T.
        unsafe { slice::from_raw_parts(self.ptr, self.len) }
    }

    /// Freeze the vector into a slice borrowing the arena for the rest of the
    /// arena's borrow - consumes self so no further pushes can reallocate it.
    #[inline]
    pub fn into_slice(self) -> &'a mut [T] {
        // SAFETY: ptr addresses len initialized T in arena memory that lives
        // for 'a; consuming self guarantees no aliasing push follows.
        unsafe { slice::from_raw_parts_mut(self.ptr, self.len) }
    }
}

/// A growable UTF-8 string backed by an arena - an [`ArenaVec<u8>`] that stays
/// valid UTF-8. Implements [`core::fmt::Write`], so write! and
/// [`Arena::format`]/[`afmt!`](crate::afmt!) build into it.
pub struct ArenaString<'a> {
    bytes: ArenaVec<'a, u8>,
}

impl<'a> ArenaString<'a> {
    /// A new empty string that allocates from arena on first write.
    #[inline]
    pub fn new(arena: &'a Arena) -> Self {
        ArenaString {
            bytes: ArenaVec::new(arena),
        }
    }

    /// A new empty string with room for cap bytes before the first regrowth.
    #[inline]
    pub fn with_capacity(arena: &'a Arena, cap: usize) -> Self {
        ArenaString {
            bytes: ArenaVec::with_capacity(arena, cap),
        }
    }

    /// Length in bytes.
    #[inline]
    pub fn len(&self) -> usize {
        self.bytes.len()
    }

    /// Whether nothing has been written.
    #[inline]
    pub fn is_empty(&self) -> bool {
        self.bytes.is_empty()
    }

    /// Append a string slice.
    #[inline]
    pub fn push_str(&mut self, s: &str) {
        for &b in s.as_bytes() {
            self.bytes.push(b);
        }
    }

    /// The bytes written so far, as a string slice.
    #[inline]
    pub fn as_str(&self) -> &str {
        // SAFETY: only push_str/write_str append, and both add whole &strs,
        // so the buffer stays valid UTF-8.
        unsafe { str::from_utf8_unchecked(self.bytes.as_slice()) }
    }

    /// Freeze the string into a &str borrowing the arena for the rest of the
    /// arena's borrow - consumes self.
    #[inline]
    pub fn into_str(self) -> &'a str {
        let bytes = self.bytes.into_slice();
        // SAFETY: the buffer is valid UTF-8 (see as_str).
        unsafe { str::from_utf8_unchecked(bytes) }
    }
}

impl fmt::Write for ArenaString<'_> {
    #[inline]
    fn write_str(&mut self, s: &str) -> fmt::Result {
        self.push_str(s);
        Ok(())
    }
}

/// format!-style string building into an arena: afmt!(arena, "x = {x}")
/// returns a &str borrowing arena.
#[macro_export]
macro_rules! afmt {
    ($arena:expr, $($arg:tt)*) => {
        $arena.format(::core::format_args!($($arg)*))
    };
}

#[cfg(test)]
mod tests {
    use super::{tracked_file, Arena};

    #[test]
    fn tracked_file_terminates_once_and_is_cached_per_path() {
        let first = tracked_file("src/host/application.rs");
        let second = tracked_file("src/host/application.rs");
        assert_eq!(first, second, "same path must reuse the same C string");

        let other = tracked_file("src/core/core_abi.rs");
        assert_ne!(first, other, "distinct paths get distinct C strings");

        // SAFETY: tracked_file NUL-terminates what it returns and leaks it, so the
        // pointer is valid for the rest of the process.
        let text = unsafe { core::ffi::CStr::from_ptr(first) };
        assert_eq!(text.to_bytes(), b"src/host/application.rs");
    }

    #[test]
    #[should_panic(expected = "arena array size overflow")]
    fn a_zeroed_count_whose_byte_size_overflows_panics() {
        let arena = Arena::new(1 << 16);
        arena.alloc_raw_zeroed::<u64>(usize::MAX / 4 + 1);
    }
}
