// SPDX-License-Identifier: GPL-3.0-or-later
//! PulseAudio/PipeWire-Pulse capture, explicit argv; never persists PCM.
use crate::{asr::AsrChannel, paraformer::ParaformerChannel, session::Lifecycle};
use anyhow::{ensure, Context, Result};
use std::{
    io::Read,
    os::{fd::AsRawFd, unix::process::CommandExt},
    process::{Child, Command, Stdio},
    time::{Duration, Instant},
};
pub(crate) fn log(_message: &str) { /* Metadata logging deliberately disabled in the host. */
}
struct Recorder(Child);
impl Drop for Recorder {
    fn drop(&mut self) {
        let _ = self.0.kill();
        let _ = self.0.wait();
    }
}
pub fn record(
    channel: &mut ParaformerChannel,
    controls: Option<(
        &std::sync::mpsc::Receiver<crate::session::control::Command>,
        u64,
    )>,
) -> Result<()> {
    if let Some((receiver, id)) = controls {
        if let Some(command) = crate::session::control::next(receiver, id) {
            crate::emit(if command == "stop" { 'F' } else { 'D' }, "");
            return Ok(());
        }
    }
    // A stop during graph loading must not open a microphone after the key was released.
    // Before readiness no audio exists to finalize. Any queued control ends this attempt.
    let mut control = libc::pollfd {
        fd: 0,
        events: libc::POLLIN | libc::POLLHUP,
        revents: 0,
    };
    if controls.is_none() && unsafe { libc::poll(&mut control, 1, 0) } > 0 {
        let mut bytes = [0u8; 256];
        let n = unsafe { libc::read(0, bytes.as_mut_ptr().cast(), bytes.len()) };
        if n > 0 && bytes[..n as usize].starts_with(b"stop") {
            crate::emit('F', "");
        }
        return Ok(());
    }
    let mut command = Command::new("parecord");
    command
        .args([
            "--raw",
            "--format=s16le",
            "--rate=16000",
            "--channels=1",
            "--latency-msec=40",
            "--client-name=CowType",
            "--stream-name=Explicit voice input",
        ])
        .stdin(Stdio::null())
        .stdout(Stdio::piped())
        .stderr(Stdio::null());
    if let Some(device) = std::env::var_os("COWTYPE_VOICE_DEVICE").filter(|v| !v.is_empty()) {
        command.arg("--device").arg(device);
    }
    // Kill the recorder if its isolated worker is cancelled/crashes. No shell is used.
    unsafe {
        command.pre_exec(|| {
            if libc::prctl(libc::PR_SET_PDEATHSIG, libc::SIGKILL) != 0 {
                return Err(std::io::Error::last_os_error());
            }
            if libc::getppid() == 1 {
                return Err(std::io::Error::other("voice parent exited"));
            }
            Ok(())
        });
    }
    let mut recorder = Recorder(
        command
            .spawn()
            .context(crate::i18n::tr("message.8c1f2492bb4b"))?,
    );
    let mut audio = recorder
        .0
        .stdout
        .take()
        .context(crate::i18n::tr("message.f84fffe2c588"))?;
    let fd = audio.as_raw_fd();
    unsafe {
        ensure!(
            libc::fcntl(
                fd,
                libc::F_SETFL,
                libc::fcntl(fd, libc::F_GETFL) | libc::O_NONBLOCK
            ) >= 0,
            crate::i18n::tr("message.5adaeff16361")
        );
    }
    crate::emit('R', crate::i18n::tr("message.81f954a8dff5"));
    let start = Instant::now();
    let mut lifecycle = Lifecycle::default();
    let mut command = String::new();
    let mut carry = None;
    let mut sent = String::new();
    let mut ending = None;
    let mut meter = crate::audio::LevelMeter::default();
    loop {
        let mut polls = [
            libc::pollfd {
                fd: if controls.is_some() { -1 } else { 0 },
                events: libc::POLLIN | libc::POLLHUP,
                revents: 0,
            },
            libc::pollfd {
                fd,
                events: libc::POLLIN | libc::POLLHUP,
                revents: 0,
            },
        ];
        let polled = unsafe { libc::poll(polls.as_mut_ptr(), polls.len() as _, 25) };
        if polled < 0 {
            if std::io::Error::last_os_error().kind() == std::io::ErrorKind::Interrupted {
                continue;
            }
            anyhow::bail!(crate::i18n::tr("message.a68c094df355"));
        }
        if polls[0].revents != 0 {
            let mut buffer = [0u8; 256];
            let n = unsafe { libc::read(0, buffer.as_mut_ptr().cast(), buffer.len()) };
            if n <= 0 {
                return Ok(());
            }
            command.push_str(std::str::from_utf8(&buffer[..n as usize]).unwrap_or("cancel\n"));
            ensure!(
                command.len() < 1024,
                crate::i18n::tr("message.171805c518e6")
            );
            while let Some(end) = command.find('\n') {
                let line: String = command.drain(..=end).collect();
                lifecycle.command(&line, Instant::now());
            }
        }
        if let Some((receiver, id)) = controls {
            while let Some(command) = crate::session::control::next(receiver, id) {
                lifecycle.command(&command, Instant::now());
                if lifecycle.cancelled() {
                    break;
                }
            }
        }
        if lifecycle.cancelled() {
            crate::emit('D', "");
            return Ok(());
        }
        if start.elapsed() >= Duration::from_secs(120) {
            lifecycle.command("stop", Instant::now());
        }
        if lifecycle.should_stop(Instant::now()) && ending.is_none() {
            unsafe {
                libc::kill(recorder.0.id() as _, libc::SIGINT);
            }
            ending = Some(Instant::now());
        }
        if polls[1].revents != 0 {
            let mut bytes = [0u8; 3200];
            match audio.read(&mut bytes) {
                Ok(0) => {
                    if ending.is_none() {
                        anyhow::bail!(crate::i18n::tr("message.a8b2debdc671"));
                    }
                    break;
                }
                Ok(n) => {
                    let mut pcm = Vec::with_capacity(n / 2 + 1);
                    let mut i = 0;
                    if let Some(first) = carry.take() {
                        pcm.push(i16::from_le_bytes([first, bytes[0]]));
                        i = 1;
                    }
                    while i + 1 < n {
                        pcm.push(i16::from_le_bytes([bytes[i], bytes[i + 1]]));
                        i += 2;
                    }
                    if i < n {
                        carry = Some(bytes[i]);
                    }
                    if ending.is_none() {
                        meter.push(&pcm, |level| crate::emit('V', &level.to_string()));
                    }
                    channel.push(&pcm);
                    channel.refresh_result()?;
                    if let Some(text) = channel.partial() {
                        if text != sent {
                            sent = text.into();
                            crate::emit('P', text);
                        }
                    }
                }
                Err(e) if e.kind() == std::io::ErrorKind::WouldBlock => (),
                Err(_) => anyhow::bail!(crate::i18n::tr("message.2b0bac73ff4a")),
            }
        }
        if ending.is_some_and(|t: Instant| t.elapsed() > Duration::from_secs(2)) {
            anyhow::bail!(crate::i18n::tr("message.6d29fedd26ae"));
        }
    }
    if lifecycle.finalize() {
        crate::emit('F', channel.finish_result()?.as_deref().unwrap_or(""));
    }
    Ok(())
}
