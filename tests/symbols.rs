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
        ("thumbsupmediumskintone", "👍🏽"),
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
