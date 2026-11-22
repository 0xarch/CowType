// SPDX-License-Identifier: GPL-3.0-or-later
//! Explicit-file diagnostics; private learning edits require an explicit command.
use cowtype::{
    dictionary::Dictionary,
    engine::{wubi::WubiTable, Engine},
    session::{Event, Frame, Key, ResetReason, Session, SessionConfig},
};
use std::{
    error::Error,
    io::{self, BufRead},
    sync::Arc,
};

fn main() {
    if let Err(error) = run() {
        eprintln!("cowtype: {error}");
        std::process::exit(1);
    }
}

fn run() -> Result<(), Box<dyn Error>> {
    let args: Vec<_> = std::env::args().skip(1).collect();
    if args == ["--apply-learning-edits"] {
        cowtype::learning::edits::apply_once(&cowtype::learning::path())?;
        return Ok(());
    }
    if args == ["--version"] {
        println!("cowtype {} (Linux input method)", env!("CARGO_PKG_VERSION"));
        return Ok(());
    }
    if args.first().is_some_and(|s| s == "--help") {
        usage();
        return Ok(());
    }
    match args.first().map(String::as_str) {
        Some("query-wubi") if args.len() == 3 => {
            let table = WubiTable::from_tsv(&std::fs::read(&args[1])?)?;
            for candidate in table.value.query(&args[2]) {
                println!(
                    "{}\t{}\t{}",
                    candidate.consumed_bytes,
                    candidate.reading,
                    candidate.text.escape_debug()
                );
            }
        }
        Some("bench" | "bench-features") if args.len() == 4 => {
            let iterations: usize = args[3].parse()?;
            if !(1..=10000).contains(&iterations) {
                return Err("iterations must be between 1 and 10000".into());
            }
            let start = std::time::Instant::now();
            let engine = load(&args[1])?;
            let load_ms = start.elapsed().as_secs_f64() * 1000.0;
            let mut times = Vec::with_capacity(iterations);
            let mut count = 0;
            for _ in 0..iterations {
                let start = std::time::Instant::now();
                count = std::hint::black_box(if args[0] == "bench-features" {
                    engine.query_configured(&args[2], true, true, true, true)
                } else {
                    engine.query(&args[2])
                })
                .len();
                times.push(start.elapsed().as_secs_f64() * 1000.0);
            }
            times.sort_by(f64::total_cmp);
            println!("load_ms={load_ms:.2} query_p50_ms={:.4} query_p95_ms={:.4} query_max_ms={:.4} candidates={count} iterations={iterations}", times[iterations/2], times[(iterations*95/100).min(iterations-1)], times[iterations-1]);
        }
        Some("query" | "query-features") if args.len() == 3 => {
            let engine = load(&args[1])?;
            for candidate in if args[0] == "query-features" {
                engine.query_configured(&args[2], true, true, true, true)
            } else {
                engine.query(&args[2])
            } {
                println!(
                    "{}\t{}\t{}",
                    candidate.consumed_bytes,
                    candidate.reading,
                    candidate.text.escape_debug()
                );
            }
        }
        Some("replay") if args.len() == 2 => {
            let engine = if args[1] == "-" {
                None
            } else {
                Some(Arc::new(load(&args[1])?))
            };
            let mut session = Session::new(engine, SessionConfig::default());
            for line in io::stdin().lock().lines() {
                let line = line?;
                if let Some(text) = line.strip_prefix("type ") {
                    // Commands model only composition letters/apostrophes. Application-side
                    // passthrough cannot be simulated as committed input by this CLI.
                    if !text
                        .chars()
                        .all(|ch| ch.is_ascii_alphabetic() || ch == '\'')
                    {
                        return Err(
                            "type accepts only ASCII letters/apostrophes; use key commands".into(),
                        );
                    }
                    for ch in text.chars() {
                        print_frame(session.key(Event::press(Key::Character(ch))));
                    }
                } else if let Some(index) = line.strip_prefix("select ") {
                    print_frame(session.select(index.parse()?));
                } else if let Some(index) = line.strip_prefix("choose ") {
                    print_frame(session.choose(index.parse()?));
                } else if line == "focus-loss" {
                    print_frame(session.reset(ResetReason::FocusLost));
                } else if line == "deactivate" {
                    print_frame(session.reset(ResetReason::InputMethodSwitch));
                } else {
                    let key = match line.as_str() {
                        "space" => Key::Space,
                        "enter" => Key::Enter,
                        "esc" => Key::Escape,
                        "backspace" => Key::Backspace,
                        "delete" => Key::Delete,
                        "left" => Key::Left,
                        "right" => Key::Right,
                        "home" => Key::Home,
                        "end" => Key::End,
                        "next" => Key::NextCandidate,
                        "previous" => Key::PreviousCandidate,
                        "page-next" => Key::PageDown,
                        "page-previous" => Key::PageUp,
                        "toggle" => Key::ToggleMode,
                        _ => return Err(format!("unknown replay command: {line:?}").into()),
                    };
                    print_frame(session.key(Event::press(key)));
                }
            }
        }
        _ => {
            usage();
            return Err("invalid arguments".into());
        }
    }
    Ok(())
}

fn load(path: &str) -> Result<Engine, Box<dyn Error>> {
    let report = match Dictionary::from_tsv(&std::fs::read(path)?) {
        Ok(report) => report,
        Err(error) => {
            for diagnostic in &error.diagnostics {
                eprintln!("line {}: {}", diagnostic.line, diagnostic.reason);
            }
            return Err(Box::new(error));
        }
    };
    for diagnostic in report.diagnostics {
        eprintln!("line {}: {}", diagnostic.line, diagnostic.reason);
    }
    Ok(Engine::new(report.value))
}
fn print_frame(frame: Frame) {
    println!(
        "handled={} commit={:?} preedit={:?} cursor={} page={} total={} candidates={:?}",
        frame.handled,
        frame.commit,
        frame.preedit,
        frame.preedit_cursor_bytes,
        frame.page_index,
        frame.candidate_total,
        frame.candidates.iter().map(|c| &c.text).collect::<Vec<_>>()
    );
}
fn usage() {
    println!("CowType experimental Linux input engine (explicit-file CLI)\n\
        cowtype --version\n\
        cowtype query-features <dictionary.tsv> <input>（组句、纠错、简拼开启）\n\
        cowtype query <dictionary.tsv> <input>\n\
        cowtype query-wubi <dictionary.tsv> <code>\n\
        cowtype bench <dictionary.tsv> <input> <iterations>\n\
        cowtype replay <dictionary.tsv|->   # read documented commands from stdin; '-' means no engine\n\
        See docs/USAGE.md for packaged dictionaries and current limitations; docs/BUILD.md for development commands.");
}
