// SPDX-License-Identifier: GPL-3.0-or-later
//! Offline CLDR annotations. Bundled Unicode data retains its Unicode-3.0 license.
use super::{Candidate, MatchKind};
use std::collections::{BTreeMap, HashSet};
use std::sync::OnceLock;

const KEYWORDS: &str = include_str!("../../src-data-tools/emoji/keywords.tsv");
const MAX_SUGGESTIONS: usize = 12;
static INDEX: OnceLock<BTreeMap<&'static str, Vec<&'static str>>> = OnceLock::new();

fn index() -> &'static BTreeMap<&'static str, Vec<&'static str>> {
    INDEX.get_or_init(|| {
        let mut result = BTreeMap::<_, Vec<_>>::new();
        for line in KEYWORDS.lines() {
            if let Some((keyword, sequence)) = line.split_once('\t') {
                result.entry(keyword).or_default().push(sequence);
            }
        }
        result
    })
}
fn normalize(text: &str) -> String {
    text.to_lowercase()
        .chars()
        .filter(|c| !c.is_ascii_whitespace() && !c.is_ascii_punctuation())
        .collect()
}

/// Supplement complete keyword matches without treating partial words or typo
/// guesses as annotations. Literal expansions share custom-phrase semantics:
/// selected symbols consume the original input once and are never learned.
pub(crate) fn augment(raw: &str, candidates: &mut Vec<Candidate>) {
    let raw_key = normalize(raw);
    let direct = index().get(raw_key.as_str());
    let mut seen: HashSet<String> = candidates.iter().map(|c| c.text.clone()).collect();
    let mut count = 0;
    let mut add = |out: &mut Vec<Candidate>, matches: Option<&Vec<&str>>, reading: &str| {
        for &sequence in matches.into_iter().flatten() {
            if count == MAX_SUGGESTIONS {
                break;
            }
            if seen.insert(sequence.to_owned()) {
                out.push(Candidate {
                    text: sequence.to_owned(),
                    reading: reading.to_owned(),
                    consumed_bytes: raw.len(),
                    has_remaining: false,
                    kind: MatchKind::Exact,
                    custom: true,
                });
                count += 1;
            }
        }
    };
    let mut result = Vec::with_capacity(candidates.len() + MAX_SUGGESTIONS);
    let mut direct_added = false;
    for candidate in std::mem::take(candidates) {
        let complete = !candidate.has_remaining
            && matches!(candidate.kind, MatchKind::Exact | MatchKind::Sentence);
        let keyword = normalize(&candidate.text);
        let reading = candidate.reading.clone();
        let custom = candidate.custom;
        result.push(candidate);
        if complete {
            if !custom {
                add(&mut result, index().get(keyword.as_str()), &reading);
            }
            if !direct_added {
                add(&mut result, direct, raw);
                direct_added = true;
            }
        }
    }
    if !direct_added {
        let mut symbols = Vec::new();
        add(&mut symbols, direct, raw);
        symbols.append(&mut result);
        result = symbols;
    }
    *candidates = result;
}
