// SPDX-License-Identifier: GPL-3.0-or-later
//! Explicit private shortcuts, indexed once per saved file; no learned history.
use crate::engine::{Candidate, MatchKind};
use std::{
    collections::BTreeMap,
    fs,
    io::Read,
    path::{Path, PathBuf},
};
pub mod service;
#[derive(Debug, Default)]
pub struct Dictionary {
    entries: BTreeMap<String, Vec<(String, u32)>>,
}
pub fn path() -> PathBuf {
    crate::learning::path().with_file_name("custom-v1.tsv")
}
impl Dictionary {
    pub fn parse(text: &str) -> Result<Self, &'static str> {
        let mut lines = text.lines();
        if lines.next() != Some("# cowtype-custom-v1") {
            return Err("custom.error.data");
        }
        let mut result = Self::default();
        let mut count = 0;
        for line in lines {
            if line.is_empty() {
                continue;
            }
            let fields: Vec<_> = line.split('\t').collect();
            if fields.len() != 3
                || fields[0].is_empty()
                || fields[0].len() > 64
                || !fields[0].bytes().all(|c| c.is_ascii_lowercase())
            {
                return Err("custom.error.data");
            }
            let weight: u32 = fields[2].parse().map_err(|_| "custom.error.data")?;
            if !(1..=1_000_000).contains(&weight) {
                return Err("custom.error.data");
            }
            let mut value = String::new();
            let mut chars = fields[1].chars();
            while let Some(c) = chars.next() {
                if c == '\\' {
                    value.push(match chars.next() {
                        Some('n') => '\n',
                        Some('t') => '\t',
                        Some('\\') => '\\',
                        _ => return Err("custom.error.data"),
                    });
                } else {
                    value.push(c);
                }
            }
            if value.trim().is_empty()
                || value.chars().count() > 4096
                || value
                    .chars()
                    .any(|c| c.is_control() && c != '\n' && c != '\t')
            {
                return Err("custom.error.data");
            }
            let entries = result.entries.entry(fields[0].into()).or_default();
            if entries.iter().any(|(text, _)| *text == value) {
                return Err("custom.error.data");
            }
            entries.push((value, weight));
            count += 1;
            if count > 10000 {
                return Err("custom.error.data");
            }
        }
        for entries in result.entries.values_mut() {
            entries.sort_by_key(|entry| std::cmp::Reverse(entry.1));
        }
        Ok(result)
    }
    pub fn load(path: &Path) -> Result<Self, &'static str> {
        if path.ancestors().any(Path::is_symlink) {
            return Err("custom.error.data");
        }
        let mut file = match fs::File::open(path) {
            Err(e) if e.kind() == std::io::ErrorKind::NotFound => return Ok(Self::default()),
            result => result.map_err(|_| "custom.error.data")?,
        };
        let mut bytes = Vec::new();
        file.by_ref()
            .take(8 * 1024 * 1024 + 1)
            .read_to_end(&mut bytes)
            .map_err(|_| "custom.error.data")?;
        if bytes.len() > 8 * 1024 * 1024 {
            return Err("custom.error.data");
        }
        Self::parse(std::str::from_utf8(&bytes).map_err(|_| "custom.error.data")?)
    }
    pub fn has_prefix(&self, raw: &str) -> bool {
        let key = raw.to_ascii_lowercase();
        self.entries
            .range(key.clone()..)
            .next()
            .is_some_and(|(k, _)| k.starts_with(&key))
    }
    pub fn query(&self, raw: &str) -> Vec<Candidate> {
        self.entries
            .get(&raw.to_ascii_lowercase())
            .into_iter()
            .flatten()
            .map(|(text, _)| Candidate {
                text: text.clone(),
                reading: raw.to_ascii_lowercase(),
                consumed_bytes: raw.len(),
                has_remaining: false,
                kind: MatchKind::Exact,
                custom: true,
            })
            .collect()
    }
}
