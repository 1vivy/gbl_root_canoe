use std::fs;
use std::process::Command;

use mode2_profile::{MIN_MEDIA_BYTES, Mode, StoreError, mode_read, mode_write};
use tempfile::tempdir;

const RECORD_BYTES: usize = 1024;
const MODE_DISTANCE: usize = 3072;
const DEFAULT_DISTANCE: usize = 2048;
const CUSTOM_DISTANCE: usize = 1024;

fn fixture(fill: u8) -> (tempfile::TempDir, std::path::PathBuf) {
    let directory = tempdir().expect("temporary directory");
    let path = directory.path().join("efisp.img");
    fs::write(&path, vec![fill; MIN_MEDIA_BYTES as usize]).expect("write ESP fixture");
    (directory, path)
}

fn assert_round_trip_preserves_legacy_records(block_size: u64) {
    // Given: old default/custom records occupy the final two KiB.
    let (_directory, path) = fixture(0x5a);
    let media_bytes = MIN_MEDIA_BYTES as usize;
    let mut seeded = fs::read(&path).expect("read ESP fixture");
    seeded[media_bytes - DEFAULT_DISTANCE..media_bytes - CUSTOM_DISTANCE].fill(0xd1);
    seeded[media_bytes - CUSTOM_DISTANCE..].fill(0xc2);
    fs::write(&path, &seeded).expect("seed legacy records");

    // When: Mode 2 is written through the requested logical-block geometry.
    mode_write(&path, MIN_MEDIA_BYTES, block_size, Mode::KmSpssProfile)
        .expect("write preferred mode");

    // Then: only the fixed mode record changes and it rereads as non-defaulted.
    let actual = fs::read(&path).expect("read updated ESP fixture");
    assert_eq!(
        &actual[..media_bytes - MODE_DISTANCE],
        &seeded[..media_bytes - MODE_DISTANCE]
    );
    assert_eq!(
        &actual[media_bytes - DEFAULT_DISTANCE..],
        &seeded[media_bytes - DEFAULT_DISTANCE..]
    );
    assert_eq!(
        mode_read(&path, MIN_MEDIA_BYTES, block_size).expect("read preferred mode"),
        mode2_profile::ModeRead {
            mode: Mode::KmSpssProfile,
            defaulted: false,
        }
    );
}

#[test]
fn mode_round_trip_preserves_legacy_records_with_512_byte_blocks() {
    assert_round_trip_preserves_legacy_records(512);
}

#[test]
fn mode_round_trip_preserves_legacy_records_with_4096_byte_blocks() {
    assert_round_trip_preserves_legacy_records(4096);
}

#[test]
fn blank_mode_record_defaults_to_mode_one() {
    let (_directory, path) = fixture(0);

    let actual = mode_read(&path, MIN_MEDIA_BYTES, 512).expect("read blank record");

    assert_eq!(actual.mode, Mode::AblFakeLocked);
    assert!(actual.defaulted);
}

#[test]
fn mode_read_cli_defaults_blank_record_to_mode_one() {
    // Given: a valid-size ESP fixture whose preferred-mode tail is blank.
    let (_directory, path) = fixture(0);
    let partition_bytes = MIN_MEDIA_BYTES.to_string();

    // When: the real mode2_profile mode-read command reads the fixture.
    let output = Command::new(env!("CARGO_BIN_EXE_mode2_profile"))
        .args(["mode-read", "--device"])
        .arg(&path)
        .arg("--partition-bytes")
        .arg(&partition_bytes)
        .args(["--block-size", "512"])
        .output()
        .expect("run mode2_profile mode-read");

    // Then: the CLI reports the safe default using its machine-readable surface.
    assert!(output.status.success());
    assert_eq!(output.stdout, b"MODE=1|MODE_DEFAULTED=1\n");
}

#[test]
fn mode_read_cli_defaults_malformed_record_to_mode_one() {
    // Given: a record with a valid prefix but an invalid mode/trailing byte.
    let (_directory, path) = fixture(0);
    let mut bytes = fs::read(&path).expect("read ESP fixture");
    let start = bytes.len() - MODE_DISTANCE;
    bytes[start..start + 9].copy_from_slice(b"SFBM1|2\0X");
    fs::write(&path, bytes).expect("write malformed record");
    let partition_bytes = MIN_MEDIA_BYTES.to_string();

    // When: the real CLI reads that malformed record.
    let output = Command::new(env!("CARGO_BIN_EXE_mode2_profile"))
        .args(["mode-read", "--device"])
        .arg(&path)
        .arg("--partition-bytes")
        .arg(&partition_bytes)
        .args(["--block-size", "512"])
        .output()
        .expect("run mode2_profile mode-read");

    // Then: malformed persistence is surfaced as the same safe default.
    assert!(output.status.success());
    assert_eq!(output.stdout, b"MODE=1|MODE_DEFAULTED=1\n");
}

#[test]
fn malformed_mode_record_defaults_to_mode_one() {
    // Given: the fixed mode record has the right prefix but forbidden trailing data.
    let (_directory, path) = fixture(0);
    let mut bytes = fs::read(&path).expect("read ESP fixture");
    let start = bytes.len() - MODE_DISTANCE;
    bytes[start..start + 9].copy_from_slice(b"SFBM1|2\0X");
    fs::write(&path, bytes).expect("write malformed record");

    // When: the helper reads preferred mode.
    let actual = mode_read(&path, MIN_MEDIA_BYTES, 512).expect("read malformed record");

    // Then: malformed data selects the safe Mode 1 default.
    assert_eq!(actual.mode, Mode::AblFakeLocked);
    assert!(actual.defaulted);
}

#[test]
fn invalid_mode_and_undersized_media_are_rejected() {
    // Given: a valid-size fixture and a separate undersized fixture.
    let (_directory, path) = fixture(0);
    let short_directory = tempdir().expect("temporary directory");
    let short_path = short_directory.path().join("short.img");
    fs::write(&short_path, vec![0; RECORD_BYTES * 4]).expect("write short fixture");

    // When: mode parsing and media reading receive invalid boundaries.
    let invalid_mode = Mode::try_from(3);
    let undersized = mode_read(&short_path, (RECORD_BYTES * 4) as u64, 512);

    // Then: both boundaries fail instead of writing or inventing a fallback store.
    assert!(matches!(
        invalid_mode,
        Err(StoreError::InvalidMode { actual: 3 })
    ));
    assert!(matches!(undersized, Err(StoreError::MediaTooSmall { .. })));
    let oversized_block = mode_read(&path, MIN_MEDIA_BYTES, MIN_MEDIA_BYTES + 1);
    let non_integral_geometry = mode_read(&path, MIN_MEDIA_BYTES, 1000);
    assert!(matches!(oversized_block, Err(StoreError::InvalidGeometry)));
    assert!(matches!(
        non_integral_geometry,
        Err(StoreError::InvalidGeometry)
    ));
    assert_eq!(
        fs::metadata(path).expect("valid fixture metadata").len(),
        MIN_MEDIA_BYTES
    );
}
