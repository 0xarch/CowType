// SPDX-License-Identifier: GPL-3.0-or-later
//! Deterministic dictionary index with an iterative reading matcher.
use crate::dictionary::{CustomPhrases, Dictionary};
use std::collections::{BTreeMap, BTreeSet, HashSet};
use std::sync::Arc;
mod sentence;
pub(crate) mod symbols;
pub mod wubi;

#[derive(Clone, Copy, Debug, PartialEq, Eq, PartialOrd, Ord)]
pub enum MatchKind {
    Exact,
    Sentence,
    Correction,
    Completion,
    Initials,
}

#[derive(Clone, Copy, Debug)]
pub struct QueryOptions {
    pub completion: bool,
    pub initials: bool,
}
impl Default for QueryOptions {
    fn default() -> Self {
        Self {
            completion: true,
            initials: false,
        }
    }
}

#[derive(Clone, Debug, PartialEq, Eq)]
pub struct Candidate {
    pub text: String,
    pub reading: String,
    /// UTF-8 bytes relative to this query's original input, never display separators.
    pub consumed_bytes: usize,
    pub has_remaining: bool,
    pub kind: MatchKind,
    pub custom: bool,
}

#[derive(Clone, Debug, Default)]
struct ReadingNode {
    children: BTreeMap<Arc<str>, usize>,
    entries: Vec<usize>,
}

pub trait CandidateProvider: std::fmt::Debug + Send + Sync {
    fn query(
        &self,
        raw: &str,
        sentence: bool,
        correction: bool,
        completion: bool,
        initials: bool,
        correction_limit: usize,
        shuangpin: bool,
    ) -> Vec<Candidate>;
}
#[derive(Clone, Debug)]
pub struct Engine {
    provider: Option<Arc<dyn CandidateProvider>>,
    pub(crate) shuangpin_semicolon: bool,
    dictionary: Dictionary,
    syllables: Vec<Arc<str>>,
    index: BTreeMap<String, Vec<usize>>,
    reading_nodes: Vec<ReadingNode>,
    wubi: Option<wubi::WubiTable>,
    custom: CustomPhrases,
    options: QueryOptions,
    log_total: f64,
}

impl Engine {
    pub fn new(dictionary: Dictionary) -> Self {
        let mut syllables: Vec<_> = dictionary
            .entries
            .iter()
            .flat_map(|e| e.reading.iter().cloned())
            .collect();
        syllables.sort();
        syllables.dedup();
        let mut index: BTreeMap<String, Vec<usize>> = BTreeMap::new();
        for (position, entry) in dictionary.entries.iter().enumerate() {
            index
                .entry(entry.reading.join(""))
                .or_default()
                .push(position);
        }
        let mut reading_nodes = vec![ReadingNode::default()];
        for (position, entry) in dictionary.entries.iter().enumerate() {
            let mut node = 0;
            for syllable in &entry.reading {
                node = if let Some(&next) = reading_nodes[node].children.get(syllable) {
                    next
                } else {
                    let next = reading_nodes.len();
                    reading_nodes.push(ReadingNode::default());
                    reading_nodes[node].children.insert(syllable.clone(), next);
                    next
                };
            }
            reading_nodes[node].entries.push(position);
        }
        let max_log = dictionary
            .entries
            .iter()
            .map(|e| e.frequency.ln())
            .fold(0.0, f64::max);
        let log_total = max_log
            + dictionary
                .entries
                .iter()
                .map(|e| (e.frequency.ln() - max_log).exp())
                .sum::<f64>()
                .max(1.0)
                .ln();
        Self {
            provider: None,
            shuangpin_semicolon: false,
            dictionary,
            syllables,
            index,
            reading_nodes,
            wubi: None,
            custom: CustomPhrases::default(),
            options: QueryOptions::default(),
            log_total,
        }
    }
    pub fn from_wubi(table: wubi::WubiTable) -> Self {
        let mut engine = Self::new(Dictionary {
            entries: Vec::new(),
        });
        engine.wubi = Some(table);
        engine
    }
    pub fn set_provider(&mut self, provider: Arc<dyn CandidateProvider>) {
        self.provider = Some(provider);
    }
    pub fn set_options(&mut self, options: QueryOptions) {
        self.options = options;
    }
    pub fn replace_custom(&mut self, phrases: CustomPhrases) {
        self.custom = phrases;
    }

    /// A query is read-only. All results own their text and can outlive this engine.
    /// No arbitrary output cap: pagination belongs to the session, not conversion.
    pub fn query(&self, raw: &str) -> Vec<Candidate> {
        self.query_base(raw, self.options)
    }
    fn query_base(&self, raw: &str, query_options: QueryOptions) -> Vec<Candidate> {
        if let Some(table) = &self.wubi {
            return table.query(raw);
        }
        let trimmed = raw.trim_ascii();
        if trimmed.is_empty()
            || !trimmed
                .bytes()
                .all(|b| b.is_ascii_alphabetic() || b == b'\'' || b == b' ')
        {
            return Vec::new();
        }
        let input = trimmed.to_ascii_lowercase();
        // Empty forced syllables are not silently normalized away.
        if input.starts_with(['\'', ' '])
            || input.ends_with('\'')
            || input
                .as_bytes()
                .windows(2)
                .any(|p| is_boundary(p[0]) && is_boundary(p[1]))
        {
            return Vec::new();
        }
        let start = raw.len() - raw.trim_ascii_start().len();
        let mut ranked = Vec::new();
        let compact: String = input
            .chars()
            .filter(|ch| !matches!(ch, '\'' | ' '))
            .collect();
        let mut positions = BTreeSet::new();
        if query_options.initials {
            positions.extend(self.initial_positions(&input, query_options));
        } else {
            for end in 1..=compact.len() {
                if let Some(found) = self.index.get(&compact[..end]) {
                    positions.extend(found);
                }
            }
            if query_options.completion {
                let max_syllable = self.syllables.iter().map(|s| s.len()).max().unwrap_or(0);
                for split in compact.len().saturating_sub(max_syllable)..compact.len() {
                    for syllable in &self.syllables {
                        if syllable.len() > compact.len() - split
                            && syllable.starts_with(&compact[split..])
                        {
                            let key = format!("{}{syllable}", &compact[..split]);
                            if let Some(found) = self.index.get(&key) {
                                positions.extend(found);
                            }
                        }
                    }
                }
            }
        }
        for (custom, entries) in [
            (true, &self.custom.entries),
            (false, &self.dictionary.entries),
        ] {
            let selected_positions: Vec<usize> = if custom {
                (0..entries.len()).collect()
            } else {
                positions.iter().copied().collect()
            };
            for order in selected_positions {
                let entry = &entries[order];
                let options = if custom {
                    QueryOptions {
                        completion: false,
                        initials: false,
                    }
                } else {
                    query_options
                };
                for (end, kind) in match_reading(&input, &entry.reading, options) {
                    let full = end == input.len();
                    if custom && !full {
                        continue;
                    }
                    if !custom
                        && !full
                        && kind == MatchKind::Exact
                        && !is_boundary(input.as_bytes()[end - 1])
                    {
                        let last = entry.reading.last().expect("validated nonempty reading");
                        let last_start = end - last.len();
                        let crosses_syllable = self.syllables.iter().any(|syllable| {
                            syllable.len() > last.len()
                                && input[last_start..].starts_with(syllable.as_ref())
                        });
                        // Preserve real ambiguity (xi + an / xian), but do not offer
                        // shan + ghai when only shang + hai is declared by the data.
                        if crosses_syllable && !self.can_segment_tail(&input[end..], query_options)
                        {
                            continue;
                        }
                    }
                    ranked.push((
                        Candidate {
                            text: entry.text.clone(),
                            reading: entry.reading.join(" "),
                            consumed_bytes: if full { raw.len() } else { start + end },
                            has_remaining: !full,
                            kind,
                            custom,
                        },
                        entry.frequency,
                        order,
                    ));
                }
            }
        }
        if ranked
            .iter()
            .any(|(candidate, _, _)| !candidate.has_remaining && candidate.kind == MatchKind::Exact)
        {
            ranked.retain(|(candidate, _, _)| {
                !(candidate.has_remaining && candidate.kind == MatchKind::Initials)
            });
        }
        ranked.sort_by(|(a, af, ai), (b, bf, bi)| {
            (!a.custom)
                .cmp(&!b.custom)
                .then(a.has_remaining.cmp(&b.has_remaining))
                .then(a.kind.cmp(&b.kind))
                .then(bf.total_cmp(af))
                .then(ai.cmp(bi))
                .then(a.consumed_bytes.cmp(&b.consumed_bytes))
        });
        let mut seen = HashSet::new();
        ranked
            .into_iter()
            .filter_map(|(candidate, _, _)| {
                // Same text with a different consumed range is a different selectable action.
                if seen.insert((candidate.text.clone(), candidate.consumed_bytes)) {
                    Some(candidate)
                } else {
                    None
                }
            })
            .collect()
    }

    // Traverse only compatible syllable edges, never scan the complete dictionary per key.
    fn initial_positions(&self, input: &str, options: QueryOptions) -> BTreeSet<usize> {
        let mut positions = BTreeSet::new();
        let mut pending = vec![(0usize, 0usize)];
        let mut visited = HashSet::new();
        while let Some((node, offset)) = pending.pop() {
            if !visited.insert((node, offset)) {
                continue;
            }
            positions.extend(&self.reading_nodes[node].entries);
            if offset >= input.len() {
                continue;
            }
            let tail = &input[offset..];
            for (syllable, &next) in &self.reading_nodes[node].children {
                let mut lengths = Vec::with_capacity(4);
                if tail.starts_with(syllable.as_ref()) {
                    lengths.push(syllable.len());
                }
                if tail.starts_with(syllable.as_bytes()[0] as char) {
                    lengths.push(1);
                }
                if ["zh", "ch", "sh"]
                    .iter()
                    .any(|head| syllable.starts_with(head))
                    && syllable.len() > 2
                    && tail.starts_with(&syllable[..2])
                {
                    lengths.push(2);
                }
                if options.completion && syllable.starts_with(tail) {
                    lengths.push(tail.len());
                }
                for length in lengths {
                    let mut end = offset + length;
                    if end < input.len() && is_boundary(input.as_bytes()[end]) {
                        end += 1;
                    }
                    pending.push((next, end));
                }
            }
        }
        positions
    }

    fn can_segment_tail(&self, input: &str, options: QueryOptions) -> bool {
        let mut reachable = vec![false; input.len() + 1];
        reachable[0] = true;
        for start in 0..input.len() {
            if !reachable[start] {
                continue;
            }
            for syllable in &self.syllables {
                for (length, _) in
                    match_reading(&input[start..], std::slice::from_ref(syllable), options)
                {
                    reachable[start + length] = true;
                }
            }
        }
        reachable[input.len()]
    }
}

fn is_boundary(b: u8) -> bool {
    b == b'\'' || b == b' '
}

fn match_reading(
    input: &str,
    reading: &[Arc<str>],
    options: QueryOptions,
) -> Vec<(usize, MatchKind)> {
    let mut states = BTreeMap::from([(0, MatchKind::Exact)]);
    for (index, syllable) in reading.iter().enumerate() {
        let mut next = BTreeMap::new();
        for (&position, &kind) in &states {
            let remaining = &input[position..];
            let mut advance = |length: usize, new_kind: MatchKind| {
                let mut end = position + length;
                if end < input.len() && is_boundary(input.as_bytes()[end]) {
                    end += 1;
                }
                let new_kind = kind.max(new_kind);
                next.entry(end)
                    .and_modify(|k: &mut MatchKind| *k = (*k).min(new_kind))
                    .or_insert(new_kind);
            };
            if remaining.starts_with(syllable.as_ref()) {
                advance(syllable.len(), MatchKind::Exact);
            }
            if options.initials && remaining.starts_with(syllable.as_bytes()[0] as char) {
                advance(1, MatchKind::Initials);
            }
            if options.initials
                && syllable.len() > 2
                && ["zh", "ch", "sh"]
                    .iter()
                    .any(|head| syllable.starts_with(head))
                && remaining.starts_with(&syllable[..2])
            {
                advance(2, MatchKind::Initials);
            }
            if options.completion
                && index + 1 == reading.len()
                && !remaining.is_empty()
                && syllable.starts_with(remaining)
                && remaining.len() < syllable.len()
            {
                advance(remaining.len(), MatchKind::Completion);
            }
        }
        states = next;
        if states.is_empty() {
            break;
        }
    }
    states.into_iter().collect()
}
