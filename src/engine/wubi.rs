// SPDX-License-Identifier: GPL-3.0-or-later
//! Wubi lookup of an explicit public text table; no private formats or learned ordering.
use super::{Candidate, MatchKind};
use crate::dictionary::{Diagnostic, LoadError, LoadReport};
use std::collections::{BTreeMap, HashSet};

#[derive(Clone, Debug)]
struct Row {
    text: String,
    priority: u64,
    order: usize,
}

#[derive(Clone, Debug)]
pub struct WubiTable {
    codes: BTreeMap<String, Vec<Row>>,
}

impl WubiTable {
    pub fn from_tsv(bytes: &[u8]) -> Result<LoadReport<Self>, LoadError> {
        let mut codes: BTreeMap<String, Vec<Row>> = BTreeMap::new();
        let mut diagnostics = Vec::new();
        let mut accepted = 0;
        for (line, bytes) in bytes.split(|b| *b == b'\n').enumerate() {
            let mut parse = || -> Result<(), &'static str> {
                let text = std::str::from_utf8(bytes).map_err(|_| "invalid UTF-8")?;
                let text = text.strip_suffix('\r').unwrap_or(text);
                if text.trim().is_empty() || text.starts_with('#') {
                    return Ok(());
                }
                let fields: Vec<_> = text.split('\t').collect();
                if fields.len() != 3 {
                    return Err("expected three TSV columns");
                }
                if !valid_code(fields[0]) {
                    return Err("invalid Wubi code (expected 1-4 a-y letters)");
                }
                if fields[1].trim().is_empty() || fields[1].chars().any(char::is_control) {
                    return Err("invalid text");
                }
                let priority = fields[2]
                    .parse::<u64>()
                    .map_err(|_| "invalid Wubi priority")?;
                codes.entry(fields[0].to_owned()).or_default().push(Row {
                    text: fields[1].into(),
                    priority,
                    order: line,
                });
                accepted += 1;
                Ok(())
            };
            if let Err(reason) = parse() {
                diagnostics.push(Diagnostic {
                    line: line + 1,
                    reason,
                });
            }
        }
        if accepted == 0 {
            return Err(LoadError { diagnostics });
        }
        Ok(LoadReport {
            value: Self { codes },
            accepted,
            duplicates: 0,
            diagnostics,
        })
    }
    pub fn query(&self, raw: &str) -> Vec<Candidate> {
        let input = raw.trim_ascii().to_ascii_lowercase();
        if !valid_code(&input) {
            return Vec::new();
        }
        let mut found: Vec<_> = self
            .codes
            .range(input.clone()..)
            .take_while(|(code, _)| code.starts_with(&input))
            .flat_map(|(code, rows)| rows.iter().map(move |row| (code, row)))
            .collect();
        found.sort_by(|(a, ar), (b, br)| {
            (a.as_str() != input)
                .cmp(&(b.as_str() != input))
                .then(br.priority.cmp(&ar.priority))
                .then(ar.order.cmp(&br.order))
        });
        let mut seen = HashSet::new();
        found
            .into_iter()
            .filter_map(|(code, row)| {
                if !seen.insert(row.text.clone()) {
                    return None;
                }
                Some(Candidate {
                    text: row.text.clone(),
                    reading: code.clone(),
                    consumed_bytes: raw.len(),
                    has_remaining: false,
                    kind: if code == &input {
                        MatchKind::Exact
                    } else {
                        MatchKind::Completion
                    },
                    custom: false,
                })
            })
            .collect()
    }
}
fn valid_code(code: &str) -> bool {
    (1..=4).contains(&code.len()) && code.bytes().all(|b| (b'a'..=b'y').contains(&b))
}
