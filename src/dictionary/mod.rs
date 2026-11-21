// SPDX-License-Identifier: GPL-3.0-or-later
//! TSV readers. Readings declare the available syllables; no inherited syllable table.
use std::collections::HashMap;
use std::fmt;
use std::sync::Arc;

#[derive(Clone, Debug, PartialEq, Eq)]
pub struct Diagnostic {
    pub line: usize,
    pub reason: &'static str,
}

#[derive(Clone, Debug)]
pub struct Entry {
    pub reading: Vec<Arc<str>>,
    pub text: String,
    pub frequency: f64,
}

#[derive(Clone, Debug)]
pub struct Dictionary {
    pub(crate) entries: Vec<Entry>,
}

#[derive(Clone, Debug)]
pub struct LoadReport<T> {
    pub value: T,
    pub accepted: usize,
    pub duplicates: usize,
    pub diagnostics: Vec<Diagnostic>,
}

#[derive(Clone, Debug)]
pub struct LoadError {
    pub diagnostics: Vec<Diagnostic>,
}

impl fmt::Display for LoadError {
    fn fmt(&self, f: &mut fmt::Formatter<'_>) -> fmt::Result {
        write!(
            f,
            "no usable dictionary entries ({} invalid lines)",
            self.diagnostics.len()
        )
    }
}
impl std::error::Error for LoadError {}

/// Syntactic reading validation only: lowercase ASCII syllable tokens, including `v`.
/// Linguistic validation and a reviewed production syllable inventory are future work.
pub fn reading_tokens(reading: &str) -> Option<Vec<String>> {
    let tokens: Vec<_> = reading.split_ascii_whitespace().collect();
    if tokens.is_empty()
        || tokens
            .iter()
            .any(|t| !t.bytes().all(|b| b.is_ascii_lowercase()))
    {
        return None;
    }
    Some(tokens.into_iter().map(str::to_owned).collect())
}

impl Dictionary {
    /// Bad rows are isolated, including invalid UTF-8. Duplicate reading/text pairs
    /// keep their first position and sum their original frequencies.
    pub fn from_tsv(bytes: &[u8]) -> Result<LoadReport<Self>, LoadError> {
        let mut entries: Vec<Entry> = Vec::new();
        let mut seen = HashMap::new();
        let mut syllables: HashMap<String, Arc<str>> = HashMap::new();
        let mut diagnostics = Vec::new();
        let mut accepted = 0;
        let mut duplicates = 0;
        for (index, bytes) in bytes.split(|b| *b == b'\n').enumerate() {
            let mut parse = || -> Result<(), &'static str> {
                let line = std::str::from_utf8(bytes).map_err(|_| "invalid UTF-8")?;
                let line = line.strip_suffix('\r').unwrap_or(line);
                if line.trim().is_empty() || line.starts_with('#') {
                    return Ok(());
                }
                let columns: Vec<_> = line.split('\t').collect();
                if columns.len() != 3 {
                    return Err("expected three TSV columns");
                }
                let reading = reading_tokens(columns[0]).ok_or("unsupported reading syntax")?;
                if columns[1].trim().is_empty() || columns[1].chars().any(char::is_control) {
                    return Err("empty text or control character");
                }
                let frequency = positive_frequency(columns[2])?;
                let key = (reading.join(" "), columns[1].to_owned());
                if let Some(&position) = seen.get(&key) {
                    let entry: &mut Entry = &mut entries[position];
                    let combined = entry.frequency + frequency;
                    if !combined.is_finite() {
                        return Err("combined frequency overflow");
                    }
                    entry.frequency = combined;
                    duplicates += 1;
                } else {
                    seen.insert(key, entries.len());
                    entries.push(Entry {
                        reading: reading
                            .into_iter()
                            .map(|s| {
                                syllables
                                    .entry(s.clone())
                                    .or_insert_with(|| Arc::from(s))
                                    .clone()
                            })
                            .collect(),
                        text: columns[1].to_owned(),
                        frequency,
                    });
                }
                accepted += 1;
                Ok(())
            };
            if let Err(reason) = parse() {
                diagnostics.push(Diagnostic {
                    line: index + 1,
                    reason,
                });
            }
        }
        if entries.is_empty() {
            return Err(LoadError { diagnostics });
        }
        Ok(LoadReport {
            value: Self { entries },
            accepted,
            duplicates,
            diagnostics,
        })
    }

    pub fn len(&self) -> usize {
        self.entries.len()
    }
    pub fn is_empty(&self) -> bool {
        self.entries.is_empty()
    }
}

fn positive_frequency(value: &str) -> Result<f64, &'static str> {
    let frequency: f64 = value.parse().map_err(|_| "invalid frequency")?;
    if !frequency.is_finite() || frequency <= 0.0 {
        return Err("frequency must be finite and positive");
    }
    Ok(frequency)
}

#[derive(Clone, Debug, Default)]
pub struct CustomPhrases {
    pub(crate) entries: Vec<Entry>,
}

impl CustomPhrases {
    /// Explicit phrases are a separate, replaceable collection, never automatic history.
    /// Empty input is a valid empty replacement. Each shortcut matches only in full.
    pub fn from_tsv(bytes: &[u8]) -> LoadReport<Self> {
        let mut entries = Vec::new();
        let mut diagnostics = Vec::new();
        for (index, bytes) in bytes.split(|b| *b == b'\n').enumerate() {
            let mut parse = || -> Result<(), &'static str> {
                let line = std::str::from_utf8(bytes).map_err(|_| "invalid UTF-8")?;
                let line = line.strip_suffix('\r').unwrap_or(line);
                if line.trim().is_empty() || line.starts_with('#') {
                    return Ok(());
                }
                let columns: Vec<_> = line.split('\t').collect();
                if !(2..=3).contains(&columns.len()) {
                    return Err("expected two or three TSV columns");
                }
                let reading = reading_tokens(columns[0]).ok_or("unsupported shortcut syntax")?;
                if columns[1].trim().is_empty() || columns[1].chars().any(char::is_control) {
                    return Err("empty text or control character");
                }
                let frequency = if columns.len() == 3 {
                    positive_frequency(columns[2])?
                } else {
                    1.0
                };
                entries.push(Entry {
                    reading: reading.into_iter().map(Arc::from).collect(),
                    text: unescape(columns[1]),
                    frequency,
                });
                Ok(())
            };
            if let Err(reason) = parse() {
                diagnostics.push(Diagnostic {
                    line: index + 1,
                    reason,
                });
            }
        }
        LoadReport {
            accepted: entries.len(),
            duplicates: 0,
            value: Self { entries },
            diagnostics,
        }
    }
}

fn unescape(value: &str) -> String {
    let mut result = String::new();
    let mut chars = value.chars();
    while let Some(ch) = chars.next() {
        if ch != '\\' {
            result.push(ch);
            continue;
        }
        match chars.next() {
            Some('n') => result.push('\n'),
            Some('t') => result.push('\t'),
            Some('\\') => result.push('\\'),
            Some(other) => {
                result.push('\\');
                result.push(other);
            }
            None => result.push('\\'),
        }
    }
    result
}
