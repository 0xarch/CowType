// SPDX-License-Identifier: GPL-3.0-or-later
//! One input field, one session. Frames own text; commits occur only on returned events.
mod punctuation;
use crate::engine::{Candidate, Engine};
use crate::learning::{SharedLearning, Undo};
use punctuation::Punctuation;
use std::sync::Arc;

#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub enum Mode {
    Chinese,
    English,
}

#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub enum Key {
    Character(char),
    Space,
    Enter,
    Escape,
    Backspace,
    Delete,
    Left,
    Right,
    Home,
    End,
    PreviousCandidate,
    NextCandidate,
    PageUp,
    PageDown,
    ToggleMode,
    Other,
}

#[derive(Clone, Copy, Debug, Default)]
pub struct Modifiers {
    pub control: bool,
    pub alt: bool,
    pub super_key: bool,
    pub shift: bool,
}

#[derive(Clone, Copy, Debug)]
pub struct Event {
    pub key: Key,
    pub pressed: bool,
    pub modifiers: Modifiers,
    /// Only set when the platform supplies trustworthy surrounding text.
    pub before_cursor: Option<char>,
}
impl Event {
    pub fn press(key: Key) -> Self {
        Self {
            key,
            pressed: true,
            modifiers: Modifiers::default(),
            before_cursor: None,
        }
    }
}

#[derive(Clone, Copy, Debug)]
pub struct SessionConfig {
    page_size: usize,
    pub chinese_punctuation: bool,
    pub sentence: bool,
    pub correction: bool,
    pub completion: bool,
    pub initials: bool,
    pub learning: bool,
    pub emoji: bool,
}
impl Default for SessionConfig {
    fn default() -> Self {
        Self {
            page_size: 5,
            chinese_punctuation: true,
            sentence: false,
            correction: false,
            completion: true,
            initials: false,
            learning: false,
            emoji: false,
        }
    }
}
impl SessionConfig {
    pub fn with_page_size(page_size: usize) -> Result<Self, &'static str> {
        if !(3..=9).contains(&page_size) {
            return Err("page size must be between 3 and 9");
        }
        Ok(Self {
            page_size,
            ..Self::default()
        })
    }
    pub fn page_size(&self) -> usize {
        self.page_size
    }
}

#[derive(Clone, Copy, Debug)]
pub enum ResetReason {
    FocusLost,
    ApplicationReset,
    InputMethodSwitch,
}

#[derive(Clone, Debug)]
pub struct Frame {
    pub handled: bool,
    pub commit: Option<String>,
    pub preedit: String,
    pub preedit_cursor_bytes: usize,
    pub candidates: Vec<Candidate>,
    pub candidate_total: usize,
    pub page_index: usize,
    /// Index within the visible page, distinct from the engine's global index.
    pub candidate_cursor: Option<usize>,
    pub has_previous_page: bool,
    pub has_next_page: bool,
    pub mode: Mode,
    pub engine_ready: bool,
}

#[derive(Debug)]
struct Selection {
    reading: String,
    custom: bool,
    text: String,
    raw: String,
}

#[derive(Debug)]
pub struct Session {
    engine: Option<Arc<Engine>>,
    config: SessionConfig,
    mode: Mode,
    remaining: String,
    cursor: usize,
    selections: Vec<Selection>,
    candidates: Vec<Candidate>,
    selected_candidate: usize,
    learning: Option<SharedLearning>,
    last_undo: Vec<Undo>,
    punctuation: Punctuation,
}

impl Session {
    pub fn new(engine: Option<Arc<Engine>>, config: SessionConfig) -> Self {
        Self {
            engine,
            config,
            mode: Mode::Chinese,
            remaining: String::new(),
            cursor: 0,
            selections: Vec::new(),
            candidates: Vec::new(),
            selected_candidate: 0,
            learning: None,
            last_undo: Vec::new(),
            punctuation: Punctuation::default(),
        }
    }
    pub fn set_learning(&mut self, learning: Option<SharedLearning>) {
        self.learning = learning;
        self.refresh();
    }
    pub fn forget_current(&mut self) -> bool {
        if !self.config.learning || self.engine.as_ref().is_none_or(|e| e.is_wubi()) {
            return false;
        }
        let Some(candidate) = self.candidates.get(self.selected_candidate) else {
            return false;
        };
        if candidate.custom {
            return false;
        }
        let changed = self
            .learning
            .as_ref()
            .and_then(|l| l.lock().ok())
            .is_some_and(|mut l| l.forget(&candidate.reading, &candidate.text));
        self.last_undo.clear();
        let cursor = self.selected_candidate;
        self.refresh();
        self.selected_candidate = cursor.min(self.candidates.len().saturating_sub(1));
        changed
    }
    pub fn remaining(&self) -> &str {
        &self.remaining
    }
    pub fn selected_text(&self) -> String {
        self.selections.iter().map(|s| s.text.as_str()).collect()
    }
    pub fn input(&self) -> String {
        let mut input: String = self.selections.iter().map(|s| s.raw.as_str()).collect();
        input.push_str(&self.remaining);
        input
    }
    pub fn set_engine(&mut self, engine: Option<Arc<Engine>>) {
        self.engine = engine;
        self.refresh();
    }
    pub fn configure(&mut self, config: SessionConfig) {
        let selected = self.selected_candidate;
        self.config = config;
        if !config.chinese_punctuation {
            self.punctuation.reset();
        }
        if !config.learning {
            self.last_undo.clear();
        }
        self.refresh();
        self.selected_candidate = selected.min(self.candidates.len().saturating_sub(1));
    }

    /// Read-only frames never repeat a prior commit. Consumers retain owned event frames.
    pub fn frame(&self) -> Frame {
        self.snapshot(false, None)
    }

    pub fn reset(&mut self, reason: ResetReason) -> Frame {
        self.punctuation.reset();
        let commit = if matches!(reason, ResetReason::InputMethodSwitch) {
            self.raw_commit()
        } else {
            None
        };
        if matches!(reason, ResetReason::InputMethodSwitch) {
            self.learn_selected(None);
        } else {
            self.last_undo.clear();
        }
        self.clear();
        self.snapshot(false, commit)
    }

    /// Zero-based index in the current visible page.
    pub fn select(&mut self, visible_index: usize) -> Frame {
        if visible_index >= self.config.page_size {
            return self.frame();
        }
        let global = self.page_start().checked_add(visible_index);
        match global {
            Some(index) => self.choose(index),
            None => self.frame(),
        }
    }

    /// Zero-based index in the entire candidate collection.
    pub fn choose(&mut self, global_index: usize) -> Frame {
        let Some(candidate) = self.candidates.get(global_index).cloned() else {
            return self.frame();
        };
        let raw: String = self.remaining.drain(..candidate.consumed_bytes).collect();
        self.selections.push(Selection {
            reading: candidate.reading,
            custom: candidate.custom,
            text: candidate.text,
            raw,
        });
        self.cursor = self.remaining.len();
        let commit = if self.remaining.is_empty() {
            let text = self.selected_text();
            self.learn_selected(None);
            self.clear();
            Some(text)
        } else {
            self.refresh();
            None
        };
        self.snapshot(true, commit)
    }

    pub fn key(&mut self, event: Event) -> Frame {
        if !event.pressed
            || event.modifiers.control
            || event.modifiers.alt
            || event.modifiers.super_key
            || (event.modifiers.shift && !matches!(event.key, Key::Character(_)))
        {
            return self.frame();
        }
        if !self.active() && event.key == Key::Backspace {
            if let Some(learning) = &self.learning {
                if let Ok(mut l) = learning.lock() {
                    let _ = l.undo(&self.last_undo);
                }
            }
            self.last_undo.clear();
            return self.frame();
        }
        if event.key != Key::Backspace {
            self.last_undo.clear();
        }
        if event.key == Key::ToggleMode {
            self.punctuation.reset();
            let commit = self.raw_commit();
            self.learn_selected(None);
            self.clear();
            self.mode = if self.mode == Mode::Chinese {
                Mode::English
            } else {
                Mode::Chinese
            };
            return self.snapshot(true, commit);
        }
        if self.mode == Mode::English {
            return self.frame();
        }
        let active = self.active();
        match event.key {
            Key::Character(ch) if ch.is_ascii_alphabetic() || (ch == '\'' && active) => {
                self.remaining.insert(self.cursor, ch);
                self.cursor += ch.len_utf8();
                self.refresh();
            }
            Key::Character(ch) if active && ch.is_ascii_digit() => {
                let digit = ch as usize - '0' as usize;
                if digit > 0 && digit <= self.config.page_size {
                    let frame = self.select(digit - 1);
                    // Invalid positions are consumed during composition as well.
                    return Frame {
                        handled: true,
                        ..frame
                    };
                }
            }
            Key::Space if active => {
                if !self.candidates.is_empty() {
                    return self.choose(self.selected_candidate);
                }
                let commit = self.raw_commit();
                self.learn_selected(None);
                self.clear();
                return self.snapshot(true, commit);
            }
            Key::Enter if active => {
                let commit = self.raw_commit();
                self.learn_selected(None);
                self.clear();
                return self.snapshot(true, commit);
            }
            Key::Escape if active => self.clear(),
            Key::Backspace if active => {
                if let Some(selection) = self.selections.pop() {
                    self.remaining.insert_str(0, &selection.raw);
                    self.cursor = self.remaining.len();
                } else if self.cursor > 0 {
                    self.cursor -= 1; // Composition contains ASCII keys only.
                    self.remaining.remove(self.cursor);
                }
                self.refresh();
            }
            Key::Delete if active => {
                if self.cursor < self.remaining.len() {
                    self.remaining.remove(self.cursor);
                }
                self.refresh();
            }
            Key::Left if active => self.cursor = self.cursor.saturating_sub(1),
            Key::Right if active => self.cursor = (self.cursor + 1).min(self.remaining.len()),
            Key::Home if active => self.cursor = 0,
            Key::End if active => self.cursor = self.remaining.len(),
            Key::PreviousCandidate if active => {
                self.selected_candidate = self.selected_candidate.saturating_sub(1)
            }
            Key::NextCandidate if active => {
                self.selected_candidate =
                    (self.selected_candidate + 1).min(self.candidates.len().saturating_sub(1));
            }
            Key::PageUp if active => {
                self.selected_candidate = self.page_start().saturating_sub(self.config.page_size)
            }
            Key::PageDown if active => {
                if self.page_start() + self.config.page_size < self.candidates.len() {
                    self.selected_candidate = self.page_start() + self.config.page_size;
                }
            }
            Key::Character(ch) if Punctuation::has(ch) => {
                let use_chinese = self.config.chinese_punctuation
                    && (active
                        || !matches!(ch, '.' | ',')
                        || !event
                            .before_cursor
                            .is_some_and(|c| c.is_ascii_alphanumeric()));
                if !active && !use_chinese {
                    return self.frame();
                }
                let mut commit = self.selected_text();
                if let Some(candidate) = self.candidates.get(self.selected_candidate) {
                    commit.push_str(&candidate.text);
                    commit.push_str(&self.remaining[candidate.consumed_bytes..]);
                } else {
                    commit.push_str(&self.remaining);
                }
                let candidate = self.candidates.get(self.selected_candidate).cloned();
                self.learn_selected(candidate.as_ref());
                if use_chinese {
                    commit.push_str(self.punctuation.convert(ch));
                } else {
                    commit.push(ch);
                }
                self.clear();
                return self.snapshot(true, Some(commit));
            }
            _ => return self.frame(),
        }
        self.snapshot(true, None)
    }

    fn active(&self) -> bool {
        !self.remaining.is_empty() || !self.selections.is_empty()
    }
    fn page_start(&self) -> usize {
        self.selected_candidate / self.config.page_size * self.config.page_size
    }
    fn refresh(&mut self) {
        self.candidates = self
            .engine
            .as_ref()
            .map(|e| {
                e.query_configured(
                    &self.remaining,
                    self.config.sentence,
                    self.config.correction,
                    self.config.completion,
                    self.config.initials,
                )
            })
            .unwrap_or_default();
        if self.config.learning && self.engine.as_ref().is_some_and(|e| !e.is_wubi()) {
            if let Some(learning) = &self.learning {
                if let Ok(l) = learning.lock() {
                    let now = crate::learning::now();
                    self.candidates.sort_by(|a, b| {
                        a.has_remaining
                            .cmp(&b.has_remaining)
                            .then(b.custom.cmp(&a.custom))
                            .then(a.kind.cmp(&b.kind))
                            .then_with(|| {
                                l.score(&b.reading, &b.text, now)
                                    .cmp(&l.score(&a.reading, &a.text, now))
                            })
                    });
                }
            }
        }
        if self.config.emoji {
            crate::engine::symbols::augment(&self.remaining, &mut self.candidates);
        }
        self.selected_candidate = 0;
    }
    fn learn_selected(&mut self, extra: Option<&Candidate>) {
        if !self.config.learning || self.engine.as_ref().is_none_or(|e| e.is_wubi()) {
            return;
        }
        let Some(learning) = &self.learning else {
            return;
        };
        let Ok(mut l) = learning.lock() else {
            return;
        };
        let now = crate::learning::now();
        let mut updates = Vec::new();
        let mut seen = std::collections::HashSet::new();
        for selection in &self.selections {
            if !selection.custom && seen.insert((selection.reading.clone(), selection.text.clone()))
            {
                if let Some(u) = l.confirm(&selection.reading, &selection.text, now) {
                    updates.push(u);
                }
            }
        }
        if let Some(c) = extra {
            if !c.custom && seen.insert((c.reading.clone(), c.text.clone())) {
                if let Some(u) = l.confirm(&c.reading, &c.text, now) {
                    updates.push(u);
                }
            }
        }
        self.last_undo = updates;
    }
    fn clear(&mut self) {
        self.remaining.clear();
        self.selections.clear();
        self.candidates.clear();
        self.cursor = 0;
        self.selected_candidate = 0;
    }
    fn raw_commit(&self) -> Option<String> {
        if !self.active() {
            return None;
        }
        let mut text = self.selected_text();
        text.push_str(&self.remaining);
        Some(text)
    }
    fn snapshot(&self, handled: bool, commit: Option<String>) -> Frame {
        let selected = self.selected_text();
        let start = self.page_start();
        Frame {
            handled,
            commit,
            preedit_cursor_bytes: selected.len() + self.cursor,
            preedit: selected + &self.remaining,
            candidates: self
                .candidates
                .iter()
                .skip(start)
                .take(self.config.page_size)
                .cloned()
                .collect(),
            candidate_total: self.candidates.len(),
            page_index: start / self.config.page_size,
            candidate_cursor: if self.candidates.is_empty() {
                None
            } else {
                Some(self.selected_candidate - start)
            },
            has_previous_page: start > 0,
            has_next_page: start + self.config.page_size < self.candidates.len(),
            mode: self.mode,
            engine_ready: self.engine.is_some(),
        }
    }
}
