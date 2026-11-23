// SPDX-License-Identifier: GPL-3.0-or-later
//! Offline CLDR annotations. Bundled Unicode data retains its Unicode-3.0 license.
use super::{Candidate, MatchKind};
use std::collections::{BTreeMap, HashSet};
use std::sync::OnceLock;

const KEYWORDS: &str = include_str!("../../src-data-tools/emoji/keywords.tsv");
const EMOJI_SEQUENCES: &str = include_str!("../../src-data-tools/emoji/emoji-sequences.txt");
static EMOJI: OnceLock<HashSet<&'static str>> = OnceLock::new();
fn is_emoji(sequence: &str) -> bool {
    EMOJI
        .get_or_init(|| EMOJI_SEQUENCES.lines().collect())
        .contains(sequence)
}
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
pub(crate) fn augment(
    raw: &str,
    candidates: &mut Vec<Candidate>,
    default_tone: u8,
    emoji_first: bool,
    symbol_limit: usize,
    emoji_limit: usize,
) {
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
    // Insert one ranked supplement block after the first matched word. Putting
    // each group back beside its source word would undo the global priority
    // (the English keyword "you" could put couple emoji before Chinese arrows).
    let insertion = groups.iter().map(|g| g.0).min().unwrap_or(0);
    let mut additions = Vec::new();
    let capacity = symbol_limit.min(12) + emoji_limit.min(12);
    // Allocate the preferred class first across all matching keywords, before
    // each class cap. Retain fair turns and source order within each class.
    for class in [emoji_first, !emoji_first] {
        let class_limit = if class { emoji_limit } else { symbol_limit }.min(12);
        let eligible: Vec<_> = groups
            .iter()
            .map(|(position, reading, matches)| {
                (
                    *position,
                    reading,
                    matches
                        .iter()
                        .filter(|s| is_emoji(s) == class)
                        .collect::<Vec<_>>(),
                )
            })
            .collect();
        let mut depth = 0;
        let mut class_count = 0;
        while class_count < class_limit {
            let mut available = false;
            for (_, reading, matches) in &eligible {
                if let Some(sequence) = matches.get(depth) {
                    available = true;
                    if seen.insert((*sequence).clone()) {
                        additions.push(Candidate {
                            text: (*sequence).clone(),
                            reading: (*reading).clone(),
                            consumed_bytes: raw.len(),
                            has_remaining: false,
                            kind: MatchKind::Exact,
                            custom: true,
                        });
                        class_count += 1;
                        if class_count == class_limit {
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
    }
    let mut result = Vec::with_capacity(candidates.len() + capacity);
    if insertion == 0 {
        result.append(&mut additions);
    }
    for (position, candidate) in std::mem::take(candidates).into_iter().enumerate() {
        result.push(candidate);
        if position + 1 == insertion {
            result.append(&mut additions);
        }
    }
    *candidates = result;
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn symbol_and_emoji_limits_are_independent() {
        let mut candidates = Vec::new();
        augment("right", &mut candidates, 0, false, 3, 2);
        let symbols = candidates.iter().filter(|c| !is_emoji(&c.text)).count();
        let emoji = candidates.iter().filter(|c| is_emoji(&c.text)).count();
        assert_eq!(symbols, 3);
        assert_eq!(emoji, 2);

        let mut candidates = Vec::new();
        augment("right", &mut candidates, 0, false, 0, 0);
        assert!(candidates.is_empty());
    }
}
