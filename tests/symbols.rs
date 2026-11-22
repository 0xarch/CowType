// SPDX-License-Identifier: GPL-3.0-or-later
use cowtype::{
    dictionary::Dictionary,
    engine::{wubi::WubiTable, Engine},
    learning::Learning,
    session::{Event, Key, Session, SessionConfig},
};
use std::sync::{Arc, Mutex};

fn config(enabled: bool) -> SessionConfig {
    let mut result = SessionConfig::default();
    result.emoji = enabled;
    result.emoji_skin_tone_picker = false;
    result
}
fn session(enabled: bool) -> Session {
    let dictionary = Dictionary::from_tsv(
        "xiao lian\t笑脸\t100\nwei xiao\t微笑\t90\njia ting\t家庭\t80\nban quan\t版权\t70\n"
            .as_bytes(),
    )
    .unwrap()
    .value;
    Session::new(Some(Arc::new(Engine::new(dictionary))), config(enabled))
}
fn type_text(s: &mut Session, text: &str) {
    for c in text.chars() {
        s.key(Event::press(Key::Character(c)));
    }
}
fn candidates(s: &mut Session) -> Vec<cowtype::engine::Candidate> {
    let mut result = s.frame().candidates;
    while s.frame().has_next_page {
        s.key(Event::press(Key::PageDown));
        result.extend(s.frame().candidates);
    }
    result
}
#[test]
fn cldr_chinese_keyword_can_be_selected_and_committed_once() {
    let mut s = session(true);
    type_text(&mut s, "xiaolian");
    let rows = candidates(&mut s);
    assert_eq!(rows[0].text, "笑脸");
    let position = rows.iter().position(|c| c.text == "😀").unwrap();
    assert_eq!(rows[position].consumed_bytes, 8);
    assert!(!rows[position].has_remaining);
    assert_eq!(s.choose(position).commit.as_deref(), Some("😀"));
    assert!(s.frame().preedit.is_empty() && s.frame().commit.is_none());
    assert!(s.key(Event::press(Key::Space)).commit.is_none());
}
#[test]
fn cldr_toggle_rebuilds_current_candidates_without_changing_input() {
    let mut s = session(false);
    type_text(&mut s, "weixiao");
    assert!(!candidates(&mut s).iter().any(|c| c.text == "😊"));
    s.configure(config(true));
    assert!(candidates(&mut s).iter().any(|c| c.text == "😊"));
    s.configure(config(false));
    assert_eq!(s.remaining(), "weixiao");
    assert!(!candidates(&mut s).iter().any(|c| c.text == "😊"));
}
#[test]
fn cldr_english_names_sequences_and_symbols_remain_intact() {
    for (keyword, symbol) in [
        ("GRINNINGFACE", "😀"),
        ("thumbsup", "👍"),
        ("thumbsupmediumskintone", "👍"),
        ("copyright", "©️"),
        ("family", "👨‍👦"),
    ] {
        let mut s = session(true);
        type_text(&mut s, keyword);
        let rows = candidates(&mut s);
        assert!(rows.len() <= 12);
        let position = rows.iter().position(|c| c.text == symbol).unwrap();
        assert_eq!(s.choose(position).commit.as_deref(), Some(symbol));
    }
    let mut s = session(true);
    type_text(&mut s, "banquan");
    assert!(candidates(&mut s).iter().any(|c| c.text == "©️"));
}
#[test]
fn cldr_prefixes_typos_and_duplicates_do_not_produce_extra_suggestions() {
    for text in ["smil", "grinningfac", "xiaolia", "xiaolain"] {
        let mut s = session(true);
        type_text(&mut s, text);
        assert!(!candidates(&mut s).iter().any(|c| c.custom));
    }
    let mut s = session(true);
    type_text(&mut s, "smile");
    let rows = candidates(&mut s);
    let unique: std::collections::HashSet<_> = rows.iter().map(|c| &c.text).collect();
    assert_eq!(rows.len(), unique.len());
    assert!(rows.len() <= 12);
}
#[test]
fn symbols_are_not_learned_as_personal_words_and_wubi_uses_decoded_keywords() {
    let learning = Arc::new(Mutex::new(Learning::default()));
    let mut s = session(true);
    s.set_learning(Some(learning.clone()));
    let mut learning_config = config(true);
    learning_config.learning = true;
    s.configure(learning_config);
    type_text(&mut s, "grinningface");
    let rows = candidates(&mut s);
    let pos = rows.iter().position(|c| c.text == "😀").unwrap();
    assert_eq!(s.choose(pos).commit.as_deref(), Some("😀"));
    assert!(!learning.lock().unwrap().dirty);
    let table = WubiTable::from_tsv("ab\t微笑\t1\n".as_bytes())
        .unwrap()
        .value;
    let mut wubi = Session::new(Some(Arc::new(Engine::from_wubi(table))), config(true));
    type_text(&mut wubi, "ab");
    assert!(candidates(&mut wubi).iter().any(|c| c.text == "😊"));
}

#[test]
fn direction_aliases_match_chinese_words_and_survive_broad_keywords() {
    let dictionary =
        Dictionary::from_tsv("you\t有\t100\nyou\t右\t90\nyou jian tou\t右箭头\t80\n".as_bytes())
            .unwrap()
            .value;
    for input in ["you", "youjiantou", "rightpointingarrow"] {
        let mut s = Session::new(
            Some(Arc::new(Engine::new(dictionary.clone()))),
            config(true),
        );
        type_text(&mut s, input);
        let rows = candidates(&mut s);
        let position = rows.iter().position(|c| c.text == "→").unwrap();
        assert_eq!(s.choose(position).commit.as_deref(), Some("→"));
    }
}

#[test]
fn skin_tones_use_one_slot_before_the_global_budget() {
    for default in 0..=5 {
        let mut cfg = config(true);
        cfg.emoji_default_skin_tone = default;
        let mut s = Session::new(None, cfg);
        type_text(&mut s, "thumbsup");
        let rows = candidates(&mut s);
        assert_eq!(rows.len(), 1);
        let text = ["👍", "👍🏻", "👍🏼", "👍🏽", "👍🏾", "👍🏿"][default as usize];
        assert_eq!(rows[0].text, text);
        assert_eq!(s.choose(0).commit.as_deref(), Some(text));
    }
    let dictionary = Dictionary::from_tsv("you\t右\t10\nyou\t手\t100\nyou\t人\t90\n".as_bytes())
        .unwrap()
        .value;
    let mut s = Session::new(Some(Arc::new(Engine::new(dictionary))), config(true));
    type_text(&mut s, "you");
    let rows = candidates(&mut s);
    assert!(rows.iter().any(|c| c.text == "→"));
    let mut families = std::collections::HashSet::new();
    for row in rows.iter().filter(|c| c.custom) {
        let base: String = row
            .text
            .chars()
            .filter(|c| !('\u{1f3fb}'..='\u{1f3ff}').contains(c))
            .collect();
        assert!(families.insert(base));
    }
    assert!(rows.iter().filter(|c| c.custom).count() <= 12);
}

#[test]
fn skin_tone_child_selection_preserves_input_and_cancels_to_parent() {
    let mut cfg = config(true);
    cfg.emoji_skin_tone_picker = true;
    let mut s = Session::new(None, cfg);
    type_text(&mut s, "thumbsup");
    let opened = s.select(0);
    assert!(opened.commit.is_none());
    assert_eq!(opened.candidate_total, 6);
    assert_eq!(s.input(), "thumbsup");
    assert_eq!(s.key(Event::press(Key::Escape)).candidate_total, 1);
    s.select(0);
    assert_eq!(s.key(Event::press(Key::Backspace)).candidate_total, 1);
    assert_eq!(s.remaining(), "thumbsup");
    s.select(0);
    s.key(Event::press(Key::PageDown));
    assert_eq!(
        s.key(Event::press(Key::Character('1'))).commit.as_deref(),
        Some("👍🏿")
    );
    assert!(s.frame().candidates.is_empty());
    assert!(s.key(Event::press(Key::Space)).commit.is_none());
}

#[test]
fn skin_tone_picker_respects_default_reload_edit_and_field_reset() {
    let mut cfg = config(true);
    cfg.emoji_skin_tone_picker = true;
    cfg.emoji_default_skin_tone = 3;
    let mut s = Session::new(None, cfg);
    type_text(&mut s, "thumbsup");
    assert_eq!(s.frame().candidates[0].text, "👍🏽");
    s.select(0);
    assert_eq!(
        s.key(Event::press(Key::Enter)).commit.as_deref(),
        Some("👍🏽")
    );
    type_text(&mut s, "thumbsup");
    s.select(0);
    s.key(Event::press(Key::Character('a')));
    assert_eq!(s.input(), "thumbsupa");
    s.key(Event::press(Key::Backspace));
    s.select(0);
    cfg.emoji_skin_tone_picker = false;
    s.configure(cfg);
    assert_eq!(s.frame().candidate_total, 1);
    assert_eq!(s.select(0).commit.as_deref(), Some("👍🏽"));
    cfg.emoji_skin_tone_picker = true;
    s.configure(cfg);
    type_text(&mut s, "thumbsup");
    s.select(0);
    let reset = s.reset(cowtype::session::ResetReason::FocusLost);
    assert!(reset.commit.is_none() && reset.candidates.is_empty());
}

#[test]
fn zwj_tone_families_remain_valid_and_are_not_learned() {
    let mut cfg = config(true);
    cfg.emoji_skin_tone_picker = true;
    cfg.learning = true;
    let learning = Arc::new(Mutex::new(Learning::default()));
    let mut s = Session::new(None, cfg);
    s.set_learning(Some(learning.clone()));
    type_text(&mut s, "womanhealthworker");
    assert_eq!(s.frame().candidate_total, 1);
    assert!(s.select(0).commit.is_none());
    let choices = candidates(&mut s);
    assert_eq!(choices.len(), 6);
    assert!(choices.iter().any(|c| c.text == "👩🏽‍⚕️"));
    assert_eq!(s.choose(3).commit.as_deref(), Some("👩🏽‍⚕️"));
    assert!(!learning.lock().unwrap().dirty);
}

#[test]
fn atomic_and_zwj_handshakes_share_one_family() {
    let mut cfg = config(true);
    cfg.emoji_skin_tone_picker = true;
    let mut s = Session::new(None, cfg);
    type_text(&mut s, "handshake");
    let parent = candidates(&mut s);
    let handshake = parent.iter().position(|c| c.text == "🤝").unwrap();
    assert_eq!(
        parent
            .iter()
            .filter(
                |c| c.text.contains('🤝') || (c.text.contains('🫱') && c.text.contains('\u{200d}'))
            )
            .count(),
        1
    );
    assert!(s.choose(handshake).commit.is_none());
    let rows = candidates(&mut s);
    assert_eq!(rows.len(), 26);
    let pos = rows.iter().position(|c| c.text == "🫱🏻‍🫲🏿").unwrap();
    assert_eq!(s.choose(pos).commit.as_deref(), Some("🫱🏻‍🫲🏿"));
    cfg.emoji_skin_tone_picker = false;
    cfg.emoji_default_skin_tone = 4;
    s.configure(cfg);
    type_text(&mut s, "handshake");
    let parent = candidates(&mut s);
    let index = parent.iter().position(|c| c.text == "🤝🏾").unwrap();
    assert_eq!(s.choose(index).commit.as_deref(), Some("🤝🏾"));
}
