// SPDX-License-Identifier: GPL-3.0-or-later
//! Bounded unigram lattice and single-edit spelling search.
//! All readings/frequencies come from the selected public dictionary, not a model.
use super::{match_reading, Candidate, Engine, MatchKind, QueryOptions};
use std::collections::{BTreeMap, HashSet, VecDeque};
#[derive(Clone)]
struct Edge {
    end: usize,
    entry: usize,
    corrected: bool,
}
#[derive(Clone)]
struct Path {
    text: String,
    reading: String,
    cost: f64,
    words: usize,
    corrected: bool,
}
impl Engine {
    pub fn is_wubi(&self) -> bool {
        self.wubi.is_some()
    }
    pub fn query_features(
        &self,
        raw: &str,
        sentence: bool,
        correction: bool,
        completion: bool,
    ) -> Vec<Candidate> {
        self.query_configured(raw, sentence, correction, completion, self.options.initials)
    }
    pub fn query_configured(
        &self,
        raw: &str,
        sentence: bool,
        correction: bool,
        completion: bool,
        initials: bool,
    ) -> Vec<Candidate> {
        self.query_configured_with_limit(raw, sentence, correction, completion, initials, 5)
    }
    pub fn query_configured_with_limit(
        &self,
        raw: &str,
        sentence: bool,
        correction: bool,
        completion: bool,
        initials: bool,
        correction_limit: usize,
    ) -> Vec<Candidate> {
        if let Some(provider) = &self.provider {
            return provider.query(
                raw,
                sentence,
                correction,
                completion,
                initials,
                correction_limit,
            );
        }
        let mut candidates = self.query_base(
            raw,
            QueryOptions {
                completion,
                initials,
            },
        );
        if self.is_wubi() || !sentence && !correction || raw.len() > 128 {
            return candidates;
        }
        let input = raw.trim_ascii().to_ascii_lowercase();
        if input.len() < 3
            || !input
                .bytes()
                .all(|b| b.is_ascii_lowercase() || b == b'\'' || b == b' ')
            || input.starts_with(['\'', ' '])
            || input.ends_with(['\'', ' '])
            || input
                .as_bytes()
                .windows(2)
                .any(|p| super::is_boundary(p[0]) && super::is_boundary(p[1]))
        {
            return candidates;
        }
        // Known complete readings, including intentional mixed initials, outrank spelling guesses.
        let correct_full = candidates
            .iter()
            .any(|c| !c.has_remaining && matches!(c.kind, MatchKind::Exact | MatchKind::Initials));
        if correct_full {
            return candidates;
        }
        let mut lattice: Vec<Vec<Path>> = vec![Vec::new(); input.len() + 1];
        lattice[0].push(Path {
            text: String::new(),
            reading: String::new(),
            cost: 0.,
            words: 0,
            corrected: false,
        });
        let mut budget = 24000usize;
        for start in 0..input.len() {
            if lattice[start].is_empty() || super::is_boundary(input.as_bytes()[start]) {
                continue;
            }
            let mut edges = self.exact_edges(&input, start);
            if correction && !input.contains(['\'', ' ']) && input.len() <= 64 {
                edges.extend(self.corrected_edges(&input, start, &mut budget));
            }
            let paths = lattice[start].clone();
            for edge in edges {
                let entry = &self.dictionary.entries[edge.entry];
                let penalty = (self.log_total - entry.frequency.ln()).max(0.001);
                for previous in &paths {
                    if previous.corrected && edge.corrected || !sentence && previous.words > 0 {
                        continue;
                    }
                    let reading = entry.reading.join(" ");
                    let path = Path {
                        text: format!("{}{}", previous.text, entry.text),
                        reading: if previous.reading.is_empty() {
                            reading
                        } else {
                            format!("{} {reading}", previous.reading)
                        },
                        cost: previous.cost
                            + penalty
                            + if edge.corrected {
                                self.log_total.max(1.)
                            } else {
                                0.
                            },
                        words: previous.words + 1,
                        corrected: previous.corrected || edge.corrected,
                    };
                    let dest = &mut lattice[edge.end];
                    if dest.iter().any(|p| {
                        p.text == path.text && p.corrected == path.corrected && p.cost <= path.cost
                    }) {
                        continue;
                    }
                    dest.push(path);
                    dest.sort_by(|a, b| a.cost.total_cmp(&b.cost).then(a.text.cmp(&b.text)));
                    dest.truncate(8);
                }
            }
        }
        let mut extra = Vec::new();
        let mut seen = HashSet::new();
        let mut corrected_count = 0;
        let mut sentence_count = 0;
        for path in &lattice[input.len()] {
            if path.words < 2 && !path.corrected || !seen.insert(path.text.clone()) {
                continue;
            }
            if path.corrected {
                if corrected_count >= correction_limit {
                    continue;
                }
                corrected_count += 1;
            } else {
                if sentence_count >= 5 {
                    continue;
                }
                sentence_count += 1;
            }
            extra.push(Candidate {
                text: path.text.clone(),
                reading: path.reading.clone(),
                consumed_bytes: raw.len(),
                has_remaining: false,
                kind: if path.corrected {
                    MatchKind::Correction
                } else {
                    MatchKind::Sentence
                },
                custom: false,
            });
        }
        // Whole-sentence paths precede incomplete prefixes; explicit custom expansions stay first.
        let custom = candidates.partition_point(|c| c.custom);
        candidates.splice(custom..custom, extra);
        let mut seen = HashSet::new();
        candidates.retain(|c| seen.insert((c.text.clone(), c.consumed_bytes)));
        candidates
    }
    fn exact_edges(&self, input: &str, start: usize) -> Vec<Edge> {
        let mut key = String::new();
        let mut result = Vec::new();
        for end in start..input.len().min(start + 96) {
            let b = input.as_bytes()[end];
            if !super::is_boundary(b) {
                key.push(b as char);
            }
            if super::is_boundary(b) {
                continue;
            }
            let Some(rows) = self.index.get(&key) else {
                continue;
            };
            let mut selected: Vec<_> = rows
                .iter()
                .copied()
                .filter(|&i| {
                    match_reading(
                        &input[start..=end],
                        &self.dictionary.entries[i].reading,
                        QueryOptions {
                            completion: false,
                            initials: false,
                        },
                    )
                    .iter()
                    .any(|(n, _)| *n == end + 1 - start)
                })
                .collect();
            selected.sort_by(|&a, &b| {
                self.dictionary.entries[b]
                    .frequency
                    .total_cmp(&self.dictionary.entries[a].frequency)
                    .then(a.cmp(&b))
            });
            selected.truncate(6);
            let mut consumed = end + 1;
            if consumed < input.len() && super::is_boundary(input.as_bytes()[consumed]) {
                consumed += 1;
            }
            result.extend(selected.into_iter().map(|entry| Edge {
                end: consumed,
                entry,
                corrected: false,
            }));
        }
        result
    }
    fn corrected_edges(&self, input: &str, start: usize, budget: &mut usize) -> Vec<Edge> {
        // One Damerau edit within a dictionary word; at most one corrected word per path.
        // Prefix pruning uses the existing index, so no second inherited spelling table exists.
        let tail = &input.as_bytes()[start..];
        let mut queue = VecDeque::from([(String::new(), 0usize, false)]);
        let mut visited = HashSet::new();
        let mut result = Vec::new();
        let mut local = 0;
        while let Some((key, pos, edited)) = queue.pop_front() {
            if *budget == 0 || local >= 1800 {
                break;
            }
            *budget -= 1;
            local += 1;
            if !visited.insert((key.clone(), pos, edited)) {
                continue;
            }
            if edited && pos >= 3 && key.len() >= 3 {
                if let Some(rows) = self.index.get(&key) {
                    let mut rows = rows.clone();
                    rows.sort_by(|&a, &b| {
                        self.dictionary.entries[b]
                            .frequency
                            .total_cmp(&self.dictionary.entries[a].frequency)
                            .then(a.cmp(&b))
                    });
                    rows.truncate(4);
                    result.extend(rows.into_iter().map(|entry| Edge {
                        end: start + pos,
                        entry,
                        corrected: true,
                    }));
                }
            }
            if key.len() >= 24 || pos > tail.len() {
                continue;
            }
            let prefix_exists = |s: &String| {
                self.index
                    .range(s.clone()..)
                    .next()
                    .is_some_and(|(k, _)| k.starts_with(s))
            };
            let append = |next: String, next_pos: usize, edited: bool, queue: &mut VecDeque<_>| {
                if prefix_exists(&next) {
                    queue.push_back((next, next_pos, edited));
                }
            };
            if let Some(&ch) = tail.get(pos) {
                let mut next = key.clone();
                next.push(ch as char);
                append(next, pos + 1, edited, &mut queue);
            }
            if edited {
                continue;
            }
            if pos < tail.len() {
                queue.push_back((key.clone(), pos + 1, true));
            }
            if pos + 1 < tail.len() && tail[pos] != tail[pos + 1] {
                let mut next = key.clone();
                next.push(tail[pos + 1] as char);
                next.push(tail[pos] as char);
                append(next, pos + 2, true, &mut queue);
            }
            for ch in b'a'..=b'z' {
                let mut next = key.clone();
                next.push(ch as char);
                if pos < tail.len() && ch != tail[pos] {
                    append(next.clone(), pos + 1, true, &mut queue);
                }
                append(next, pos, true, &mut queue);
            }
        }
        let mut unique = BTreeMap::new();
        for e in result {
            unique.entry((e.end, e.entry)).or_insert(e);
        }
        unique.into_values().collect()
    }
}
