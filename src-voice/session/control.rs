// SPDX-License-Identifier: GPL-3.0-or-later
//! Bounded command reader. Idle workers block; no audio device exists while idle.
use anyhow::{bail, Result};
use std::{
    io::{BufRead, Read},
    sync::mpsc::{self, Receiver, TryRecvError},
};
#[derive(Debug, PartialEq, Eq)]
pub struct Command {
    pub id: u64,
    pub action: String,
}
pub fn parse(line: &str) -> Result<Command> {
    let mut fields = line.trim().split_ascii_whitespace();
    let action = fields.next().unwrap_or("");
    let id = fields
        .next()
        .and_then(|v| v.parse::<u64>().ok())
        .unwrap_or(0);
    if id == 0
        || !matches!(action, "start" | "stop" | "cancel" | "prepare")
        || fields.next().is_some()
    {
        bail!(crate::i18n::tr("message.518a5c4b7dd3"));
    }
    Ok(Command {
        id,
        action: action.into(),
    })
}
pub fn reader() -> Receiver<Command> {
    let (sender, receiver) = mpsc::sync_channel(32);
    std::thread::spawn(move || {
        let mut input = std::io::stdin().lock();
        loop {
            let mut bytes = Vec::new();
            match input.by_ref().take(129).read_until(b'\n', &mut bytes) {
                Ok(0) | Err(_) => break,
                _ if bytes.len() > 128 || !bytes.ends_with(b"\n") => break,
                _ => (),
            }
            let Ok(text) = std::str::from_utf8(&bytes) else {
                break;
            };
            let Ok(command) = parse(text) else { break };
            if sender.send(command).is_err() {
                break;
            }
        }
    });
    receiver
}
// During capture only stop/cancel for this generation are actionable.
// The parent serializes start until a terminal acknowledgement arrives.
pub fn next(receiver: &Receiver<Command>, id: u64) -> Option<String> {
    match receiver.try_recv() {
        Ok(command) if command.id == id && command.action != "start" => Some(command.action),
        Ok(_) => Some("cancel".into()),
        Err(TryRecvError::Disconnected) => Some("cancel".into()),
        Err(TryRecvError::Empty) => None,
    }
}
#[cfg(test)]
mod tests {
    use super::*;
    #[test]
    fn generation_and_commands_are_strict() {
        assert_eq!(parse("start 12\n").unwrap().id, 12);
        for invalid in ["start", "start 0", "stop -1", "start 2 extra", "pcm 1"] {
            assert!(parse(invalid).is_err());
        }
        let (sender, receiver) = mpsc::channel();
        sender.send(parse("stop 2").unwrap()).unwrap();
        assert_eq!(next(&receiver, 1).as_deref(), Some("cancel"));
    }
}
