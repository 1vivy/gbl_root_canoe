use std::fmt::{Display, Formatter};
use std::io;

#[derive(Debug)]
pub enum CanoeError {
    Message(String),
    Io(io::Error),
}

impl CanoeError {
    pub fn message(text: impl Into<String>) -> Self {
        Self::Message(text.into())
    }
}

impl Display for CanoeError {
    fn fmt(&self, formatter: &mut Formatter<'_>) -> std::fmt::Result {
        match self {
            Self::Message(text) => formatter.write_str(text),
            Self::Io(error) => Display::fmt(error, formatter),
        }
    }
}

impl std::error::Error for CanoeError {
    fn source(&self) -> Option<&(dyn std::error::Error + 'static)> {
        match self {
            Self::Message(_) => None,
            Self::Io(error) => Some(error),
        }
    }
}

impl From<io::Error> for CanoeError {
    fn from(error: io::Error) -> Self {
        Self::Io(error)
    }
}
