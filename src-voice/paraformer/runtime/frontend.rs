//! Kaldi-compatible log-Mel -> LFR -> CMVN -> online positional encoding.
//! See NOTICE.md for the official reference. No model-specific proprietary code.
use anyhow::{ensure, Context, Result};
use rustfft::{num_complex::Complex32, Fft, FftPlanner};
use std::{f32::consts::PI, path::Path, sync::Arc};

pub const MELS: usize = 80;
pub const DIM: usize = MELS * 7;
const WINDOW: usize = 400;
const SHIFT: usize = 160;
const FFT: usize = 512;
const LFR_STEP: usize = 6;

pub struct Frontend {
    fft: Arc<dyn Fft<f32>>,
    window: Vec<f32>,
    filters: Vec<Vec<(usize, f32)>>,
    shift: Vec<f32>,
    scale: Vec<f32>,
    pcm: Vec<f32>,
    frames: Vec<[f32; MELS]>,
    next_lfr: usize,
    dither: f32,
    random: u64,
}

impl Frontend {
    pub fn new(cmvn: &Path, dither: f32) -> Result<Self> {
        let text = std::fs::read_to_string(cmvn)?;
        let stats = |tag: &str| -> Result<Vec<f32>> {
            let tail = text
                .split_once(tag)
                .context(crate::i18n::tr("voice.paraformer.cmvn"))?
                .1;
            let vector = tail
                .split_once('[')
                .and_then(|(_, s)| s.split_once(']'))
                .context(crate::i18n::tr("voice.paraformer.cmvn"))?
                .0;
            let values = vector
                .split_whitespace()
                .map(str::parse)
                .collect::<std::result::Result<Vec<f32>, _>>()?;
            ensure!(
                values.len() == DIM && values.iter().all(|v| v.is_finite()),
                "{}",
                crate::i18n::tr("voice.paraformer.cmvn")
            );
            Ok(values)
        };
        let shift = stats("<AddShift>")?;
        let scale = stats("<Rescale>")?;
        let mel = |hz: f32| 1127.0 * (1.0 + hz / 700.0).ln();
        let low = mel(20.0);
        let step = (mel(8000.0) - low) / (MELS + 1) as f32;
        let filters = (0..MELS)
            .map(|m| {
                let left = low + m as f32 * step;
                let center = left + step;
                let right = center + step;
                // Kaldi excludes the Nyquist bin, uses power, and does not normalize triangles.
                (0..FFT / 2)
                    .filter_map(|k| {
                        let f = mel(k as f32 * 16000.0 / FFT as f32);
                        (f > left && f < right).then(|| {
                            (
                                k,
                                if f <= center {
                                    (f - left) / step
                                } else {
                                    (right - f) / step
                                },
                            )
                        })
                    })
                    .collect()
            })
            .collect();
        Ok(Self {
            fft: FftPlanner::new().plan_fft_forward(FFT),
            window: (0..WINDOW)
                .map(|i| 0.54 - 0.46 * (2.0 * PI * i as f32 / (WINDOW - 1) as f32).cos())
                .collect(),
            filters,
            shift,
            scale,
            pcm: Vec::new(),
            frames: Vec::new(),
            next_lfr: 0,
            dither,
            random: 0x853c_49e6_748f_ea9b,
        })
    }

    fn uniform(&mut self) -> f32 {
        self.random ^= self.random >> 12;
        self.random ^= self.random << 25;
        self.random ^= self.random >> 27;
        ((self.random.wrapping_mul(0x2545_f491_4f6c_dd1d) >> 40) as f32 + 0.5) / 16777216.0
    }

    pub fn push(&mut self, samples: &[i16]) {
        // Official frontend multiplies normalized float PCM by 32768. Input here is already i16.
        self.pcm.extend(samples.iter().map(|&s| s as f32));
        let mut offset = 0;
        while self.pcm.len() - offset >= WINDOW {
            let mut frame = self.pcm[offset..offset + WINDOW].to_vec();
            if self.dither != 0.0 {
                for x in &mut frame {
                    let u = self.uniform();
                    let v = self.uniform();
                    *x += self.dither * (-2.0 * u.ln()).sqrt() * (2.0 * PI * v).cos();
                }
            }
            let mean = frame.iter().sum::<f32>() / WINDOW as f32;
            for x in &mut frame {
                *x -= mean;
            }
            // Reverse order preserves the original previous sample for pre-emphasis.
            for i in (1..WINDOW).rev() {
                frame[i] -= 0.97 * frame[i - 1];
            }
            frame[0] *= 1.0 - 0.97;
            let mut spectrum = vec![Complex32::default(); FFT];
            for i in 0..WINDOW {
                spectrum[i].re = frame[i] * self.window[i];
            }
            self.fft.process(&mut spectrum);
            let power: Vec<f32> = spectrum.iter().map(|x| x.norm_sqr()).collect();
            self.frames.push(std::array::from_fn(|m| {
                self.filters[m]
                    .iter()
                    .map(|&(k, w)| power[k] * w)
                    .sum::<f32>()
                    .max(f32::EPSILON)
                    .ln()
            }));
            offset += SHIFT;
        }
        self.pcm.drain(..offset);
    }

    pub fn take(&mut self, is_final: bool) -> Vec<Vec<f32>> {
        let mut out = Vec::new();
        if self.frames.is_empty() {
            return out;
        }
        // snip_edges=true: final does not manufacture partial 25 ms waveform frames.
        // LFR repeats edge feature frames, including when final has no new waveform frame.
        while self.next_lfr * LFR_STEP < self.frames.len() {
            let center = self.next_lfr * LFR_STEP;
            if !is_final && center + 3 >= self.frames.len() {
                break;
            }
            let half = DIM / 2;
            let mut feat = Vec::with_capacity(DIM);
            for j in 0..7 {
                let idx = (center + j).saturating_sub(3).min(self.frames.len() - 1);
                feat.extend_from_slice(&self.frames[idx]);
            }
            for (i, x) in feat.iter_mut().enumerate() {
                *x = (*x + self.shift[i]) * self.scale[i] * 512.0_f32.sqrt();
                let angle = (self.next_lfr + 1) as f32
                    * (-10000.0_f32.ln() * (i % half) as f32 / (half - 1) as f32).exp();
                *x += if i < half { angle.sin() } else { angle.cos() };
            }
            out.push(feat);
            self.next_lfr += 1;
        }
        out
    }
}
