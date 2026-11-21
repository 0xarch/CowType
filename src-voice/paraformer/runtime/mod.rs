//! Rust streaming Paraformer over the official FunASR encoder/decoder ONNX export.
//! The reference algorithm and attribution are recorded in NOTICE.md.
mod frontend;
mod model;

use crate::asr::AsrChannel;
use anyhow::{ensure, Context, Result};
use frontend::{Frontend, DIM};
pub use model::{check_files, default_dir, is_ready, load, selection_path, MODEL_URL};
use model::{float_tensor, length_tensor, Model};
use std::{collections::VecDeque, path::Path, sync::Arc, time::Instant};

const HIDDEN: usize = 512;
const LEFT: usize = 5;
const CHUNK: usize = 10;
const RIGHT: usize = 5;

pub(crate) fn log(message: &str) {
    #[cfg(feature = "capture")]
    crate::capture::log(&format!("backend=paraformer {message}"));
    #[cfg(not(feature = "capture"))]
    eprintln!("paraformer: {message}");
}

pub struct ParaformerChannel {
    model: Arc<Model>,
    frontend: Frontend,
    queued: VecDeque<Vec<f32>>,
    overlap: Vec<Vec<f32>>,
    fsmn: Vec<Vec<f32>>,
    integrated: f32,
    weighted: Vec<f32>,
    tokens: Vec<String>,
    text: String,
    error: Option<String>,
    finished: bool,
    samples: usize,
    blocks: usize,
}

impl ParaformerChannel {
    pub fn new(dir: impl AsRef<Path>) -> Result<Self> {
        let model = load(dir.as_ref())?;
        let frontend = Frontend::new(&model.dir.join("am.mvn"), model.dither)?;
        let fsmn = vec![vec![0.0; HIDDEN * model.order]; model.layers];
        log("session_start chunk=[5,10,5] sample_rate=16000 caches=reset");
        Ok(Self {
            model,
            frontend,
            queued: VecDeque::new(),
            overlap: vec![vec![0.0; DIM]; LEFT + RIGHT],
            fsmn,
            integrated: 0.0,
            weighted: vec![0.0; HIDDEN],
            tokens: Vec::new(),
            text: String::new(),
            error: None,
            finished: false,
            samples: 0,
            blocks: 0,
        })
    }

    pub fn push(&mut self, pcm: &[i16]) {
        if self.finished || self.error.is_some() {
            return;
        }
        self.samples += pcm.len();
        // The 120 s trigger limit is followed by up to 250 ms of real capture tail.
        if self.samples > 16000 * 121 {
            self.error = Some(crate::i18n::tr("voice.paraformer.limit").into());
            return;
        }
        self.frontend.push(pcm);
        self.queued.extend(self.frontend.take(false));
    }

    pub fn take_error(&mut self) -> Option<String> {
        self.error.clone()
    }

    pub fn refresh_result(&mut self) -> Result<bool> {
        if let Some(e) = &self.error {
            anyhow::bail!("{e}");
        }
        let before = self.text.clone();
        while !self.finished && self.queued.len() >= CHUNK {
            let mut block = self.overlap.clone();
            block.extend(self.queued.drain(..CHUNK));
            self.overlap = block[block.len() - (LEFT + RIGHT)..].to_vec();
            self.infer(&block, false)?;
        }
        Ok(self.text != before)
    }

    pub fn refresh(&mut self) -> bool {
        match self.refresh_result() {
            Ok(changed) => changed,
            Err(e) => {
                self.error = Some(format!("{e:#}"));
                false
            }
        }
    }

    pub fn finish_result(&mut self) -> Result<Option<String>> {
        if let Some(e) = &self.error {
            anyhow::bail!("{e}");
        }
        if self.finished {
            return Ok((!self.text.is_empty()).then(|| self.text.clone()));
        }
        self.queued.extend(self.frontend.take(true));
        self.refresh_result()?;
        if self.blocks > 0 || !self.queued.is_empty() {
            let mut block = self.overlap.clone();
            block.extend(self.queued.drain(..));
            // A non-final pass has a 5-frame lookahead. Consume its outstanding center
            // frames once; if the final tail exceeds the remaining center, flush twice.
            // The second block starts at the next LEFT context, NOT at already-decoded
            // center frames. Never replay PCM or manufacture silence feature frames.
            if block.len() > LEFT + CHUNK {
                let tail = block[CHUNK..].to_vec();
                block.resize(LEFT + CHUNK + RIGHT, vec![0.0; DIM]);
                self.infer(&block, false)?;
                self.infer(&tail, true)?;
            } else {
                self.infer(&block, true)?;
            }
        }
        self.finished = true;
        log(&format!(
            "session_finish samples={} blocks={} final=true chars={}",
            self.samples,
            self.blocks,
            self.text.chars().count()
        ));
        Ok((!self.text.is_empty()).then(|| self.text.clone()))
    }

    fn infer(&mut self, features: &[Vec<f32>], last: bool) -> Result<()> {
        let start = Instant::now();
        let input = float_tensor(
            vec![1, features.len(), DIM],
            features.iter().flatten().copied().collect(),
        )?;
        let mut sessions = self
            .model
            .sessions
            .lock()
            .map_err(|_| anyhow::anyhow!(crate::i18n::tr("voice.paraformer.lock")))?;
        let (encoder, decoder) = &mut *sessions;
        let outputs = encoder
            .run(vec![
                ("speech", input),
                ("speech_lengths", length_tensor(features.len())?),
            ])
            .context(crate::i18n::tr("voice.paraformer.encoder"))?;
        let (shape, enc) = outputs[0].try_extract_tensor::<f32>()?;
        ensure!(
            shape.len() == 3
                && shape[0] == 1
                && shape[1] == features.len() as i64
                && shape[2] == HIDDEN as i64,
            "{}",
            crate::i18n::tr("voice.paraformer.shape")
        );
        let (_, alphas) = outputs[2].try_extract_tensor::<f32>()?;
        ensure!(
            alphas.len() == features.len() && enc.iter().all(|v| v.is_finite()),
            "{}",
            crate::i18n::tr("voice.paraformer.shape")
        );
        let mut embeds = Vec::new();
        for (t, &alpha) in alphas.iter().enumerate() {
            if !(LEFT..LEFT + CHUNK).contains(&t) {
                continue;
            }
            ensure!(
                alpha.is_finite() && (0.0..=1.00001).contains(&alpha),
                "{}",
                crate::i18n::tr("voice.paraformer.shape")
            );
            cif_step(
                alpha,
                &enc[t * HIDDEN..(t + 1) * HIDDEN],
                self.model.threshold,
                &mut self.integrated,
                &mut self.weighted,
                &mut embeds,
            );
        }
        if last {
            cif_step(
                self.model.tail,
                &[0.0; HIDDEN],
                self.model.threshold,
                &mut self.integrated,
                &mut self.weighted,
                &mut embeds,
            );
        }
        let n = embeds.len() / HIDDEN;
        if n > 0 {
            let mut input = vec![
                (
                    "enc".to_string(),
                    float_tensor(vec![1, features.len(), HIDDEN], enc.to_vec())?,
                ),
                ("enc_len".into(), length_tensor(features.len())?),
                (
                    "acoustic_embeds".into(),
                    float_tensor(vec![1, n, HIDDEN], embeds)?,
                ),
                ("acoustic_embeds_len".into(), length_tensor(n)?),
            ];
            for (layer, cache) in self.fsmn.iter().enumerate() {
                input.push((
                    format!("in_cache_{layer}"),
                    float_tensor(vec![1, HIDDEN, self.model.order], cache.clone())?,
                ));
            }
            let output = decoder
                .run(input)
                .context(crate::i18n::tr("voice.paraformer.decoder"))?;
            let (shape, logits) = output[0].try_extract_tensor::<f32>()?;
            let vocab = self.model.tokens.len();
            ensure!(
                shape.len() == 3
                    && shape[0] == 1
                    && shape[1] >= n as i64
                    && shape[2] == vocab as i64
                    && logits.iter().all(|v| v.is_finite()),
                "{}",
                crate::i18n::tr("voice.paraformer.shape")
            );
            for row in logits.chunks_exact(vocab).take(n) {
                let id = row
                    .iter()
                    .enumerate()
                    .max_by(|a, b| a.1.total_cmp(b.1))
                    .map(|(i, _)| i)
                    .unwrap_or(0);
                if ![0, 2].contains(&id) {
                    self.tokens.push(self.model.tokens[id].clone());
                }
            }
            for layer in 0..self.model.layers {
                let (shape, cache) = output[layer + 2].try_extract_tensor::<f32>()?;
                ensure!(
                    shape.len() == 3
                        && shape[0] == 1
                        && shape[1] == HIDDEN as i64
                        && shape[2] >= self.model.order as i64,
                    "{}",
                    crate::i18n::tr("voice.paraformer.shape")
                );
                let width = shape[2] as usize;
                self.fsmn[layer] = cache
                    .chunks_exact(width)
                    .flat_map(|row| row[width - self.model.order..].iter().copied())
                    .collect();
            }
            self.text = render_tokens(&self.tokens);
        }
        self.blocks += 1;
        log(&format!(
            "chunk={} feature_frames={} final={} emitted_tokens={} decode_ms={}",
            self.blocks,
            features.len(),
            last,
            n,
            start.elapsed().as_millis()
        ));
        Ok(())
    }
}

fn cif_step(
    alpha: f32,
    hidden: &[f32],
    threshold: f32,
    integrated: &mut f32,
    weighted: &mut [f32],
    out: &mut Vec<f32>,
) {
    if *integrated + alpha < threshold {
        *integrated += alpha;
        for (v, &h) in weighted.iter_mut().zip(hidden) {
            *v += alpha * h;
        }
    } else {
        let needed = threshold - *integrated;
        for (v, &h) in weighted.iter_mut().zip(hidden) {
            *v += needed * h;
        }
        out.extend_from_slice(weighted);
        *integrated = (*integrated + alpha - threshold).max(0.0);
        for (v, &h) in weighted.iter_mut().zip(hidden) {
            *v = *integrated * h;
        }
    }
}

fn render_tokens(tokens: &[String]) -> String {
    let mut words: Vec<(String, bool)> = Vec::new();
    let mut pending = String::new();
    for token in tokens {
        if matches!(
            token.as_str(),
            "<blank>" | "<s>" | "</s>" | "<unk>" | "<sos/eos>"
        ) {
            continue;
        }
        if let Some(part) = token.strip_suffix("@@") {
            pending.push_str(part);
            continue;
        }
        if token.chars().all(|c| c.is_ascii_alphabetic() || c == '\'') && !token.is_empty() {
            pending.push_str(token);
            words.push((std::mem::take(&mut pending), true));
        } else {
            if !pending.is_empty() {
                words.push((std::mem::take(&mut pending), true));
            }
            words.push((token.replace("<space>", " "), false));
        }
    }
    if !pending.is_empty() {
        words.push((pending, true));
    }
    let mut text = String::new();
    let mut i = 0;
    let mut previous_alpha = false;
    while i < words.len() {
        let (word, alpha) = &words[i];
        if previous_alpha && *alpha {
            text.push(' ');
        }
        if *alpha && word.len() == 1 && word.is_ascii() {
            let mut end = i + 1;
            while end < words.len()
                && words[end].1
                && words[end].0.len() == 1
                && words[end].0.is_ascii()
            {
                end += 1;
            }
            if end - i >= 2 {
                for item in &words[i..end] {
                    text.push_str(&item.0.to_ascii_uppercase());
                }
                i = end;
                previous_alpha = true;
                continue;
            }
        }
        text.push_str(word);
        previous_alpha = *alpha;
        i += 1;
    }
    text.trim().to_string()
}

impl AsrChannel for ParaformerChannel {
    fn name(&self) -> &str {
        "paraformer"
    }
    fn feed(&mut self, pcm: &[i16]) {
        self.push(pcm);
        self.refresh();
    }
    fn partial(&self) -> Option<&str> {
        (!self.text.is_empty()).then_some(self.text.as_str())
    }
    fn finish(&mut self) -> Option<String> {
        match self.finish_result() {
            Ok(text) => text,
            Err(e) => {
                self.error = Some(format!("{e:#}"));
                None
            }
        }
    }
    fn take_error(&mut self) -> Option<String> {
        ParaformerChannel::take_error(self)
    }
}
