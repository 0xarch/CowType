// SPDX-License-Identifier: GPL-3.0-or-later
//! One serialized background writer. No disk I/O on selection/query paths.
use super::{save_atomic, Learning, SharedLearning};
use std::{
    path::{Path, PathBuf},
    sync::{mpsc, Arc, Mutex},
    thread,
    time::Duration,
};
pub struct Service {
    pub state: SharedLearning,
    pub error: Arc<Mutex<String>>,
    stop: mpsc::Sender<()>,
    thread: Option<thread::JoinHandle<()>>,
}
fn reset_token(path: &Path) -> String {
    std::fs::read_to_string(path)
        .ok()
        .filter(|s| {
            s.len() <= 128
                && s.trim()
                    .bytes()
                    .all(|b| b.is_ascii_alphanumeric() || b == b'-')
        })
        .map(|s| s.trim().into())
        .unwrap_or_default()
}
impl Service {
    pub fn new(path: PathBuf) -> Self {
        let mut loaded = Learning::load(&path);
        if loaded.read_error {
            let marker = path.with_file_name("learning-reset-v1");
            // An old reset request must not authorize overwriting a subsequently damaged file.
            let fresh_reset = marker
                .metadata()
                .and_then(|m| m.modified())
                .ok()
                .zip(path.metadata().and_then(|m| m.modified()).ok())
                .is_some_and(|(reset, history)| reset > history);
            if !fresh_reset {
                loaded.reset_token = reset_token(&marker);
            }
        }
        let state = Arc::new(Mutex::new(loaded));
        let error = Arc::new(Mutex::new(String::new()));
        let (stop, receiver) = mpsc::channel();
        let state2 = state.clone();
        let error2 = error.clone();
        let thread = thread::spawn(move || {
            let marker = path.with_file_name("learning-reset-v1");
            loop {
                let stopping = receiver.recv_timeout(Duration::from_millis(500)).is_ok();
                let token = reset_token(&marker);
                let snapshot = if let Ok(mut l) = state2.lock() {
                    if l.reset_token != token {
                        l.clear();
                        l.reset_token = token.clone();
                        l.read_error = false;
                    }
                    if l.read_error {
                        if let Ok(mut e) = error2.lock() {
                            *e = crate::i18n::tr("message.a3f8ce85d454").into();
                        }
                        None
                    } else if l.dirty {
                        l.dirty = false;
                        Some(l.serialize())
                    } else {
                        None
                    }
                } else {
                    None
                };
                if let Some(text) = snapshot {
                    // A concurrent explicit reset invalidates the previous snapshot.
                    if reset_token(&marker) != token {
                        if let Ok(mut l) = state2.lock() {
                            l.dirty = true;
                        }
                    } else if save_atomic(&path, &text).is_err() {
                        if let Ok(mut l) = state2.lock() {
                            l.dirty = true;
                        }
                        if let Ok(mut e) = error2.lock() {
                            *e = crate::i18n::tr("message.228d321b266f").into();
                        }
                    } else if let Ok(mut e) = error2.lock() {
                        e.clear();
                    }
                }
                if stopping {
                    break;
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
