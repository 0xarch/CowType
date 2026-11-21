// SPDX-License-Identifier: GPL-3.0-or-later
use cowtype::{
    dictionary::Dictionary,
    engine::{Engine, MatchKind},
    session::{Event, Key, ResetReason, Session, SessionConfig},
};
use std::sync::Arc;
fn engine() -> Engine {
    Engine::new(Dictionary::from_tsv(b"ke neng\t\xe5\x8f\xaf\xe8\x83\xbd\t100\nke\t\xe5\x8f\xaf\t200\nneng\t\xe8\x83\xbd\t90\nzhong guo ren\t\xe4\xb8\xad\xe5\x9b\xbd\xe4\xba\xba\t80\nan\t\xe5\xae\x89\t50\nxi an\t\xe8\xa5\xbf\xe5\xae\x89\t10\nxian\t\xe5\x85\x88\t100\n").unwrap().value)
}
#[test]
fn initials_mixed_digraph_forced_boundaries_and_raw_consumption() {
    let engine = engine();
    for raw in ["kn", "k'n", "ken", "kneng", "ke'n", "k'neng"] {
        let result = engine.query_configured(raw, false, false, false, true);
        let candidate = result
            .iter()
            .find(|c| c.text == "可能" && !c.has_remaining)
            .unwrap_or_else(|| panic!("{raw}: {result:?}"));
        assert_eq!(candidate.consumed_bytes, raw.len());
        assert_eq!(candidate.reading, "ke neng");
        assert_eq!(candidate.kind, MatchKind::Initials);
    }
    for raw in ["zgr", "zhgr", "zhonggr", "zhongguor", "zh'g'r"] {
        assert!(
            engine
                .query_configured(raw, false, false, false, true)
                .iter()
                .any(|c| c.text == "中国人" && !c.has_remaining),
            "{raw}"
        );
    }
    assert!(!engine
        .query_configured("kn", false, false, false, false)
        .iter()
        .any(|c| c.text == "可能"));
    assert!(!engine
        .query_configured("k'nn", false, false, false, true)
        .iter()
        .any(|c| c.text == "可能" && !c.has_remaining));
    let exact = engine.query_configured("xian", false, false, true, true);
    assert_eq!(exact[0].kind, MatchKind::Exact);
    assert_eq!(exact[0].text, "先");
}
#[test]
fn indexed_initials_matches_reference_matcher_on_generated_dictionary() {
    // Enumerate every full/initial combination for a small independently authored lexicon.
    let mut e = engine();
    e.set_options(cowtype::engine::QueryOptions {
        initials: true,
        completion: true,
    });
    for a in ["zhong", "zh", "z"] {
        for b in ["guo", "g"] {
            for c in ["ren", "r"] {
                for delimiter in ["", "'"] {
                    let input = [a, b, c].join(delimiter);
                    assert!(
                        e.query(&input)
                            .iter()
                            .any(|c| c.text == "中国人" && !c.has_remaining),
                        "{input}"
                    );
                }
            }
        }
    }
}
fn session() -> Session {
    let mut config = SessionConfig::default();
    config.initials = true;
    Session::new(Some(Arc::new(engine())), config)
}
#[test]
fn initials_session_selection_and_backspace_keep_input_lossless() {
    let mut s = session();
    s.key(Event::press(Key::Character('k')));
    s.key(Event::press(Key::Character('n')));
    assert_eq!(s.frame().preedit, "kn");
    assert_eq!(
        s.key(Event::press(Key::Space)).commit.as_deref(),
        Some("可能")
    );
    for ch in "k'n".chars() {
        s.key(Event::press(Key::Character(ch)));
    }
    s.key(Event::press(Key::Backspace));
    assert_eq!(s.frame().preedit, "k'");
    assert_eq!(
        s.key(Event::press(Key::Enter)).commit.as_deref(),
        Some("k'")
    );
}
#[test]
fn expanded_punctuation_multichar_and_quote_state_are_per_field() {
    let mut s = session();
    for (key, expected) in [
        (',', "，"),
        ('.', "。"),
        ('?', "？"),
        ('!', "！"),
        (':', "："),
        (';', "；"),
        ('<', "《"),
        ('>', "》"),
        ('\\', "、"),
        ('#', "＃"),
        ('$', "￥"),
        ('%', "％"),
        ('^', "……"),
        ('_', "——"),
        ('(', "（"),
        (')', "）"),
        ('[', "【"),
        (']', "】"),
        ('{', "｛"),
        ('}', "｝"),
        ('~', "～"),
        ('`', "·"),
    ] {
        assert_eq!(
            s.key(Event::press(Key::Character(key))).commit.as_deref(),
            Some(expected),
            "{key}"
        );
    }
    assert_eq!(
        s.key(Event::press(Key::Character('"'))).commit.as_deref(),
        Some("“")
    );
    assert_eq!(
        session()
            .key(Event::press(Key::Character('"')))
            .commit
            .as_deref(),
        Some("“")
    );
    assert_eq!(
        s.key(Event::press(Key::Character('"'))).commit.as_deref(),
        Some("”")
    );
    assert_eq!(
        s.key(Event::press(Key::Character('\''))).commit.as_deref(),
        Some("‘")
    );
    assert_eq!(
        s.key(Event::press(Key::Character('\''))).commit.as_deref(),
        Some("’")
    );
    s.reset(ResetReason::FocusLost);
    assert_eq!(
        s.key(Event::press(Key::Character('"'))).commit.as_deref(),
        Some("“")
    );
}
#[test]
fn punctuation_commits_candidate_once_and_numeric_guard_is_narrow() {
    let mut s = session();
    for ch in "kn".chars() {
        s.key(Event::press(Key::Character(ch)));
    }
    assert_eq!(
        s.key(Event::press(Key::Character('^'))).commit.as_deref(),
        Some("可能……")
    );
    assert!(s.frame().commit.is_none());
    for ch in ['.', ','] {
        let mut event = Event::press(Key::Character(ch));
        event.before_cursor = Some('3');
        assert!(!s.key(event).handled);
    }
    let mut event = Event::press(Key::Character('!'));
    event.before_cursor = Some('3');
    assert_eq!(s.key(event).commit.as_deref(), Some("！"));
    for ch in "ke'".chars() {
        s.key(Event::press(Key::Character(ch)));
    }
    assert_eq!(s.frame().preedit, "ke'");
    s.reset(ResetReason::FocusLost);
    let mut config = SessionConfig::default();
    config.chinese_punctuation = false;
    s.configure(config);
    assert!(!s.key(Event::press(Key::Character('['))).handled);
    s.key(Event::press(Key::ToggleMode));
    assert!(!s.key(Event::press(Key::Character('"'))).handled);
}

#[test]
fn known_mixed_initials_are_not_displaced_by_typo_guesses() {
    let dictionary =
        Dictionary::from_tsv("zhong guo ren\t中国人\t80\nzhong er\t中二\t1000\n".as_bytes())
            .unwrap()
            .value;
    let e = Engine::new(dictionary);
    let result = e.query_configured("zhonggr", true, true, true, true);
    assert_eq!(result[0].text, "中国人");
    assert_eq!(result[0].kind, MatchKind::Initials);
}
