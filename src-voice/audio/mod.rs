// SPDX-License-Identifier: GPL-3.0-or-later
//! Ten RMS level updates per second of 16 kHz PCM. No audio is retained.
#[derive(Default)]
pub struct LevelMeter {
    squared: f64,
    samples: usize,
}
impl LevelMeter {
    pub fn push(&mut self, pcm: &[i16], mut emit: impl FnMut(u8)) {
        for &sample in pcm {
            self.squared += (sample as f64 / 32768.).powi(2);
            self.samples += 1;
            if self.samples == 1600 {
                let rms = (self.squared / 1600.).sqrt();
                let level = if rms == 0. {
                    0
                } else {
                    ((20. * rms.log10() + 60.) / 60. * 8.).round().clamp(0., 8.) as u8
                };
                emit(level);
                self.squared = 0.;
                self.samples = 0;
            }
        }
    }
}
#[cfg(test)]
mod tests {
    use super::*;
    #[test]
    fn levels_follow_signal_and_split_reads_keep_update_rate() {
        let mut meter = LevelMeter::default();
        let mut levels = Vec::new();
        meter.push(&[0; 799], |v| levels.push(v));
        assert!(levels.is_empty());
        meter.push(&[0; 801], |v| levels.push(v));
        meter.push(&[32767; 1600], |v| levels.push(v));
        meter.push(&[3277; 1600], |v| levels.push(v));
        assert_eq!(levels[..2], [0, 8]);
        assert!(levels[2] > 0 && levels[2] < 8);
    }
}
