// SPDX-License-Identifier: GPL-3.0-or-later
//! Confirmed pinyin selections only. Versioned TSV, decay and conflict-safe undo.
use std::os::unix::fs::OpenOptionsExt;
pub mod edits;
pub mod service;
use std::{
    collections::BTreeMap,
    fs,
    io::{Read, Write},
    path::{Path, PathBuf},
    sync::{Arc, Mutex},
};
#[derive(Clone, Debug, PartialEq, Eq)]
struct Record {
    count: u32,
    last: u64,
    revision: u64,
}
#[derive(Clone, Debug)]
pub struct Undo {
    key: (String, String),
    before: Option<Record>,
    revision: u64,
}
#[derive(Debug, Default)]
pub struct Learning {
    records: BTreeMap<(String, String), Record>,
    sequence: u64,
    pub dirty: bool,
    pub read_error: bool,
    pub reset_token: String,
    edit_token: String,
}
pub type SharedLearning = Arc<Mutex<Learning>>;
pub fn now() -> u64 {
    std::time::SystemTime::now()
        .duration_since(std::time::UNIX_EPOCH)
        .map(|d| d.as_secs())
        .unwrap_or(0)
}
pub fn path() -> PathBuf {
    std::env::var_os("XDG_DATA_HOME")
        .filter(|v| !v.is_empty())
        .map(PathBuf::from)
        .unwrap_or_else(|| {
            PathBuf::from(std::env::var_os("HOME").unwrap_or_default()).join(".local/share")
        })
        .join("cowtype/learning-v1.tsv")
}
fn valid(reading: &str, text: &str) -> bool {
    crate::dictionary::reading_tokens(reading).is_some()
        && text.chars().count() <= 64
        && !text.is_ascii()
        && !text.chars().any(char::is_control)
}
impl Learning {
    pub fn score(&self, reading: &str, text: &str, time: u64) -> u32 {
        self.records
            .get(&(reading.into(), text.into()))
            .map(|r| r.count >> ((time.saturating_sub(r.last) / (30 * 86400)).min(31) as u32))
            .unwrap_or(0)
    }
    /// A single confirmation records an event but is not enough to reorder candidates.
    pub fn ranking_score(&self, reading: &str, text: &str, time: u64) -> u32 {
        self.score(reading, text, time).saturating_sub(1)
    }
    pub fn confirm(&mut self, reading: &str, text: &str, time: u64) -> Option<Undo> {
        if !valid(reading, text)
            || self.records.len() >= 20000
                && !self.records.contains_key(&(reading.into(), text.into()))
        {
            return None;
        }
        let key = (reading.into(), text.into());
        let before = self.records.get(&key).cloned();
        let count = self.score(reading, text, time).saturating_add(1);
        self.sequence = self.sequence.wrapping_add(1);
        self.records.insert(
            key.clone(),
            Record {
                count,
                last: time,
                revision: self.sequence,
            },
        );
        self.dirty = true;
        Some(Undo {
            key,
            before,
            revision: self.sequence,
        })
    }
    pub fn undo(&mut self, undos: &[Undo]) -> bool {
        if undos.iter().any(|u| {
            self.records
                .get(&u.key)
                .is_none_or(|r| r.revision != u.revision)
        }) {
            return false;
        }
        for undo in undos {
            if let Some(old) = &undo.before {
                self.records.insert(undo.key.clone(), old.clone());
            } else {
                self.records.remove(&undo.key);
            }
        }
        self.dirty |= !undos.is_empty();
        true
    }
    pub fn forget(&mut self, reading: &str, text: &str) -> bool {
        let changed = self
            .records
            .remove(&(reading.into(), text.into()))
            .is_some();
        self.dirty |= changed;
        changed
    }
    pub fn clear(&mut self) {
        self.records.clear();
        self.edit_token.clear();
        self.dirty = true;
    }
    pub fn load(path: &Path) -> Self {
        let run = || -> Result<Self, String> {
            let mut file = fs::File::open(path).map_err(|e| e.to_string())?;
            let mut bytes = Vec::new();
            std::io::Read::by_ref(&mut file)
                .take(8 * 1024 * 1024 + 1)
                .read_to_end(&mut bytes)
                .map_err(|e| e.to_string())?;
            if bytes.len() > 8 * 1024 * 1024 {
                return Err("learning file too large".into());
            }
            let mut lines = bytes.split(|b| *b == b'\n');
            if lines.next() != Some(b"# cowtype-learning-v1".as_slice()) {
                return Err("unrecognized learning data".into());
            }
            let mut state = Self::default();
            for line in lines.take(20003) {
                if state.records.len() >= 20000 {
                    break;
                }
                let Ok(line) = std::str::from_utf8(line) else {
                    continue;
                };
                if let Some(token) = line.strip_prefix("# edit-token\t") {
                    if token.len() == 32 && token.bytes().all(|b| b.is_ascii_hexdigit()) {
                        state.edit_token = token.into();
                    }
                    continue;
                }
                if let Some(token) = line.strip_prefix("# reset-token\t") {
                    if token.len() <= 128 {
                        state.reset_token = token.into();
                    }
                    continue;
                }
                let p: Vec<_> = line.trim_end_matches('\r').split('\t').collect();
                if p.len() != 4 || !valid(p[0], p[1]) {
                    continue;
                }
                if let (Ok(count), Ok(last)) = (p[2].parse::<u32>(), p[3].parse::<u64>()) {
                    if count > 0 {
                        state.sequence += 1;
                        state.records.insert(
                            (p[0].into(), p[1].into()),
                            Record {
                                count,
                                last,
                                revision: state.sequence,
                            },
                        );
                    }
                }
            }
            Ok(state)
        };
        match std::fs::symlink_metadata(path) {
            Err(e) if e.kind() == std::io::ErrorKind::NotFound => return Self::default(),
            Ok(m) if !m.file_type().is_symlink() => (),
            _ => {
                return Self {
                    read_error: true,
                    ..Self::default()
                }
            }
        }
        run().unwrap_or_else(|_| Self {
            read_error: true,
            ..Self::default()
        })
    }
    pub fn serialize(&self) -> String {
        let mut text = format!(
            "# cowtype-learning-v1\n# reset-token\t{}\n",
            self.reset_token
        );
        if !self.edit_token.is_empty() {
            text.push_str(&format!("# edit-token\t{}\n", self.edit_token));
        }
        for ((r, t), v) in &self.records {
            text.push_str(&format!("{r}\t{t}\t{}\t{}\n", v.count, v.last));
        }
        text
    }
}
pub fn save_atomic(path: &Path, text: &str) -> std::io::Result<()> {
    let parent = path
        .parent()
        .ok_or_else(|| std::io::Error::other("missing parent"))?;
    for p in path.ancestors() {
        if p.is_symlink() {
            return Err(std::io::Error::other("refusing learning symlink"));
        }
    }
    fs::create_dir_all(parent)?;
    let temporary = parent.join(format!(".learning-{}-{}.tmp", std::process::id(), now()));
    let mut created = false;
    let mut run = || -> std::io::Result<()> {
        let mut file = fs::OpenOptions::new()
            .create_new(true)
            .write(true)
            .mode(0o600)
            .open(&temporary)?;
        created = true;
        file.write_all(text.as_bytes())?;
        file.sync_all()?;
        fs::rename(&temporary, path)?;
        fs::File::open(parent)?.sync_all()
    };
    let result = run();
    if result.is_err() && created {
        let _ = fs::remove_file(&temporary);
    }
    result
}
