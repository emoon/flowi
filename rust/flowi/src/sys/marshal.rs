//! Arena copies and slice views for `extern "C"` entry points.
//!
//! The arena entries are unsafe for one shared reason: the caller supplies a live
//! `*mut Arena` for the call and nothing checks it.

use super::{fl_arena_alloc_raw, Arena};

/// Allocate count uninitialised T out of arena - the array a result struct hands back by pointer.
///
/// # Panics
/// Panics on a count whose byte size does not fit a usize.
///
/// # Safety
/// arena must be a live arena for the call, and the caller must initialise every element it lets C read.
pub unsafe fn arena_alloc_array<T>(arena: *mut Arena, count: usize) -> *mut T {
    let size = count
        .checked_mul(core::mem::size_of::<T>())
        .expect("arena array size overflow");
    // SAFETY: arena is live for the call - the caller's contract.
    unsafe { fl_arena_alloc_raw(arena, size as u64, core::mem::align_of::<T>() as u64) as *mut T }
}

/// Move one T into arena and return the pointer C holds it by. The value's destructor never runs - arena
/// contents are abandoned, not dropped.
///
/// # Safety
/// arena must be a live arena for the call.
pub unsafe fn arena_box<T>(arena: *mut Arena, value: T) -> *mut T {
    // SAFETY: arena is live for the call (the caller's contract), so the allocation returns one aligned,
    // uniquely-owned T slot; write moves value in without dropping the uninitialised destination.
    unsafe {
        let p = arena_alloc_array::<T>(arena, 1);
        p.write(value);
        p
    }
}

/// View a C (pointer, count) pair as a slice - the shape every array argument takes across the ABI.
///
/// A null pointer or a zero count yields an empty slice.
///
/// # Safety
/// When items is non-null and count is non-zero, it must point at count initialised, contiguous
/// T that stay put and unwritten for 'a. The caller picks 'a, so it owes that lifetime.
#[inline]
pub unsafe fn borrow_slice<'a, T>(items: *const T, count: usize) -> &'a [T] {
    if items.is_null() || count == 0 {
        return &[];
    }
    // SAFETY: the caller's contract, minus the null / empty cases handled above.
    unsafe { core::slice::from_raw_parts(items, count) }
}

/// [`borrow_slice`] for a buffer C hands over to be written - an output array or a mix destination.
///
/// # Safety
/// When items is non-null and count is non-zero, it must point at count contiguous, writable,
/// initialised T that no one else touches for 'a. The caller picks 'a, so it owes that lifetime.
#[inline]
pub unsafe fn borrow_slice_mut<'a, T>(items: *mut T, count: usize) -> &'a mut [T] {
    if items.is_null() || count == 0 {
        return &mut [];
    }
    // SAFETY: the caller's contract, minus the null / empty cases handled above.
    unsafe { core::slice::from_raw_parts_mut(items, count) }
}

#[cfg(test)]
mod tests {
    use super::*;
    use crate::sys::{fl_arena_create, fl_arena_destroy};

    const RESERVE: u64 = 1 << 16;

    /// A live arena for one test, destroyed on drop.
    struct TestArena(*mut Arena);

    impl TestArena {
        fn new() -> TestArena {
            // SAFETY: fl_arena_create takes a reservation size, a NUL-terminated name and a flags word.
            TestArena(unsafe { fl_arena_create(RESERVE, c"marshal_test".as_ptr(), 0) })
        }
    }

    impl Drop for TestArena {
        fn drop(&mut self) {
            // SAFETY: self.0 came from fl_arena_create and is destroyed exactly once.
            unsafe { fl_arena_destroy(self.0) }
        }
    }

    /// Both spellings of "no elements" a C caller may pass; from_raw_parts accepts neither.
    #[test]
    fn an_absent_array_borrows_as_empty_however_c_spelled_it() {
        // SAFETY: the null and zero-count arms never dereference anything.
        unsafe {
            assert!(borrow_slice::<u32>(core::ptr::null(), 7).is_empty());
            assert!(borrow_slice::<u32>(core::ptr::null(), 0).is_empty());
            assert!(borrow_slice_mut::<u32>(core::ptr::null_mut(), 7).is_empty());
        }

        let items = [1u32, 2, 3];
        // SAFETY: the pointer is live; the zero count is the case under test.
        assert!(unsafe { borrow_slice(items.as_ptr(), 0) }.is_empty());
    }

    #[test]
    fn a_present_array_borrows_its_elements() {
        let items = [1u32, 2, 3];
        // SAFETY: items outlives the borrow and holds exactly three initialised u32.
        assert_eq!(unsafe { borrow_slice(items.as_ptr(), items.len()) }, &items);

        let mut out = [0u32; 3];
        // SAFETY: out outlives the borrow and holds exactly three writable u32.
        unsafe { borrow_slice_mut(out.as_mut_ptr(), out.len()) }.copy_from_slice(&items);
        assert_eq!(out, items);
    }

    #[test]
    fn arena_alloc_array_hands_back_aligned_writable_slots() {
        let arena = TestArena::new();
        // SAFETY: arena is live; every one of the three slots is written before it
        // is read back.
        let values = unsafe {
            let slots = arena_alloc_array::<u64>(arena.0, 3);
            assert_eq!(slots.align_offset(core::mem::align_of::<u64>()), 0);
            for (i, value) in [7u64, 8, 9].into_iter().enumerate() {
                slots.add(i).write(value);
            }
            core::slice::from_raw_parts(slots, 3).to_vec()
        };
        assert_eq!(values, [7, 8, 9]);
    }

    #[test]
    #[should_panic(expected = "arena array size overflow")]
    fn arena_alloc_array_refuses_a_count_whose_byte_size_overflows() {
        let arena = TestArena::new();
        // SAFETY: arena is live; the multiply panics before the allocation is attempted.
        unsafe { arena_alloc_array::<u64>(arena.0, usize::MAX / 4 + 1) };
    }

    #[test]
    fn arena_box_moves_the_value_into_the_arena() {
        let arena = TestArena::new();
        // SAFETY: arena is live and arena_box initialised the slot it returns.
        let read_back = unsafe { *arena_box(arena.0, (1u32, 2u32)) };
        assert_eq!(read_back, (1, 2));
    }
}
