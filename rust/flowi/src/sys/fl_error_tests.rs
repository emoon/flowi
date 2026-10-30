//! Decode behaviour of FlError.

use super::FlError;

#[test]
fn decodes_success_known_and_catch_all() {
    assert_eq!(FlError::from_status(0), Ok(()));

    assert_eq!(FlError::from_status(1), Err(FlError::InvalidArgument));
    assert_eq!(FlError::from_status(100), Err(FlError::OutOfMemory));
    assert_eq!(
        FlError::from_status(203),
        Err(FlError::PluginVersionMismatch)
    );
    assert_eq!(FlError::from_code(999), FlError::GenericError);

    // An unrecognised code decodes to the catch-all carrying the raw value; it must never panic.
    assert_eq!(FlError::from_status(4242), Err(FlError::Unknown(4242)));
    assert_eq!(FlError::from_code(-7), FlError::Unknown(-7));
}
