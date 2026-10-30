//! Content digests - CRC32, SHA1, MD5 and SHA256 over bytes.
//!
//! These are the same implementations flowi's native library uses, so a digest
//! computed here matches one computed there over the same bytes.
//!
//! Two shapes: a one-shot function per algorithm for a buffer already in memory,
//! and [`Sha256`] for the incremental case, where a large file is digested a
//! chunk at a time rather than read whole.
//!
//! ```no_run
//! # fn read_chunk(_: &mut [u8]) -> usize { 0 }
//! let mut digest = flowi::Sha256::new();
//! let mut buffer = [0u8; 64 * 1024];
//! loop {
//!     let read = read_chunk(&mut buffer);
//!     if read == 0 {
//!         break;
//!     }
//!     digest.update(&buffer[..read]);
//! }
//! let hex = digest.finish_hex();
//! ```

use std::os::raw::c_char;

use crate::sys;

/// Length of a SHA1 digest in bytes.
pub const SHA1_LEN: usize = 20;

/// Length of an MD5 digest in bytes.
pub const MD5_LEN: usize = 16;

/// Length of a SHA256 digest in bytes.
pub const SHA256_LEN: usize = 32;

/// Bytes fl_hash_sha256_hex / fl_hash_sha256_digest_hex write: 64 hex characters
/// plus the NUL they terminate with.
const SHA256_HEX_BUFFER: usize = 65;

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

/// CRC32 of data (zlib's polynomial).
#[inline]
pub fn crc32(data: &[u8]) -> u32 {
    // SAFETY: the call reads exactly the data.len() bytes the slice owns.
    unsafe { sys::fl_hash_crc32(data.as_ptr(), data.len() as u64) }
}

/// SHA1 of data.
#[inline]
pub fn sha1(data: &[u8]) -> [u8; SHA1_LEN] {
    let mut out = [0u8; SHA1_LEN];
    // SAFETY: the call reads data.len() bytes from the slice and writes exactly
    // the 20 bytes out holds.
    unsafe { sys::fl_hash_sha1(data.as_ptr(), data.len() as u64, out.as_mut_ptr()) };
    out
}

/// MD5 of data.
#[inline]
pub fn md5(data: &[u8]) -> [u8; MD5_LEN] {
    let mut out = [0u8; MD5_LEN];
    // SAFETY: the call reads data.len() bytes from the slice and writes exactly
    // the 16 bytes out holds.
    unsafe { sys::fl_hash_md5(data.as_ptr(), data.len() as u64, out.as_mut_ptr()) };
    out
}

/// SHA256 of data as a 64-character lowercase hex string.
#[inline]
pub fn sha256_hex(data: &[u8]) -> String {
    let mut hex = [0u8; SHA256_HEX_BUFFER];
    // SAFETY: the call reads data.len() bytes from the slice and writes a
    // NUL-terminated 64-character string, which is what hex is sized for.
    unsafe {
        sys::fl_hash_sha256_hex(
            data.as_ptr(),
            data.len() as u64,
            hex.as_mut_ptr().cast::<c_char>(),
        );
    }
    hex_string(&hex)
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

/// An incremental SHA256: feed it bytes with [`update`](Sha256::update), then
/// take the digest with [`finish`](Sha256::finish) or
/// [`finish_hex`](Sha256::finish_hex).
///
/// Finishing consumes the hash, so it cannot be fed afterwards.
pub struct Sha256 {
    context: sys::Sha256Context,
}

impl Sha256 {
    /// Start a digest over no bytes.
    #[inline]
    pub fn new() -> Sha256 {
        let mut context = sys::Sha256Context {
            state: [0; 8],
            bitcount: 0,
            data: [0; 64],
            datalen: 0,
        };
        // SAFETY: context is a live, correctly-shaped context this owns.
        unsafe { sys::fl_hash_sha256_init(&mut context) };
        Sha256 { context }
    }

    /// Feed data into the digest.
    #[inline]
    pub fn update(&mut self, data: &[u8]) {
        // SAFETY: the call reads exactly the data.len() bytes the slice owns
        // and folds them into the context this owns.
        unsafe { sys::fl_hash_sha256_update(&mut self.context, data.as_ptr(), data.len() as u64) };
    }

    /// Finish and return the 32-byte digest.
    #[inline]
    pub fn finish(mut self) -> [u8; SHA256_LEN] {
        let mut digest = [0u8; SHA256_LEN];
        // SAFETY: the call writes exactly the 32 bytes digest holds, from the
        // context this owns.
        unsafe { sys::fl_hash_sha256_final(&mut self.context, digest.as_mut_ptr()) };
        digest
    }

    /// Finish and return the digest as a 64-character lowercase hex string.
    #[inline]
    pub fn finish_hex(self) -> String {
        digest_hex(&self.finish())
    }
}

impl Default for Sha256 {
    fn default() -> Sha256 {
        Sha256::new()
    }
}

/// A 32-byte SHA256 digest as a 64-character lowercase hex string.
#[inline]
pub fn digest_hex(digest: &[u8; SHA256_LEN]) -> String {
    let mut hex = [0u8; SHA256_HEX_BUFFER];
    // SAFETY: the call reads the 32 bytes digest holds and writes a
    // NUL-terminated 64-character string, which is what hex is sized for.
    unsafe {
        sys::fl_hash_sha256_digest_hex(digest.as_ptr(), hex.as_mut_ptr().cast::<c_char>());
    }
    hex_string(&hex)
}

/// The NUL-terminated ASCII hex the C side wrote into buffer, as a String.
fn hex_string(buffer: &[u8; SHA256_HEX_BUFFER]) -> String {
    let len = buffer
        .iter()
        .position(|&byte| byte == 0)
        .unwrap_or(SHA256_HEX_BUFFER - 1);
    String::from_utf8_lossy(&buffer[..len]).into_owned()
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

#[cfg(test)]
mod tests {
    use super::*;

    /// The published digests of the empty input, for every algorithm here.
    #[test]
    fn the_empty_input_hashes_to_the_published_digests() {
        assert_eq!(crc32(&[]), 0);
        assert_eq!(
            sha1(&[]),
            [
                0xda, 0x39, 0xa3, 0xee, 0x5e, 0x6b, 0x4b, 0x0d, 0x32, 0x55, 0xbf, 0xef, 0x95, 0x60,
                0x18, 0x90, 0xaf, 0xd8, 0x07, 0x09
            ]
        );
        assert_eq!(
            md5(&[]),
            [
                0xd4, 0x1d, 0x8c, 0xd9, 0x8f, 0x00, 0xb2, 0x04, 0xe9, 0x80, 0x09, 0x98, 0xec, 0xf8,
                0x42, 0x7e
            ]
        );
        assert_eq!(
            sha256_hex(&[]),
            "e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855"
        );
    }

    /// Published non-empty vectors: "123456789" for crc32, "abc" for the rest
    /// (sha256's from FIPS 180-4).
    #[test]
    fn known_vectors_hash_to_their_published_digests() {
        // The CRC-32/ISO-HDLC check value: the defining constant for the variant.
        assert_eq!(crc32(b"123456789"), 0xcbf4_3926);
        assert_eq!(
            sha1(b"abc"),
            [
                0xa9, 0x99, 0x3e, 0x36, 0x47, 0x06, 0x81, 0x6a, 0xba, 0x3e, 0x25, 0x71, 0x78, 0x50,
                0xc2, 0x6c, 0x9c, 0xd0, 0xd8, 0x9d
            ]
        );
        assert_eq!(
            md5(b"abc"),
            [
                0x90, 0x01, 0x50, 0x98, 0x3c, 0xd2, 0x4f, 0xb0, 0xd6, 0x96, 0x3f, 0x7d, 0x28, 0xe1,
                0x7f, 0x72
            ]
        );
        assert_eq!(
            sha256_hex(b"abc"),
            "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad"
        );
    }

    /// Streaming in chunks yields the one-shot digest of the same bytes.
    #[test]
    fn streaming_in_chunks_matches_the_one_shot_digest() {
        let data: Vec<u8> = (0..1000u32).map(|i| (i % 251) as u8).collect();

        let mut digest = Sha256::new();
        for chunk in data.chunks(37) {
            digest.update(chunk);
        }
        assert_eq!(digest.finish_hex(), sha256_hex(&data));

        // Feeding nothing must not disturb the running state either.
        let mut with_empties = Sha256::new();
        with_empties.update(&[]);
        with_empties.update(&data);
        with_empties.update(&[]);
        assert_eq!(with_empties.finish_hex(), sha256_hex(&data));
    }

    /// digest_hex of a raw digest agrees with finish_hex.
    #[test]
    fn rendering_a_digest_matches_finishing_to_hex() {
        let mut digest = Sha256::new();
        digest.update(b"abc");
        let raw = digest.finish();
        assert_eq!(
            digest_hex(&raw),
            "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad"
        );
    }
}
