use std::env;
use std::fs;
use std::process::ExitCode;

fn main() -> ExitCode {
    let Some(path) = env::args_os().nth(1) else {
        eprintln!("usage: vbmeta_inspect <vbmeta.img>");
        return ExitCode::FAILURE;
    };
    let vbmeta = match fs::read(&path) {
        Ok(vbmeta) => vbmeta,
        Err(error) => {
            eprintln!("read vbmeta: {error}");
            return ExitCode::FAILURE;
        }
    };
    match mode2_profile::inspect_vbmeta(&vbmeta) {
        Ok(inspection) => {
            println!("{inspection:#?}");
            ExitCode::SUCCESS
        }
        Err(error) => {
            eprintln!("inspect vbmeta: {error:?}: {error}");
            ExitCode::FAILURE
        }
    }
}
