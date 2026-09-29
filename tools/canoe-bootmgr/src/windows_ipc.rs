use std::{
    ffi::OsStr,
    fs::OpenOptions,
    io::BufReader,
    os::windows::{
        ffi::OsStrExt,
        io::{AsRawHandle, FromRawHandle, OwnedHandle},
    },
    path::Path,
};

use windows_sys::Win32::{
    Foundation::INVALID_HANDLE_VALUE,
    System::{
        JobObjects::{AssignProcessToJobObject, OpenJobObjectW},
        SystemServices::JOB_OBJECT_ASSIGN_PROCESS,
        Threading::GetCurrentProcess,
    },
};

use crate::cli::Cli;

/// Serve the normal JSONL protocol over the GUI-owned Windows named pipe.
///
/// The helper joins the GUI-created containment job before opening the pipe, so
/// the GUI cannot dispatch a request until the complete elevated process tree
/// is contained. The opened job handle is intentionally dropped after the
/// assignment; the GUI remains its sole owner for kill-on-close semantics.
pub(crate) fn run(cli: &Cli, pipe: &Path, job_name: &Path) -> i32 {
    if let Err(error) = join_gui_job(job_name.as_os_str()) {
        eprintln!("canoe-bootmgr: could not join GUI containment job: {error}");
        return 1;
    }
    let pipe = match OpenOptions::new().read(true).write(true).open(pipe) {
        Ok(pipe) => pipe,
        Err(error) => {
            eprintln!("canoe-bootmgr: could not connect to GUI named pipe: {error}");
            return 1;
        }
    };
    let reader = match pipe.try_clone() {
        Ok(reader) => BufReader::new(reader),
        Err(error) => {
            eprintln!("canoe-bootmgr: could not duplicate GUI named pipe: {error}");
            return 1;
        }
    };
    crate::cli_runner::run_jsonl_io(cli, reader, pipe)
}

fn join_gui_job(name: &OsStr) -> Result<(), String> {
    let name = wide(name);
    // SAFETY: the NUL-terminated GUI-provided job name remains live for the call. The DACL
    // grants the elevated user JOB_OBJECT_ASSIGN_PROCESS without exposing a writable job handle.
    let job = unsafe { OpenJobObjectW(JOB_OBJECT_ASSIGN_PROCESS, 0, name.as_ptr()) };
    if job.is_null() || job == INVALID_HANDLE_VALUE {
        return Err(std::io::Error::last_os_error().to_string());
    }
    // SAFETY: OpenJobObjectW returned a single closeable handle owned only in this scope.
    let job = unsafe { OwnedHandle::from_raw_handle(job) };
    // SAFETY: this is the helper's pseudo-handle, which has PROCESS_SET_QUOTA and
    // PROCESS_TERMINATE. The job handle has the explicitly requested JOB_OBJECT_ASSIGN_PROCESS.
    if unsafe { AssignProcessToJobObject(job.as_raw_handle(), GetCurrentProcess()) } == 0 {
        return Err(std::io::Error::last_os_error().to_string());
    }
    Ok(())
}

fn wide(value: impl AsRef<OsStr>) -> Vec<u16> {
    value
        .as_ref()
        .encode_wide()
        .chain(std::iter::once(0))
        .collect()
}
