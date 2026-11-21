// SPDX-License-Identifier: GPL-3.0-or-later
//! Stop/cancel are separate. A stop retains real tail samples and finalizes once.
pub mod control;
use std::time::{Duration, Instant};
#[derive(Default)]
pub struct Lifecycle {
    stop: Option<Instant>,
    cancelled: bool,
    finished: bool,
}
impl Lifecycle {
    pub fn command(&mut self, command: &str, time: Instant) {
        match command.trim() {
            "cancel" => self.cancelled = true,
            "stop" if self.stop.is_none() => self.stop = Some(time + Duration::from_millis(250)),
            _ => (),
        }
    }
    pub fn cancelled(&self) -> bool {
        self.cancelled
    }
    pub fn should_stop(&self, time: Instant) -> bool {
        self.stop.is_some_and(|t| time >= t)
    }
    pub fn finalize(&mut self) -> bool {
        if self.cancelled || self.finished {
            false
        } else {
            self.finished = true;
            true
        }
    }
}
#[cfg(test)]
mod tests {
    use super::*;
    #[test]
    fn tap_and_hold_share_real_tail_then_finalize_once() {
        let now = Instant::now();
        let mut l = Lifecycle::default();
        l.command("stop", now);
        l.command("stop", now + Duration::from_millis(100));
        assert!(!l.should_stop(now + Duration::from_millis(249)));
        assert!(l.should_stop(now + Duration::from_millis(250)));
        assert!(l.finalize());
        assert!(!l.finalize());
    }
    #[test]
    fn cancel_late_result_never_finalizes() {
        let mut l = Lifecycle::default();
        l.command("stop", Instant::now());
        l.command("cancel", Instant::now());
        assert!(l.cancelled());
        assert!(!l.finalize());
    }
}
