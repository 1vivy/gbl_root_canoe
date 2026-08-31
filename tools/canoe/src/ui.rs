use std::io::{self, BufRead, Write};

use crate::error::CanoeError;

pub fn step(text: &str) {
    println!("\n[*] {text}");
    let _ = io::stdout().flush();
}

pub fn note_to<W: Write + ?Sized>(writer: &mut W, text: &str) -> Result<(), CanoeError> {
    writeln!(writer, "    {text}")?;
    writer.flush()?;
    Ok(())
}

pub fn note(text: &str) {
    let mut stdout = io::stdout().lock();
    let _ = note_to(&mut stdout, text);
}

pub fn warn(text: &str) {
    eprintln!("    WARNING: {text}");
    let _ = io::stderr().flush();
}

pub fn emit(text: &str) {
    println!("{text}");
    let _ = io::stdout().flush();
}

fn read_answer<R: BufRead + ?Sized, W: Write + ?Sized>(
    reader: &mut R,
    writer: &mut W,
    question: &str,
) -> Result<String, CanoeError> {
    write!(writer, "{question}")?;
    writer.flush()?;
    let mut answer = String::new();
    let read = reader.read_line(&mut answer)?;
    if read == 0 {
        return Err(CanoeError::message("input closed"));
    }
    Ok(answer.trim().to_ascii_lowercase())
}


pub fn ask_choice<R: BufRead + ?Sized, W: Write + ?Sized>(
    reader: &mut R,
    writer: &mut W,
    question: &str,
    choices: &[&str],
    default: Option<&str>,
) -> Result<String, CanoeError> {
    let options = choices.join("/");
    let suffix = default.map_or_else(String::new, |value| format!(" [{value}]"));
    loop {
        let answer = read_answer(reader, writer, &format!("{question} ({options}){suffix}: "))?;
        let selected = if answer.is_empty() {
            default.unwrap_or_default().to_owned()
        } else {
            answer
        };
        if choices.iter().any(|choice| *choice == selected) {
            return Ok(selected);
        }
        warn(&format!("choose one of: {options}"));
    }
}

pub fn ask_yes_no(question: &str, default: bool) -> Result<bool, CanoeError> {
    let stdin = io::stdin();
    let stdout = io::stdout();
    ask_yes_no_from(&mut stdin.lock(), &mut stdout.lock(), question, default)
}

pub fn ask_yes_no_from<R: BufRead + ?Sized, W: Write + ?Sized>(
    reader: &mut R,
    writer: &mut W,
    question: &str,
    default: bool,
) -> Result<bool, CanoeError> {
    let suffix = if default { "[Y/n]" } else { "[y/N]" };
    loop {
        let answer = read_answer(reader, writer, &format!("{question} {suffix}: "))?;
        if answer.is_empty() {
            return Ok(default);
        }
        match answer.as_str() {
            "y" | "yes" => return Ok(true),
            "n" | "no" => return Ok(false),
            _ => warn("answer yes or no"),
        }
    }
}

pub fn run_entry<F>(prog: &str, run: F, argv: &[String]) -> i32
where
    F: FnOnce(&[String]) -> Result<(), CanoeError>,
{
    match run(argv) {
        Ok(()) => 0,
        Err(CanoeError::Message(message)) => {
            eprintln!("{prog}: error: {message}");
            1
        }
        Err(CanoeError::Io(error)) => {
            eprintln!("{prog}: error: {error}");
            1
        }
    }
}
