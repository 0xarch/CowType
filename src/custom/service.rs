// SPDX-License-Identifier: GPL-3.0-or-later
//! Reload saved personal shortcuts off the keystroke path, preserving last good data.
use super::Dictionary;
use std::{
    fs,
    os::unix::fs::MetadataExt,
    path::{Path, PathBuf},
    sync::{mpsc, Arc, Mutex, RwLock},
    thread,
    time::{Duration, SystemTime},
};
pub type Shared = Arc<RwLock<Dictionary>>;
pub struct Service {
    pub state: Shared,
    pub error: Arc<Mutex<String>>,
    stop: mpsc::Sender<()>,
    thread: Option<thread::JoinHandle<()>>,
}
fn stamp(path: &Path) -> Option<(SystemTime, u64, u64)> {
    fs::symlink_metadata(path)
        .ok()
        .and_then(|m| Some((m.modified().ok()?, m.len(), m.ino())))
}
impl Service {
    pub fn new(path: PathBuf) -> Self {
        let initial_stamp = stamp(&path);
        let loaded = Dictionary::load(&path);
        let error = Arc::new(Mutex::new(
            loaded
                .as_ref()
                .err()
                .map(|key| crate::i18n::tr(key).to_owned())
                .unwrap_or_default(),
        ));
        let state = Arc::new(RwLock::new(loaded.unwrap_or_default()));
        let (stop, receiver) = mpsc::channel();
        let state2 = state.clone();
        let error2 = error.clone();
        let thread = thread::spawn(move || {
            let mut previous = initial_stamp;
            while let Err(mpsc::RecvTimeoutError::Timeout) =
                receiver.recv_timeout(Duration::from_millis(500))
            {
                let current = stamp(&path);
                if current == previous {
                    continue;
                }
                // An atomic rename between stat and read causes another read next tick.
                previous = current;
                match Dictionary::load(&path) {
                    Ok(dictionary) => {
                        if let Ok(mut state) = state2.write() {
                            *state = dictionary;
                        }
                        if let Ok(mut error) = error2.lock() {
                            error.clear();
                        }
                    }
                    Err(key) => {
                        if let Ok(mut error) = error2.lock() {
                            *error = crate::i18n::tr(key).into();
                        }
                    }
                }
            }
        });
        Self {
            state,
            error,
            stop,
            thread: Some(thread),
        }
    }
}
impl Drop for Service {
    fn drop(&mut self) {
        let _ = self.stop.send(());
        if let Some(t) = self.thread.take() {
            let _ = t.join();
        }
    }
}
