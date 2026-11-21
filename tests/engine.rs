// SPDX-License-Identifier: GPL-3.0-or-later
use cowtype::{
    dictionary::{CustomPhrases, Dictionary},
    engine::{Engine, MatchKind, QueryOptions},
};

fn engine() -> Engine {
    Engine::new(
        Dictionary::from_tsv(include_bytes!("fixtures/basic.tsv"))
            .unwrap()
            .value,
    )
}
fn texts(engine: &Engine, input: &str) -> Vec<String> {
    engine.query(input).into_iter().map(|c| c.text).collect()
}

#[test]
fn eng_t01_empty_case_and_original_byte_ranges() {
    let engine = engine();
    assert!(engine.query("").is_empty());
    assert!(engine.query(" \t\r\n").is_empty());
    assert_eq!(texts(&engine, "SHAN"), texts(&engine, "shan"));
    let candidates = engine.query(" \tSHANHAI \r\n");
    assert_eq!(candidates[0].text, "山海");
    assert_eq!(candidates[0].consumed_bytes, " \tSHANHAI \r\n".len());
    let prefix = candidates.iter().find(|c| c.text == "山").unwrap();
    assert_eq!(&" \tSHANHAI \r\n"[prefix.consumed_bytes..], "HAI \r\n");
}

#[test]
fn eng_t02_forced_boundaries_cannot_be_crossed() {
    let engine = engine();
    assert!(texts(&engine, "xian").contains(&"先".into()));
    let separated = texts(&engine, "xi'an");
    assert!(separated.contains(&"西安".into()));
    assert!(!separated.contains(&"先".into()));
    assert!(engine.query("xi''an").is_empty());
    assert!(engine.query("xi'").is_empty());
    assert!(engine.query("'xian").is_empty());
}

#[test]
fn eng_t03_completion_and_initials_are_independent() {
    let mut engine = engine();
    engine.set_options(QueryOptions {
        completion: false,
        initials: false,
    });
    assert!(!texts(&engine, "shanh").contains(&"山海".into()));
    assert!(!texts(&engine, "sh").contains(&"山海".into()));
    engine.set_options(QueryOptions {
        completion: true,
        initials: false,
    });
    let result = engine.query("shanh");
    assert_eq!(result[0].text, "山海");
    assert_eq!(result[0].kind, MatchKind::Completion);
    assert!(!texts(&engine, "sh").contains(&"山海".into()));
    engine.set_options(QueryOptions {
        completion: false,
        initials: true,
    });
    assert!(texts(&engine, "sh").contains(&"山海".into()));
    assert!(texts(&engine, "shanh").contains(&"山海".into()));
    assert!(texts(&engine, "s'h").contains(&"山海".into()));
}

#[test]
fn eng_t06_prefix_consumes_actual_keys_including_explicit_boundary() {
    let engine = engine();
    for input in ["shanHAI", "SHAN'HAI", "shan hai"] {
        let candidates = engine.query(input);
        let prefix = candidates.iter().find(|c| c.text == "山").unwrap();
        assert_eq!(input[prefix.consumed_bytes..].to_lowercase(), "hai");
        assert!(prefix.has_remaining);
        assert!(!candidates[0].has_remaining);
    }
}

#[test]
fn eng_io002_prefix_does_not_cut_a_declared_syllable_without_valid_alternative() {
    let engine = Engine::new(
        Dictionary::from_tsv("shan\t山\t1\nshang hai\t上海\t2\nhai\t海\t1\n".as_bytes())
            .unwrap()
            .value,
    );
    let result = texts(&engine, "shanghai");
    assert!(result.contains(&"上海".into()));
    assert!(!result.contains(&"山".into()));
    // A forced boundary explicitly overrides the longer syllable interpretation.
    let forced = engine.query("shan'ghai");
    assert_eq!(forced[0].text, "山");
    assert_eq!(forced[0].consumed_bytes, 5);
    // A real alternative segmentation is retained, rather than greedy longest match.
    assert!(texts(&self::engine(), "xian").contains(&"西".into()));
}

#[test]
fn eng_ca001_frequency_and_file_order_are_stable_without_learning() {
    let engine = engine();
    assert_eq!(
        texts(&engine, "shan"),
        ["山", "衫", "杉", "珊", "删", "栅", "跚"]
    );
    assert_eq!(texts(&engine, "lin"), ["林", "临"]);
    for _ in 0..20 {
        assert_eq!(texts(&engine, "lin"), ["林", "临"]);
    }
}

#[test]
fn loader_isolates_invalid_utf8_numeric_and_structural_rows() {
    let mut data = b"# comment\r\nshan\t\xe5\xb1\xb1\t3\r\nwrong\nshan\tbad\tNaN\nshan\tbad\tinf\nshan\tbad\t-1\nshan\tbad\t0\nSHAN\tbad\t1\n".to_vec();
    data.extend_from_slice(b"\xff\nlin\t\xe6\x9e\x97\t2\n");
    let report = Dictionary::from_tsv(&data).unwrap();
    assert_eq!(report.accepted, 2);
    assert_eq!(
        report
            .diagnostics
            .iter()
            .map(|d| d.line)
            .collect::<Vec<_>>(),
        [3, 4, 5, 6, 7, 8, 9]
    );
    assert_eq!(report.value.len(), 2);
}

#[test]
fn loader_reports_totally_unusable_dictionary() {
    let error = Dictionary::from_tsv(b"# empty\na\t\t1\na\tx\t1e999\n").unwrap_err();
    assert_eq!(error.diagnostics.len(), 2);
    assert!(Dictionary::from_tsv(b"").is_err());
}

#[test]
fn duplicate_rows_sum_original_frequency() {
    let report = Dictionary::from_tsv("lin\t林\t2\nlin\t临\t3\nlin\t林\t2\n".as_bytes()).unwrap();
    assert_eq!(report.duplicates, 1);
    assert_eq!(report.accepted, 3);
    assert_eq!(report.value.len(), 2);
    assert_eq!(texts(&Engine::new(report.value), "lin"), ["林", "临"]);
}

#[test]
fn eng_t12_custom_exact_expansion_escapes_and_replacement_are_separate() {
    let mut engine = engine();
    let custom =
        CustomPhrases::from_tsv(b"brb\tfirst\\nsecond\\tend\\\\\\q\nshan\tcustom\t1\nbad\t\t0\n");
    assert_eq!(custom.accepted, 2);
    assert_eq!(custom.diagnostics.len(), 1);
    engine.replace_custom(custom.value);
    assert!(engine.query("br").is_empty());
    assert_eq!(engine.query("brb")[0].text, "first\nsecond\tend\\\\q");
    assert!(engine.query("brb")[0].custom);
    assert_eq!(engine.query("shan")[0].text, "custom");
    engine.replace_custom(CustomPhrases::from_tsv(b"").value);
    assert!(engine.query("brb").is_empty());
    assert_eq!(engine.query("shan")[0].text, "山");
}

#[test]
fn unknown_or_non_ascii_input_has_no_fabricated_candidates() {
    let engine = engine();
    for raw in [
        "unknown",
        "山",
        "😀",
        "\0",
        "sha\tn",
        "shan!",
        "ü",
        "shan\nhai",
    ] {
        assert!(engine.query(raw).is_empty(), "{raw:?}");
    }
    assert_eq!(engine.query("lv")[0].text, "吕");
}

#[test]
fn candidate_snapshots_own_strings() {
    let result = engine().query("shan");
    assert_eq!(result[0].text, "山");
}

#[test]
fn synthetic_query_space_always_returns_valid_nonempty_raw_ranges() {
    let mut engine = engine();
    engine.set_options(QueryOptions {
        completion: true,
        initials: true,
    });
    // Independently generated small exhaustive alphabet; not a ranking corpus.
    let mut inputs = vec![String::new()];
    for _ in 0..4 {
        inputs = inputs
            .into_iter()
            .flat_map(|prefix| {
                ['s', 'h', 'a', 'n', '\'']
                    .into_iter()
                    .map(move |ch| format!("{prefix}{ch}"))
            })
            .collect();
        for raw in &inputs {
            let first = engine.query(raw);
            assert_eq!(first, engine.query(raw));
            for c in first {
                assert!(c.consumed_bytes > 0 && c.consumed_bytes <= raw.len());
                assert!(raw.is_char_boundary(c.consumed_bytes));
                assert_eq!(c.has_remaining, c.consumed_bytes < raw.len());
            }
        }
    }
}

#[test]
fn duplicate_frequency_overflow_is_diagnosed_without_poisoning_entry() {
    let report = Dictionary::from_tsv(b"lin\tx\t1e308\nlin\tx\t1e308\n").unwrap();
    assert_eq!(report.accepted, 1);
    assert_eq!(report.diagnostics[0].reason, "combined frequency overflow");
}
