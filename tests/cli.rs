// SPDX-License-Identifier: GPL-3.0-or-later
use std::{
    io::Write,
    process::{Command, Stdio},
};

#[test]
fn query_runs_offline_with_explicit_fixture() {
    let output = Command::new(env!("CARGO_BIN_EXE_cowtype"))
        .args([
            "query",
            concat!(env!("CARGO_MANIFEST_DIR"), "/tests/fixtures/basic.tsv"),
            "shanhai",
        ])
        .output()
        .unwrap();
    assert!(output.status.success());
    assert!(String::from_utf8(output.stdout)
        .unwrap()
        .starts_with("7\tshan hai\t山海\n"));
    assert!(output.stderr.is_empty());
}

#[test]
fn no_engine_replay_is_lossless_and_commit_is_not_replayed() {
    let mut child = Command::new(env!("CARGO_BIN_EXE_cowtype"))
        .args(["replay", "-"])
        .stdin(Stdio::piped())
        .stdout(Stdio::piped())
        .spawn()
        .unwrap();
    child
        .stdin
        .take()
        .unwrap()
        .write_all(b"type AbC\nenter\nenter\n")
        .unwrap();
    let output = child.wait_with_output().unwrap();
    assert!(output.status.success());
    let stdout = String::from_utf8(output.stdout).unwrap();
    assert_eq!(stdout.matches("commit=Some(\"AbC\")").count(), 1);
    assert!(stdout
        .lines()
        .last()
        .unwrap()
        .starts_with("handled=false commit=None preedit=\"\""));
}

#[test]
fn missing_explicit_dictionary_is_readable_failure() {
    let output = Command::new(env!("CARGO_BIN_EXE_cowtype"))
        .args([
            "query",
            "/cowtype-intentionally-missing-fixture/dictionary.tsv",
            "shan",
        ])
        .output()
        .unwrap();
    assert!(!output.status.success());
    assert!(String::from_utf8(output.stderr)
        .unwrap()
        .contains("cowtype:"));
}
