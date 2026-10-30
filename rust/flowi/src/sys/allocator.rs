//! Rust's global heap backed by Flowi's C heap API.
//!
//! The calls resolve to the linked native heap, including in plugins, and need no
//! `fl_init`. Allocations must still be released by their owning API: sharing a heap
//! does not make a C buffer a valid Rust `Vec`.

use super::foundation::heap;
use std::alloc::{GlobalAlloc, Layout};

/// Uses Flowi's native heap, normally mimalloc. Installed as the process
/// allocator only when the `global-allocator` feature is enabled.
pub struct HeapAllocator;

fn ordinary(layout: Layout) -> bool {
    // The ordinary heap guarantees pointer alignment; larger alignments use the paired
    // aligned allocate/free API.
    layout.align() <= std::mem::align_of::<usize>()
}

#[inline]
fn record_allocation() {
    #[cfg(feature = "allocation-probe")]
    super::allocation_probe::record_allocation();
}

// SAFETY: the native heap is thread-safe and does not call Rust or require
// application initialization. Every allocation honors Layout's alignment and
// uses the matching free/realloc family; null propagates allocation failure.
unsafe impl GlobalAlloc for HeapAllocator {
    unsafe fn alloc(&self, layout: Layout) -> *mut u8 {
        record_allocation();
        // SAFETY: Layout provides a nonzero size and power-of-two alignment.
        unsafe {
            if ordinary(layout) {
                heap::fl_heap_alloc(layout.size() as u64).cast()
            } else {
                heap::fl_heap_alloc_aligned(layout.align() as u64, layout.size() as u64).cast()
            }
        }
    }

    unsafe fn alloc_zeroed(&self, layout: Layout) -> *mut u8 {
        if ordinary(layout) {
            record_allocation();
            // SAFETY: the native calloc honors this size/alignment and zeroes the block.
            unsafe { heap::fl_heap_calloc(1, layout.size() as u64).cast() }
        } else {
            // SAFETY: forwarding the caller's valid Layout to our aligned path.
            let ptr = unsafe { self.alloc(layout) };
            if !ptr.is_null() {
                // SAFETY: the successful allocation contains layout.size() writable bytes.
                unsafe { ptr.write_bytes(0, layout.size()) };
            }
            ptr
        }
    }

    unsafe fn dealloc(&self, ptr: *mut u8, layout: Layout) {
        // SAFETY: the caller supplies a live block and its original layout,
        // which selects exactly the native family that allocated it.
        unsafe {
            if ordinary(layout) {
                heap::fl_heap_free(ptr.cast());
            } else {
                heap::fl_heap_free_aligned(ptr.cast());
            }
        }
    }

    unsafe fn realloc(&self, ptr: *mut u8, layout: Layout, new_size: usize) -> *mut u8 {
        if ordinary(layout) {
            record_allocation();
            // SAFETY: ptr belongs to the ordinary native heap. realloc preserves
            // its contents and leaves the original allocation live on failure.
            return unsafe { heap::fl_heap_realloc(ptr.cast(), new_size as u64).cast() };
        }
        // No aligned realloc exists natively: allocate/copy/free, leaving ptr live on failure.
        // SAFETY: GlobalAlloc::realloc requires new_size > 0 and a valid rounded size.
        let new_layout = unsafe { Layout::from_size_align_unchecked(new_size, layout.align()) };
        // SAFETY: new_layout satisfies the allocation contract.
        let new_ptr = unsafe { self.alloc(new_layout) };
        if !new_ptr.is_null() {
            // SAFETY: both live allocations cover the copied bytes and cannot overlap.
            unsafe { std::ptr::copy_nonoverlapping(ptr, new_ptr, layout.size().min(new_size)) };
            // SAFETY: the copy is complete; release the original with its original layout.
            unsafe { self.dealloc(ptr, layout) };
        }
        new_ptr
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn alignment_zeroing_and_reallocation_preserve_contents() {
        for alignment in [1, 2, 8, 16, 64, 4096] {
            let layout = Layout::from_size_align(37, alignment).unwrap();
            // SAFETY: every pointer is checked before access and released with
            // the current layout; all accesses stay within its allocated size.
            unsafe {
                let ptr = HeapAllocator.alloc_zeroed(layout);
                assert!(!ptr.is_null());
                assert_eq!(ptr as usize % alignment, 0);
                assert!(std::slice::from_raw_parts(ptr, 37).iter().all(|&b| b == 0));
                ptr.write_bytes(0x5a, 37);
                let grown = HeapAllocator.realloc(ptr, layout, 8193);
                assert!(!grown.is_null());
                assert_eq!(grown as usize % alignment, 0);
                assert!(std::slice::from_raw_parts(grown, 37)
                    .iter()
                    .all(|&b| b == 0x5a));
                let grown_layout = Layout::from_size_align(8193, alignment).unwrap();
                let shrunk = HeapAllocator.realloc(grown, grown_layout, 13);
                assert!(!shrunk.is_null());
                assert_eq!(shrunk as usize % alignment, 0);
                assert!(std::slice::from_raw_parts(shrunk, 13)
                    .iter()
                    .all(|&b| b == 0x5a));
                HeapAllocator.dealloc(shrunk, Layout::from_size_align(13, alignment).unwrap());
            }
        }
    }

    #[cfg(feature = "global-allocator")]
    #[test]
    fn rust_collections_use_the_native_heap() {
        let values = vec![0x5au8; 8193];
        // SAFETY: the global allocator routes this ordinary-aligned Vec to
        // fl_heap_alloc, and it stays live during the native usable-size query.
        unsafe {
            let native = heap::fl_heap_alloc(8193);
            assert!(!native.is_null());
            let native_size = heap::fl_heap_usable_size(native);
            let rust_size = heap::fl_heap_usable_size(values.as_ptr().cast_mut().cast());
            heap::fl_heap_free(native);
            // The system fallback reports zero; mimalloc can inspect both blocks.
            if native_size != 0 {
                assert!(rust_size >= values.len() as u64);
            } else {
                assert_eq!(rust_size, 0);
            }
        }
    }

    #[cfg(feature = "global-allocator")]
    #[test]
    fn rust_collections_can_be_dropped_on_another_thread() {
        let values = vec![0x5au8; 8193];
        std::thread::spawn(move || {
            assert!(values.iter().all(|&b| b == 0x5a));
            drop(values);
        })
        .join()
        .unwrap();
    }
}
