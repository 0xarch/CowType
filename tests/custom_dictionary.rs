// SPDX-License-Identifier: GPL-3.0-or-later
use cowtype::{
    custom::{service::Service, Dictionary},
    dictionary::Dictionary as Pinyin,
    engine::Engine,
    learning::Learning,
    session::{Event, Key, Session, SessionConfig},
};
use std::{
    fs,
    sync::{Arc, Mutex, RwLock},
    time::{Duration, Instant},
};
fn shared(text: &str) -> Arc<RwLock<Dictionary>> {
    Arc::new(RwLock::new(Dictionary::parse(text).unwrap()))
}
fn type_text(session: &mut Session, text: &str) {
    for c in text.chars() {
        session.key(Event::press(Key::Character(c)));
    }
}
#[test]
fn exact_index_weight_order_multiline_literal_and_validation() {
    let d = Dictionary::parse("# cowtype-custom-v1\nabc\t第一\\n行\\t\\\\路径\t100\nabc\t优先\t1000\nabc\t同权重\t1000\nuab\t字母触发\t1\n").unwrap();
    let q = d.query("ABC");
    assert_eq!(
        q.iter().map(|c| c.text.as_str()).collect::<Vec<_>>(),
        ["优先", "同权重", "第一\n行\t\\路径"]
    );
    assert!(q
        .iter()
        .all(|c| c.custom && c.consumed_bytes == 3 && !c.has_remaining));
    assert!(d.query("ab").is_empty());
    assert!(d.query("a'bc").is_empty());
    assert!(d.has_prefix("ua"));
    assert!(!d.has_prefix("uc"));
    for row in [
        "a1\t字\t1",
        "abc\t字\t0",
        "abc\t字\t1000001",
        "abc\t字\\q\t1",
        "abc\t字\t1\nabc\t字\t2",
    ] {
        assert!(Dictionary::parse(&format!("# cowtype-custom-v1\n{row}\n")).is_err());
    }
}
#[test]
fn real_session_custom_precedes_words_emoji_and_learning_and_commits_exact_content() {
    let engine = Engine::new(
        Pinyin::from_tsv("shan\t山\t100\n".as_bytes())
            .unwrap()
            .value,
    );
    let learning = Arc::new(Mutex::new(Learning::default()));
    learning
        .lock()
        .unwrap()
        .confirm("shan", "山", cowtype::learning::now());
    let mut config = SessionConfig::default();
    config.emoji = true;
    let mut session = Session::new(Some(Arc::new(engine)), config);
    session.set_learning(Some(learning.clone()));
    session.set_custom(shared(
        "# cowtype-custom-v1\nshan\t自定义\t1\nshan\t👍\t100\nshan\t另一项\t10\n",
    ));
    type_text(&mut session, "shan");
    assert_eq!(
        session
            .frame()
            .candidates
            .iter()
            .take(3)
            .map(|c| c.text.as_str())
            .collect::<Vec<_>>(),
        ["👍", "另一项", "自定义"]
    );
    // An explicitly saved Emoji is literal, not a skin-tone submenu.
    assert_eq!(session.select(0).commit.as_deref(), Some("👍"));
    assert_eq!(
        learning
            .lock()
            .unwrap()
            .score("shan", "👍", cowtype::learning::now()),
        0
    );
    type_text(&mut session, "shan");
    assert_eq!(session.select(2).commit.as_deref(), Some("自定义"));
}
#[test]
fn background_updates_existing_sessions_and_preserves_good_data_on_corruption() {
    let root = std::env::temp_dir().join(format!("cowtype-custom-service-{}", std::process::id()));
    fs::create_dir_all(&root).unwrap();
    let path = root.join("custom-v1.tsv");
    let service = Service::new(path.clone());
    let mut session = Session::new(None, SessionConfig::default());
    session.set_custom(service.state.clone());
    cowtype::learning::save_atomic(&path, "# cowtype-custom-v1\nabc\t测试\t100\n").unwrap();
    let deadline = Instant::now() + Duration::from_secs(3);
    while service.state.read().unwrap().query("abc").is_empty() {
        assert!(Instant::now() < deadline);
        std::thread::sleep(Duration::from_millis(20));
    }
    type_text(&mut session, "abc");
    assert_eq!(session.select(0).commit.as_deref(), Some("测试"));
    cowtype::learning::save_atomic(&path, "broken\n").unwrap();
    let deadline = Instant::now() + Duration::from_secs(3);
    while service.error.lock().unwrap().is_empty() {
        assert!(Instant::now() < deadline);
        std::thread::sleep(Duration::from_millis(20));
    }
    assert_eq!(service.state.read().unwrap().query("abc")[0].text, "测试");
    fs::remove_file(&path).unwrap();
    let deadline = Instant::now() + Duration::from_secs(3);
    while !service.state.read().unwrap().query("abc").is_empty() {
        assert!(Instant::now() < deadline);
        std::thread::sleep(Duration::from_millis(20));
    }
    drop(service);
    fs::remove_dir_all(root).unwrap();
}

/// Opt-in synthetic measurement; prints timings/counts only, never private data.
#[test]
#[ignore]
fn indexed_personal_dictionary_benchmark() {
    let mut data = String::from("# cowtype-custom-v1\n");
    for i in 0..10000u32 {
        let key: String = (0..6)
            .map(|n| (b'a' + ((i / 26u32.pow(n)) % 26) as u8) as char)
            .collect();
        data.push_str(&format!("{key}\t合成词条{i}\t100\n"));
    }
    let start = Instant::now();
    let dictionary = Dictionary::parse(&data).unwrap();
    let load = start.elapsed();
    let start = Instant::now();
    for _ in 0..100000 {
        std::hint::black_box(dictionary.query(std::hint::black_box("aaaaaa")));
    }
    eprintln!(
        "synthetic entries=10000 bytes={} parse_index_ms={:.3} query_mean_us={:.3}",
        data.len(),
        load.as_secs_f64() * 1000.0,
        start.elapsed().as_secs_f64() * 1_000_000.0 / 100000.0
    );
}
