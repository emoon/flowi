//! CPU topology and thread affinity - which cores a heterogeneous SoC offers and
//! how to pin a thread to a chosen set of them.
//!
//! Only Linux classifies cores (it reads /sys/devices/system/cpu/); elsewhere
//! every core reports as unknown, so [`performance_cores`] comes back empty and
//! [`pin_current_thread`] reports failure because no affinity work is done.
//!
//! ```no_run
//! if let Some(&cpu) = flowi::performance_cores().first() {
//!     if flowi::pin_current_thread(&[cpu]) {
//!         println!("pinned to cpu {cpu}");
//!     }
//! }
//! ```

use std::ptr;

use crate::sys;

use crate::Arena;

/// The OS CPU ids of every performance core, in the order the topology reports
/// them. Empty when the platform does not classify cores, or when it has no
/// performance cores to distinguish.
pub fn performance_cores() -> Vec<i32> {
    Arena::scratch(|arena| {
        // SAFETY: fl_cpu_query_by_type allocates its result array from the scratch
        // arena, which outlives the copy below.
        let list = unsafe { sys::fl_cpu_query_by_type(sys::CoreType::Performance, arena.as_raw()) };
        if list.ids.is_null() || list.count <= 0 {
            return Vec::new();
        }
        // SAFETY: the C side reports count initialised ids at ids.
        unsafe { std::slice::from_raw_parts(list.ids, list.count as usize) }.to_vec()
    })
}

/// Pin the calling thread to cpus, reporting whether the OS accepted it.
///
/// True only when the pinning was applied. False on a cpu id this machine does
/// not have, and on every platform but Linux, which is the only one that
/// performs affinity work at all.
///
/// An empty set returns false and leaves the thread's affinity unchanged.
pub fn pin_current_thread(cpus: &[i32]) -> bool {
    if cpus.is_empty() {
        return false;
    }
    let list = sys::CpuIdList {
        ids: cpus.as_ptr().cast_mut(),
        count: cpus.len() as i32,
    };
    // SAFETY: a null thread handle names the calling thread, and list borrows
    // cpus for the duration of the call.
    unsafe { sys::fl_cpu_set_thread_affinity(ptr::null_mut(), list) }
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn an_empty_cpu_set_is_refused_rather_than_widening_the_affinity() {
        assert!(!pin_current_thread(&[]));
    }
}
