use thiserror::Error;

#[derive(Error, Debug)]
pub enum InternalError {
    #[error("Io error: {0}")]
    Io(#[from] std::io::Error),
    #[error("Notify Error")]
    Notify(#[from] notify::Error),
    #[error("Generic error {text})")]
    GenericError { text: String },
}

pub type InternalResult<T> = std::result::Result<T, InternalError>;
