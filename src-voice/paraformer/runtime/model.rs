use anyhow::{ensure, Context, Result};
use ort::{
    session::Session,
    value::{Tensor, TensorElementType},
};
use serde_yaml::Value;
use std::{
    path::{Path, PathBuf},
    sync::{Arc, Mutex, OnceLock},
    time::SystemTime,
};

pub const FILES: &[&str] = &[
    "model.onnx",
    "decoder.onnx",
    "config.yaml",
    "tokens.json",
    "am.mvn",
];
pub const MODEL_URL: &str =
    "https://huggingface.co/funasr/paraformer-zh-streaming/tree/apache-2.0-20260804";

pub fn selection_path() -> PathBuf {
    std::env::var_os("XDG_CONFIG_HOME")
        .map(PathBuf::from)
        .unwrap_or_else(|| {
            PathBuf::from(std::env::var_os("HOME").unwrap_or_default()).join(".config")
        })
        .join("cowtype/voice-model.json")
}

pub fn default_dir() -> PathBuf {
    if let Some(dir) = std::env::var_os("COWTYPE_VOICE_MODEL_DIR").filter(|v| !v.is_empty()) {
        return dir.into();
    }
    if let Ok(text) = std::fs::read_to_string(selection_path()) {
        if let Ok(value) = serde_json::from_str::<serde_json::Value>(&text) {
            if let Some(dir) = value["directory"].as_str().filter(|s| !s.is_empty()) {
                return dir.into();
            }
        }
    }
    std::env::var_os("XDG_DATA_HOME")
        .map(PathBuf::from)
        .unwrap_or_else(|| {
            PathBuf::from(std::env::var_os("HOME").unwrap_or_default()).join(".local/share")
        })
        .join("cowtype/paraformer-zh-streaming")
}

pub fn is_ready(dir: &Path) -> bool {
    check_files(dir).is_ok()
}

pub fn check_files(dir: &Path) -> Result<()> {
    for name in FILES {
        let path = dir.join(name);
        let mut file = std::fs::File::open(&path).with_context(|| {
            format!(
                "{}: {}",
                crate::i18n::tr("voice.paraformer.files"),
                path.display()
            )
        })?;
        use std::io::Read;
        let mut head = [0; 128];
        let n = file.read(&mut head)?;
        ensure!(
            n > 0 && !head[..n].starts_with(b"version https://git-lfs.github.com/spec/"),
            "{}: {}",
            crate::i18n::tr("voice.paraformer.pointer"),
            path.display()
        );
    }
    Ok(())
}

pub struct Model {
    pub sessions: Mutex<(Session, Session)>,
    pub tokens: Vec<String>,
    pub layers: usize,
    pub order: usize,
    pub threshold: f32,
    pub tail: f32,
    pub dither: f32,
    pub dir: PathBuf,
}

fn number(config: &Value, group: &str, key: &str) -> Result<f64> {
    config[group][key].as_f64().with_context(|| {
        format!(
            "{}: {group}.{key}",
            crate::i18n::tr("voice.paraformer.config")
        )
    })
}

fn runtime() -> Result<()> {
    static READY: OnceLock<()> = OnceLock::new();
    static GUARD: Mutex<()> = Mutex::new(());
    let _guard = GUARD
        .lock()
        .map_err(|_| anyhow::anyhow!(crate::i18n::tr("voice.paraformer.lock")))?;
    if READY.get().is_some() {
        return Ok(());
    }
    let path = std::env::var_os("ORT_DYLIB_PATH")
        .filter(|p| !p.is_empty())
        .unwrap_or_else(|| {
            option_env!("COWTYPE_ORT_DYLIB_PATH")
                .unwrap_or("libonnxruntime.so")
                .into()
        });
    let result = std::panic::catch_unwind(|| -> Result<()> {
        ort::init_from(path)?
            .with_name("cowtype-paraformer")
            .commit();
        // Force the version handshake here: ort otherwise panics lazily on first API use.
        let _ = ort::api();
        Ok(())
    });
    match result {
        Ok(Ok(())) => {
            let _ = READY.set(());
            Ok(())
        }
        Ok(Err(error)) => {
            anyhow::bail!("{}: {error:#}", crate::i18n::tr("voice.paraformer.runtime"))
        }
        Err(_) => anyhow::bail!("{}", crate::i18n::tr("voice.paraformer.runtime")),
    }
}

impl Model {
    fn open(dir: &Path) -> Result<Self> {
        check_files(dir)?;
        let config: Value =
            serde_yaml::from_str(&std::fs::read_to_string(dir.join("config.yaml"))?)?;
        for (group, key, want) in [
            ("frontend_conf", "fs", 16000.0),
            ("frontend_conf", "n_mels", 80.0),
            ("frontend_conf", "frame_length", 25.0),
            ("frontend_conf", "frame_shift", 10.0),
            ("frontend_conf", "lfr_m", 7.0),
            ("frontend_conf", "lfr_n", 6.0),
            ("encoder_conf", "output_size", 512.0),
        ] {
            ensure!(
                number(&config, group, key)? == want,
                "{}: {group}.{key}",
                crate::i18n::tr("voice.paraformer.config")
            );
        }
        ensure!(
            config["model"].as_str() == Some("ParaformerStreaming")
                && config["frontend_conf"]["window"].as_str() == Some("hamming"),
            "{}",
            crate::i18n::tr("voice.paraformer.config")
        );
        let layers = number(&config, "decoder_conf", "num_blocks")?;
        let kernel = number(&config, "decoder_conf", "kernel_size")?;
        ensure!(
            (1.0..=64.0).contains(&layers)
                && layers.fract() == 0.0
                && (2.0..=64.0).contains(&kernel)
                && kernel.fract() == 0.0,
            "{}",
            crate::i18n::tr("voice.paraformer.config")
        );
        let threshold = number(&config, "predictor_conf", "threshold")? as f32;
        let tail = number(&config, "predictor_conf", "tail_threshold")? as f32;
        let dither = config["frontend_conf"]["dither"].as_f64().unwrap_or(1.0) as f32;
        ensure!(
            threshold == 1.0
                && (0.0..1.0).contains(&tail)
                && dither.is_finite()
                && (0.0..=10.0).contains(&dither),
            "{}",
            crate::i18n::tr("voice.paraformer.config")
        );
        let tokens: Vec<String> =
            serde_json::from_str(&std::fs::read_to_string(dir.join("tokens.json"))?)?;
        ensure!(
            (3..=100_000).contains(&tokens.len()),
            "{}",
            crate::i18n::tr("voice.paraformer.config")
        );
        runtime()?;
        let threads = std::env::var("COWTYPE_VOICE_THREADS")
            .ok()
            .and_then(|v| v.parse::<usize>().ok())
            .unwrap_or(4)
            .clamp(1, 32);
        let open = |name: &str| -> Result<Session> {
            let mut builder = Session::builder()?
                .with_intra_threads(threads)
                .map_err(|e| anyhow::anyhow!(e.to_string()))?;
            Ok(builder
                .commit_from_file(dir.join(name))
                .map_err(|e| anyhow::anyhow!(e.to_string()))?)
        };
        let encoder = open("model.onnx")?;
        let decoder = open("decoder.onnx")?;
        let expected_encoder = ["speech", "speech_lengths"];
        let expected_decoder = ["enc", "enc_len", "acoustic_embeds", "acoustic_embeds_len"];
        ensure!(
            encoder.inputs().len() == 2
                && encoder.outputs().len() == 3
                && encoder
                    .inputs()
                    .iter()
                    .zip(expected_encoder)
                    .all(|(a, b)| a.name() == b)
                && decoder.inputs().len() == layers as usize + 4
                && decoder.outputs().len() == layers as usize + 2
                && decoder
                    .inputs()
                    .iter()
                    .take(4)
                    .zip(expected_decoder)
                    .all(|(a, b)| a.name() == b),
            "{}",
            crate::i18n::tr("voice.paraformer.interface")
        );
        let encoder_outputs = ["enc", "enc_len", "alphas"];
        ensure!(
            encoder
                .outputs()
                .iter()
                .zip(encoder_outputs)
                .all(|(out, name)| out.name() == name)
                && decoder
                    .inputs()
                    .iter()
                    .skip(4)
                    .enumerate()
                    .all(|(i, input)| input.name() == format!("in_cache_{i}")),
            "{}",
            crate::i18n::tr("voice.paraformer.interface")
        );
        for (outlets, integers, integer_type) in [
            (encoder.inputs(), vec![1], TensorElementType::Int32),
            (encoder.outputs(), vec![1], TensorElementType::Int32),
            (decoder.inputs(), vec![1, 3], TensorElementType::Int32),
            (decoder.outputs(), vec![1], TensorElementType::Int64),
        ] {
            for (index, outlet) in outlets.iter().enumerate() {
                let expected = if integers.contains(&index) {
                    integer_type
                } else {
                    TensorElementType::Float32
                };
                ensure!(
                    outlet.dtype().tensor_type() == Some(expected),
                    "{}: {}",
                    crate::i18n::tr("voice.paraformer.interface"),
                    outlet.name()
                );
            }
        }
        super::log(&format!("model_ready backend=paraformer ort_api=22 threads={threads} decoder_layers={layers} source=local"));
        Ok(Self {
            sessions: Mutex::new((encoder, decoder)),
            tokens,
            layers: layers as usize,
            order: kernel as usize - 1,
            threshold,
            tail,
            dither,
            dir: dir.to_path_buf(),
        })
    }
}

// Model sessions are shared; CIF, FSMN and feature caches belong to a single utterance.
type Fingerprint = Vec<(u64, Option<SystemTime>)>;
type Cached = Option<(PathBuf, Fingerprint, Arc<Model>)>;
static CACHE: Mutex<Cached> = Mutex::new(None);

pub fn load(dir: &Path) -> Result<Arc<Model>> {
    let mut cache = CACHE
        .lock()
        .map_err(|_| anyhow::anyhow!(crate::i18n::tr("voice.paraformer.lock")))?;
    let fingerprint: Fingerprint = FILES
        .iter()
        .map(|f| std::fs::metadata(dir.join(f)).map(|m| (m.len(), m.modified().ok())))
        .collect::<std::io::Result<_>>()?;
    if let Some((path, stamp, model)) = &*cache {
        if path == dir && stamp == &fingerprint {
            return Ok(model.clone());
        }
    }
    let model = Arc::new(Model::open(dir)?);
    *cache = Some((dir.to_path_buf(), fingerprint, model.clone()));
    Ok(model)
}

pub fn float_tensor(shape: Vec<usize>, data: Vec<f32>) -> Result<ort::value::DynValue> {
    Ok(Tensor::from_array((shape, data))?.into_dyn())
}
pub fn length_tensor(len: usize) -> Result<ort::value::DynValue> {
    Ok(Tensor::from_array(([1usize], vec![i32::try_from(len)?]))?.into_dyn())
}
