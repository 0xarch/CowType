// SPDX-License-Identifier: GPL-3.0-or-later
//! Optional local voice worker. stdout is private IPC, never a diagnostic log.
#[allow(dead_code, unused_imports)]
mod asr {
    pub trait AsrChannel: Send {
        fn name(&self) -> &str;
        fn feed(&mut self, pcm: &[i16]);
        fn partial(&self) -> Option<&str>;
        fn finish(&mut self) -> Option<String>;
        fn take_error(&mut self) -> Option<String>;
    }
}
mod audio;
mod capture;
mod i18n;
#[path = "paraformer/runtime/mod.rs"]
#[allow(dead_code, unused_imports, clippy::needless_question_mark)]
mod paraformer;
mod session;
use anyhow::{ensure, Context, Result};
use sha2::{Digest, Sha256};
use std::{
    io::{Read, Write},
    path::{Path, PathBuf},
};
static REQUEST: std::sync::atomic::AtomicU64 = std::sync::atomic::AtomicU64::new(0);
fn emit(kind: char, text: &str) {
    let text: String = text
        .chars()
        .map(|c| if c.is_control() { ' ' } else { c })
        .take(16000)
        .collect();
    let id = REQUEST.load(std::sync::atomic::Ordering::Relaxed);
    if id == 0 {
        let _ = writeln!(std::io::stdout(), "{kind}\t{text}");
    } else {
        let _ = writeln!(std::io::stdout(), "{id}\t{kind}\t{text}");
    }
    let _ = std::io::stdout().flush();
}
fn verify_source(dir: &Path) -> Result<()> {
    paraformer::check_files(dir)?;
    let path = dir.join("SOURCE.json");
    ensure!(
        path.metadata()?.len() < 1024 * 1024,
        crate::i18n::tr("message.ff19b914000e")
    );
    let value: serde_json::Value = serde_json::from_slice(&std::fs::read(path)?)
        .context(crate::i18n::tr("message.90b0f30f9ed4"))?;
    ensure!(
        value["interface"] == "funasr-streaming-paraformer-two-graph",
        crate::i18n::tr("message.97f37ac9ae4a")
    );
    // Only the pinned Apache-2.0 checkpoint supported by the bundled downloader is supported.
    ensure!(
        value["checkpoint_sha256"]
            == "4fdfb48ed4471777c9a511e96a2acae17f77cac9d709cc756634622769192a64",
        crate::i18n::tr("message.5f53e12c4429")
    );
    for name in [
        "model.onnx",
        "decoder.onnx",
        "config.yaml",
        "tokens.json",
        "am.mvn",
    ] {
        let expected = value["files"][name]
            .as_str()
            .context(crate::i18n::tr("message.37a87a9cf26d"))?;
        let mut hash = Sha256::new();
        let mut file = std::fs::File::open(dir.join(name))?;
        let mut buffer = [0u8; 65536];
        loop {
            let n = file.read(&mut buffer)?;
            if n == 0 {
                break;
            }
            hash.update(&buffer[..n]);
        }
        ensure!(
            format!("{:x}", hash.finalize()) == expected,
            "{}",
            crate::i18n::tr("message.d984cc090e22").replace("{name}", name)
        );
    }
    Ok(())
}
fn serve() -> Result<()> {
    let commands = session::control::reader();
    let mut verified = None;
    // Keep only the model/ORT cache between utterances, never frontend/audio/token state.
    while let Ok(command) = commands.recv() {
        if command.action != "start" && command.action != "prepare" {
            continue;
        }
        REQUEST.store(command.id, std::sync::atomic::Ordering::Relaxed);
        let result = (|| -> Result<()> {
            let dir = paraformer::default_dir();
            let names = [
                "SOURCE.json",
                "model.onnx",
                "decoder.onnx",
                "config.yaml",
                "tokens.json",
                "am.mvn",
            ];
            let stamps = names
                .iter()
                .map(|name| {
                    let metadata = std::fs::metadata(dir.join(name))?;
                    Ok((metadata.len(), metadata.modified()?))
                })
                .collect::<std::io::Result<Vec<_>>>()?;
            let fingerprint = (dir.clone(), stamps);
            if verified.as_ref() != Some(&fingerprint) {
                verify_source(&dir)?;
                verified = Some(fingerprint);
            }
            let mut channel = paraformer::ParaformerChannel::new(&dir)?;
            if command.action == "prepare" {
                emit('D', "");
                Ok(())
            } else {
                capture::record(&mut channel, Some((&commands, command.id)))
            }
        })();
        if let Err(error) = result {
            emit('E', &format!("{error:#}"));
        }
        // Reclaim free frontend/graph-build allocations while keeping the shared ORT model.
        #[cfg(target_env = "gnu")]
        unsafe {
            libc::malloc_trim(0);
        }
    }
    Ok(())
}
fn run() -> Result<()> {
    let mut args = std::env::args().skip(1);
    let mut dir = None;
    let mut pcm = None;
    let mut check = false;
    let mut resident = false;
    while let Some(arg) = args.next() {
        match arg.as_str() {
            "--dir" => {
                dir = Some(PathBuf::from(
                    args.next()
                        .context(crate::i18n::tr("message.dbcff5ab2a46"))?,
                ))
            }
            "--pcm" => {
                pcm = Some(PathBuf::from(
                    args.next()
                        .context(crate::i18n::tr("message.7e0c52d08c6e"))?,
                ))
            }
            "--check" => check = true,
            "--serve" => resident = true,
            "--version" => {
                println!("cowtype-voice {}", env!("CARGO_PKG_VERSION"));
                return Ok(());
            }
            "--model-url" => {
                println!("{}", paraformer::MODEL_URL);
                return Ok(());
            }
            "--help" => {
                println!("cowtype-voice --serve | [--dir ONNX_DIRECTORY] [--check | --pcm RAW_S16LE_16000_MONO]\nNo model download or microphone access before an explicit invocation. One-shot stdin: stop/cancel; --serve stdin: start/stop/cancel REQUEST_ID. Resident replies: REQUEST_ID TAB R/P/F/E/D TAB text.");
                return Ok(());
            }
            _ => anyhow::bail!(crate::i18n::tr("message.fd6955acd5d1")),
        }
    }
    if resident {
        ensure!(
            dir.is_none() && pcm.is_none() && !check,
            crate::i18n::tr("message.a02910fcb9c1")
        );
        return serve();
    }
    let dir = dir.unwrap_or_else(paraformer::default_dir);
    verify_source(&dir)?;
    let mut channel = paraformer::ParaformerChannel::new(&dir)?;
    if check {
        println!("{{\"ready\":true,\"backend\":\"onnx-paraformer\",\"sample_rate\":16000}}");
        return Ok(());
    }
    if let Some(path) = pcm {
        let mut file = std::fs::File::open(path)?;
        let mut bytes = Vec::new();
        std::io::Read::by_ref(&mut file)
            .take(16000 * 2 * 120 + 1)
            .read_to_end(&mut bytes)?;
        ensure!(
            bytes.len() <= 16000 * 2 * 120 && bytes.len() % 2 == 0,
            crate::i18n::tr("message.5b95106f776a")
        );
        for block in bytes.chunks(3200) {
            let pcm: Vec<_> = block
                .chunks_exact(2)
                .map(|b| i16::from_le_bytes([b[0], b[1]]))
                .collect();
            channel.push(&pcm);
            channel.refresh_result()?;
        }
        emit('F', channel.finish_result()?.as_deref().unwrap_or(""));
        return Ok(());
    }
    capture::record(&mut channel, None)
}
fn main() {
    // Backend errors must reach the owner field through the bounded IPC channel.
    // The parent closes the channel on cancel; never dump recognized text to stderr.
    std::panic::set_hook(Box::new(|_| {}));
    let result = std::panic::catch_unwind(run);
    match result {
        Ok(Ok(())) => (),
        Ok(Err(error)) => {
            emit('E', &format!("{error:#}"));
            std::process::exit(1);
        }
        Err(_) => {
            emit('E', crate::i18n::tr("message.e6575bb6801f"));
            std::process::exit(1);
        }
    }
}

#[cfg(test)]
mod provenance_tests {
    use super::*;
    #[test]
    fn missing_wrong_source_and_changed_graph_are_rejected() {
        let directory =
            std::env::temp_dir().join(format!("cowtype-voice-source-test-{}", std::process::id()));
        let _ = std::fs::remove_dir_all(&directory);
        std::fs::create_dir_all(&directory).unwrap();
        assert!(verify_source(&directory).is_err());
        let mut files = serde_json::Map::new();
        for name in [
            "model.onnx",
            "decoder.onnx",
            "config.yaml",
            "tokens.json",
            "am.mvn",
        ] {
            std::fs::write(directory.join(name), b"synthetic provenance fixture").unwrap();
            files.insert(
                name.into(),
                format!("{:x}", Sha256::digest(b"synthetic provenance fixture")).into(),
            );
        }
        let mut source = serde_json::json!({"interface":"funasr-streaming-paraformer-two-graph", "checkpoint_sha256":"4fdfb48ed4471777c9a511e96a2acae17f77cac9d709cc756634622769192a64", "files":files});
        let manifest = directory.join("SOURCE.json");
        std::fs::write(&manifest, source.to_string()).unwrap();
        assert!(verify_source(&directory).is_ok());
        std::fs::write(directory.join("decoder.onnx"), b"changed graph").unwrap();
        assert!(verify_source(&directory).is_err());
        source["checkpoint_sha256"] = "unrecognized".into();
        std::fs::write(manifest, source.to_string()).unwrap();
        assert!(verify_source(&directory).is_err());
        std::fs::remove_dir_all(directory).unwrap();
    }
}
