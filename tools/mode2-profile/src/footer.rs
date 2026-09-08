//! One checked AVB footer layout shared by inspection, extraction and image
//! preparation. A footer refers to vbmeta by offset, independently of the
//! original payload length; alignment padding can lie between the two.
use crate::DeriveError;
pub const SIZE: usize = 64;
#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub struct Footer {
    pub original_image_size: usize,
    pub vbmeta_offset: usize,
    pub vbmeta_size: usize,
}
impl Footer {
    pub fn parse(image: &[u8]) -> Result<Option<Self>, DeriveError> {
        let Some(start) = image.len().checked_sub(SIZE) else {
            return Ok(None);
        };
        let footer = &image[start..];
        if &footer[..4] != b"AVBf" {
            return Ok(None);
        }
        if u32::from_be_bytes(footer[4..8].try_into().unwrap()) != 1
            || u32::from_be_bytes(footer[8..12].try_into().unwrap()) != 0
            || footer[36..].iter().any(|b| *b != 0)
        {
            return Err(DeriveError::BadFooter);
        }
        let field = |offset| -> Result<usize, DeriveError> {
            usize::try_from(u64::from_be_bytes(
                footer[offset..offset + 8].try_into().unwrap(),
            ))
            .map_err(|_| DeriveError::VbmetaPastImage)
        };
        let result = Self {
            original_image_size: field(12)?,
            vbmeta_offset: field(20)?,
            vbmeta_size: field(28)?,
        };
        let end = result
            .vbmeta_offset
            .checked_add(result.vbmeta_size)
            .ok_or(DeriveError::VbmetaPastImage)?;
        if result.original_image_size > result.vbmeta_offset
            || result.vbmeta_size < 256
            || end > start
        {
            return Err(DeriveError::VbmetaPastImage);
        }
        if image.get(result.vbmeta_offset..result.vbmeta_offset + 4) != Some(b"AVB0") {
            return Err(DeriveError::BadMagic);
        }
        Ok(Some(result))
    }
    pub fn vbmeta<'a>(&self, image: &'a [u8]) -> Result<&'a [u8], DeriveError> {
        let end = self
            .vbmeta_offset
            .checked_add(self.vbmeta_size)
            .ok_or(DeriveError::VbmetaPastImage)?;
        image
            .get(self.vbmeta_offset..end)
            .ok_or(DeriveError::VbmetaPastImage)
    }
}
