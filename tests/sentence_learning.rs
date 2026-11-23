// SPDX-License-Identifier: GPL-3.0-or-later
use cowtype::{
    dictionary::Dictionary,
    engine::{Candidate, CandidateProvider, Engine, MatchKind},
    learning::Learning,
    session::{Event, Key, ResetReason, Session, SessionConfig},
};
use std::sync::{Arc, Mutex};
fn engine() -> Engine {
    Engine::new(Dictionary::from_tsv("wo\t我\t100\nxi huan\t喜欢\t100\nxie zuo\t写作\t70\nni hao\t你好\t80\nni\t你\t90\nhao\t好\t30\nshan\t山\t100\nshan\t杉\t2\n".as_bytes()).unwrap().value)
}
#[test]
fn sentence_composes_new_full_candidate_and_retains_prefixes() {
    let e = engine();
    let raw = "woxihuanxiezuo";
    let r = e.query_features(raw, true, false, true);
    assert_eq!(r[0].text, "我喜欢写作");
    assert_eq!(r[0].consumed_bytes, raw.len());
    assert_eq!(r[0].reading, "wo xi huan xie zuo");
    assert!(r.iter().any(|c| c.text == "我" && c.has_remaining));
    let r = e.query_features("wo'xi'huan'xie'zuo", true, false, true);
    assert_eq!(r[0].text, "我喜欢写作");
    assert!(!e
        .query_features(raw, false, false, true)
        .iter()
        .any(|c| c.text == "我喜欢写作"));
}
#[test]
fn spelling_single_edit_operations_and_correct_word_protection() {
    let e = engine();
    for input in ["nihoa", "niihao", "niha", "nigao"] {
        let r = e.query_features(input, true, true, false);
        assert!(
            r.iter()
                .any(|c| c.text == "你好" && !c.has_remaining && c.kind == MatchKind::Correction),
            "{input}: {r:?}"
        );
        assert!(!e
            .query_features(input, false, false, false)
            .iter()
            .any(|c| c.kind == MatchKind::Correction));
    }
    assert_eq!(
        e.query_features("nihao", true, true, true)[0].kind,
        MatchKind::Exact
    );
    assert!(!e
        .query_features("n'ihao", true, true, true)
        .iter()
        .any(|c| c.text == "你好" && !c.has_remaining));
}
#[test]
fn spelling_inside_a_new_sentence_preserves_raw_consumption() {
    let e = engine();
    let raw = "woxihuanxiezou";
    let r = e.query_features(raw, true, true, false);
    assert!(
        r.iter()
            .any(|c| c.text == "我喜欢写作" && c.consumed_bytes == raw.len()),
        "{r:?}"
    );
}
#[test]
fn preferences_decay_undo_conflicts_and_no_ascii_history() {
    let mut l = Learning::default();
    assert!(l.confirm("abc", "abc", 1).is_none());
    let u = l.confirm("shan", "杉", 100).unwrap();
    assert_eq!(l.score("shan", "杉", 100), 1);
    assert_eq!(l.ranking_score("shan", "杉", 100), 0);
    assert_eq!(l.score("shan", "杉", 100 + 30 * 86400), 0);
    assert!(l.undo(&[u]));
    assert_eq!(l.score("shan", "杉", 100), 0);
    let u = l.confirm("shan", "杉", 100).unwrap();
    l.confirm("shan", "杉", 100);
    assert_eq!(l.ranking_score("shan", "杉", 100), 1);
    assert!(!l.undo(&[u]));
    assert!(l.forget("shan", "杉"));
    assert!(!l.forget("shan", "杉"));
}

#[test]
fn one_mistaken_confirmation_does_not_override_dictionary_order() {
    let learned = Arc::new(Mutex::new(Learning::default()));
    let mut config = SessionConfig::default();
    config.learning = true;
    let dictionary_engine = Arc::new(engine());
    learned
        .lock()
        .unwrap()
        .confirm("shan", "杉", cowtype::learning::now());
    let mut session = Session::new(Some(dictionary_engine), config);
    session.set_learning(Some(learned.clone()));
    for ch in "shan".chars() {
        session.key(Event::press(Key::Character(ch)));
    }
    assert_eq!(session.frame().candidates[0].text, "山");
    learned
        .lock()
        .unwrap()
        .confirm("shan", "杉", cowtype::learning::now());
    let mut session = Session::new(Some(Arc::new(engine())), config);
    session.set_learning(Some(learned));
    for ch in "shan".chars() {
        session.key(Event::press(Key::Character(ch)));
    }
    assert_eq!(session.frame().candidates[0].text, "杉");
}

#[derive(Debug)]
struct ReversedProvider;
impl CandidateProvider for ReversedProvider {
    fn query(&self, raw: &str, _: bool, _: bool, _: bool, _: bool) -> Vec<Candidate> {
        vec![
            Candidate {
                text: "那".into(),
                reading: "na".into(),
                consumed_bytes: raw.len(),
                has_remaining: false,
                kind: MatchKind::Correction,
                custom: false,
            },
            Candidate {
                text: "把".into(),
                reading: "ba".into(),
                consumed_bytes: raw.len(),
                has_remaining: false,
                kind: MatchKind::Exact,
                custom: false,
            },
        ]
    }
}

#[test]
fn learned_correction_cannot_jump_ahead_of_exact_provider_reading() {
    let learned = Arc::new(Mutex::new(Learning::default()));
    learned
        .lock()
        .unwrap()
        .confirm("na", "那", cowtype::learning::now());
    let mut engine = Engine::new(
        Dictionary::from_tsv("ba\t把\t1\n".as_bytes())
            .unwrap()
            .value,
    );
    engine.set_provider(Arc::new(ReversedProvider));
    let mut config = SessionConfig::default();
    config.learning = true;
    let mut session = Session::new(Some(Arc::new(engine)), config);
    session.set_learning(Some(learned));
    for ch in "ba".chars() {
        session.key(Event::press(Key::Character(ch)));
    }
    assert_eq!(session.frame().candidates[0].text, "把");
}
#[test]
fn only_confirmed_sessions_learn_focus_drop_does_not_and_backspace_undoes() {
    let learned = Arc::new(Mutex::new(Learning::default()));
    let mut config = SessionConfig::default();
    config.learning = true;
    let mut s = Session::new(Some(Arc::new(engine())), config);
    s.set_learning(Some(learned.clone()));
    for ch in "shan".chars() {
        s.key(Event::press(Key::Character(ch)));
    }
    s.select(1);
    for ch in "shan".chars() {
        s.key(Event::press(Key::Character(ch)));
    }
    assert_eq!(s.frame().candidates[0].text, "山");
    s.reset(ResetReason::FocusLost);
    assert_eq!(
        learned
            .lock()
            .unwrap()
            .score("shan", "山", cowtype::learning::now()),
        0
    );
    let mut s = Session::new(Some(Arc::new(engine())), config);
    s.set_learning(Some(learned.clone()));
    for ch in "shan".chars() {
        s.key(Event::press(Key::Character(ch)));
    }
    s.select(0);
    assert!(!s.key(Event::press(Key::Backspace)).handled);
    assert_eq!(
        learned
            .lock()
            .unwrap()
            .score("shan", "杉", cowtype::learning::now()),
        1
    );
}
#[test]
fn settings_validated_and_unknown_file_never_imported() {
    assert!(cowtype::config::Settings::parse("Voice=True").is_err());
    assert!(cowtype::config::Settings::parse("[CowType]\nVersion=1\nPageSize=10").is_err());
    let s = cowtype::config::Settings::parse(
        "[CowType]\nVersion=1\nPageSize=7\nLearning=False\nSentence=False",
    )
    .unwrap();
    assert_eq!(s.page_size, 7);
    assert!(!s.learning);
    assert!(!s.sentence);
}

#[test]
fn learning_atomic_roundtrip_reset_and_corruption_are_isolated() {
    use cowtype::learning::{save_atomic, service::Service};
    use std::{
        fs,
        os::unix::fs::{symlink, PermissionsExt},
    };
    let directory =
        std::env::temp_dir().join(format!("cowtype-learning-test-{}", std::process::id()));
    let _ = fs::remove_dir_all(&directory);
    fs::create_dir_all(&directory).unwrap();
    let path = directory.join("learning-v1.tsv");
    {
        let service = Service::new(path.clone());
        service.state.lock().unwrap().confirm("ni hao", "你好", 100);
    }
    assert!(Learning::load(&path).score("ni hao", "你好", 100) > 0);
    assert_eq!(
        fs::metadata(&path).unwrap().permissions().mode() & 0o777,
        0o600
    );
    fs::write(directory.join("learning-reset-v1"), "explicit-reset-1").unwrap();
    {
        let _service = Service::new(path.clone());
    }
    assert_eq!(Learning::load(&path).score("ni hao", "你好", 100), 0);
    let marker = directory.join("learning-reset-v1");
    fs::File::open(&marker)
        .unwrap()
        .set_modified(std::time::SystemTime::UNIX_EPOCH)
        .unwrap();
    fs::write(&path, "unknown-format\nprivate contents\n").unwrap();
    {
        let service = Service::new(path.clone());
        service.state.lock().unwrap().confirm("ni hao", "你好", 100);
    }
    assert_eq!(
        fs::read_to_string(&path).unwrap(),
        "unknown-format\nprivate contents\n"
    );
    let link = directory.join("link.tsv");
    symlink(&path, &link).unwrap();
    assert!(save_atomic(&link, "replacement").is_err());
    assert_eq!(
        fs::read_to_string(&path).unwrap(),
        "unknown-format\nprivate contents\n"
    );
    fs::remove_dir_all(directory).unwrap();
}
