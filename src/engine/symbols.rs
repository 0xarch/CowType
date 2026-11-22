// SPDX-License-Identifier: GPL-3.0-or-later
//! Offline CLDR annotations. Bundled Unicode data retains its Unicode-3.0 license.
use super::{Candidate, MatchKind};
use std::collections::{BTreeMap, HashSet};
use std::sync::OnceLock;

const KEYWORDS: &str = include_str!("../../src-data-tools/emoji/keywords.tsv");
const MAX_SUGGESTIONS: usize = 12;
static INDEX: OnceLock<BTreeMap<&'static str, Vec<&'static str>>> = OnceLock::new();
const TONE_FAMILIES: &str = include_str!("../../src-data-tools/emoji/tone-families.tsv");
#[derive(Default)]
struct Families {
    canonical: BTreeMap<&'static str, &'static str>,
    choices: BTreeMap<&'static str, Vec<&'static str>>,
}
static FAMILIES: OnceLock<Families> = OnceLock::new();

fn tone(c: char) -> Option<u8> {
    ('\u{1f3fb}'..='\u{1f3ff}')
        .contains(&c)
        .then(|| (c as u32 - 0x1f3fb + 1) as u8)
}
fn families() -> &'static Families {
    FAMILIES.get_or_init(|| {
        let mut result = Families::default();
        for line in TONE_FAMILIES.lines() {
            let choices: Vec<_> = line.split('\t').collect();
            if let Some(&base) = choices.first() {
                for &sequence in &choices {
                    result.canonical.insert(sequence, base);
                }
                result.choices.insert(base, choices);
            }
        }
        result
    })
}
fn family_key(sequence: &str) -> &str {
    families()
        .canonical
        .get(sequence)
        .copied()
        .unwrap_or(sequence)
}
/// Use only official fully-qualified Unicode sequences. Names associate the
/// atomic and ZWJ variants without inventing or deleting any code points.
pub(crate) fn variants(sequence: &str) -> &'static [&'static str] {
    families()
        .choices
        .get(family_key(sequence))
        .map(Vec::as_slice)
        .unwrap_or_default()
}
fn preferred(sequence: &str, default_tone: u8) -> String {
    let choices = variants(sequence);
    let matches_tone = |s: &&&str| {
        let tones: Vec<_> = s.chars().filter_map(tone).collect();
        if default_tone == 0 {
            tones.is_empty()
        } else {
            !tones.is_empty() && tones.iter().all(|&t| t == default_tone)
        }
    };
    choices
        .iter()
        .find(matches_tone)
        .or_else(|| choices.first())
        .copied()
        .unwrap_or(sequence)
        .to_owned()
}

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
pub(crate) fn augment(raw: &str, candidates: &mut Vec<Candidate>, default_tone: u8) {
    let raw_key = normalize(raw);
    let direct = index().get(raw_key.as_str());
    let mut seen: HashSet<String> = candidates.iter().map(|c| c.text.clone()).collect();
    let mut groups: Vec<(usize, String, Vec<String>)> = Vec::new();
    // Collapse families BEFORE allocating the global suggestion budget. A
    // keyword containing only a toned variant still refers to the same family.
    let collapse = |matches: &[&str]| {
        let mut seen = HashSet::new();
        matches
            .iter()
            .filter_map(|s| {
                seen.insert(family_key(s))
                    .then(|| preferred(s, default_tone))
            })
            .collect::<Vec<_>>()
    };
    let mut direct_added = false;
    for (position, candidate) in candidates.iter().enumerate() {
        let complete = !candidate.has_remaining
            && matches!(candidate.kind, MatchKind::Exact | MatchKind::Sentence);
        if complete {
            if !candidate.custom {
                if let Some(matches) = index().get(normalize(&candidate.text).as_str()) {
                    groups.push((position + 1, candidate.reading.clone(), collapse(matches)));
                }
            }
            if !direct_added {
                if let Some(matches) = direct {
                    groups.push((position + 1, raw.to_owned(), collapse(matches)));
                }
                direct_added = true;
            }
        }
    }
    if !direct_added {
        if let Some(matches) = direct {
            groups.push((0, raw.to_owned(), collapse(matches)));
        }
    }
    // Give each complete keyword a turn before broad annotations spend the
    // global budget. Keep each symbol beside its original word candidate.
    let mut additions = BTreeMap::<usize, Vec<Candidate>>::new();
    let mut count = 0;
    let mut depth = 0;
    while count < MAX_SUGGESTIONS {
        let mut available = false;
        for (position, reading, matches) in &groups {
            if let Some(sequence) = matches.get(depth) {
                available = true;
                if seen.insert(sequence.to_owned()) {
                    additions.entry(*position).or_default().push(Candidate {
                        text: sequence.to_owned(),
                        reading: reading.clone(),
                        consumed_bytes: raw.len(),
                        has_remaining: false,
                        kind: MatchKind::Exact,
                        custom: true,
                    });
                    count += 1;
                    if count == MAX_SUGGESTIONS {
                        break;
                    }
                }
            }
        }
        if !available {
            break;
        }
        depth += 1;
    }
    let mut result = Vec::with_capacity(candidates.len() + count);
    result.extend(additions.remove(&0).unwrap_or_default());
    for (position, candidate) in std::mem::take(candidates).into_iter().enumerate() {
        result.push(candidate);
        result.extend(additions.remove(&(position + 1)).unwrap_or_default());
    }
    *candidates = result;
}
