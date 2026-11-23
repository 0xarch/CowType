// SPDX-License-Identifier: GPL-3.0-or-later
use cowtype::{
    engine::{wubi::WubiTable, Engine},
    session::{Event, Key, Session, SessionConfig},
};
use std::sync::Arc;
#[test]
fn eng_t08_exact_first_dedup_and_stable_u64_priorities() {
    let table = WubiTable::from_tsv("a\t甲\t0\naa\t乙\t18446744073709551615\nab\t丙\t18446744073709551614\na\t丁\t0\nac\t甲\t10\n".as_bytes()).unwrap().value;
    let result = table.query("a");
    assert_eq!(
        result.iter().map(|c| c.text.as_str()).collect::<Vec<_>>(),
        ["甲", "丁", "乙", "丙"]
    );
    assert_eq!(result[2].reading, "aa");
    assert_eq!(result[2].consumed_bytes, 1);
    assert!(!result[2].has_remaining);
    for input in ["", "z", "aaaaa", "a!", "中"] {
        assert!(table.query(input).is_empty());
    }
}
#[test]
fn eng_t09_bad_wubi_rows_do_not_destroy_valid_data() {
    let report = WubiTable::from_tsv(
        b"# comment\r\na\tword\t0\r\nz\tbad\t1\na\tbad\t-1\na\tbad\t18446744073709551616\n",
    )
    .unwrap();
    assert_eq!(report.accepted, 1);
    assert_eq!(report.diagnostics.len(), 3);
    assert!(WubiTable::from_tsv(b"a\tbad\t-1").is_err());
}
#[test]
fn wubi_session_uses_same_selection_and_reset_contract() {
    let engine = Engine::from_wubi(
        WubiTable::from_tsv(include_bytes!("fixtures/wubi.tsv"))
            .unwrap()
            .value,
    );
    let mut session = Session::new(Some(Arc::new(engine)), SessionConfig::default());
    assert!(session.key(Event::press(Key::Character('a'))).handled);
    assert_eq!(
        session.key(Event::press(Key::Space)).commit.as_deref(),
        Some("工")
    );
    assert!(session.frame().preedit.is_empty());
}

#[test]
fn double_pinyin_option_never_changes_late_wubi_attachment() {
    let mut config = SessionConfig::default();
    config.shuangpin = true;
    let mut session = Session::new(None, config);
    // An explicitly validated MS profile accepts its semicolon before loading.
    session.set_pending_shuangpin_semicolon(true);
    session.key(Event::press(Key::Character('n')));
    assert!(session
        .key(Event::press(Key::Character(';')))
        .commit
        .is_none());
    assert_eq!(session.input(), "n;");
    session.key(Event::press(Key::Escape));
    let table = WubiTable::from_tsv("a\t工\t100\n".as_bytes())
        .unwrap()
        .value;
    session.set_engine(Some(Arc::new(Engine::from_wubi(table))));
    session.key(Event::press(Key::Character('a')));
    assert_eq!(session.frame().candidates[0].text, "工");
    assert_eq!(
        session
            .key(Event::press(Key::Character(';')))
            .commit
            .as_deref(),
        Some("工；")
    );
}

#[test]
fn cold_wubi_does_not_claim_shuangpin_punctuation() {
    let mut config = SessionConfig::default();
    config.shuangpin = true;
    let mut session = Session::new(None, config);
    // Host supplies no double-pinyin capability for the Wubi resource.
    session.set_pending_shuangpin_semicolon(false);
    assert_eq!(
        session
            .key(Event::press(Key::Character(';')))
            .commit
            .as_deref(),
        Some("；")
    );
}
