// SPDX-License-Identifier: GPL-3.0-or-later
use cowtype::{
    dictionary::Dictionary,
    engine::Engine,
    session::{Event, Frame, Key, Mode, ResetReason, Session, SessionConfig},
};
use std::sync::Arc;

fn shared_engine() -> Arc<Engine> {
    Arc::new(Engine::new(
        Dictionary::from_tsv(include_bytes!("fixtures/basic.tsv"))
            .unwrap()
            .value,
    ))
}
fn session() -> Session {
    Session::new(
        Some(shared_engine()),
        SessionConfig::with_page_size(3).unwrap(),
    )
}
fn key(session: &mut Session, key: Key) -> Frame {
    session.key(Event::press(key))
}
fn type_text(session: &mut Session, text: &str) {
    for ch in text.chars() {
        assert!(key(session, Key::Character(ch)).handled);
    }
}

#[test]
fn translation_preview_preserves_partial_selection_suffix_and_frame() {
    let mut s = session();
    type_text(&mut s, "SHAN'hai");
    s.choose(1);
    assert_eq!(s.selected_text(), "山");
    let before = s.frame();
    assert_eq!(s.preview(), "山海");
    assert_eq!(s.preview(), "山海");
    assert_eq!(s.frame().preedit, before.preedit);
    assert_eq!(s.remaining(), "hai");
    assert!(s.frame().commit.is_none());
    let mut raw = Session::new(None, SessionConfig::default());
    type_text(&mut raw, "Hello");
    assert_eq!(raw.preview(), "Hello");
}

#[test]
fn ps001_no_engine_preserves_case_and_commits_raw_once() {
    let mut session = Session::new(None, SessionConfig::default());
    type_text(&mut session, "AbC");
    assert_eq!(session.frame().preedit, "AbC");
    assert!(!session.frame().engine_ready);
    assert_eq!(key(&mut session, Key::Enter).commit.as_deref(), Some("AbC"));
    assert!(session.frame().preedit.is_empty());
    assert!(session.frame().commit.is_none());
    assert!(!key(&mut session, Key::Enter).handled);
}

#[test]
fn late_engine_attachment_retains_composition() {
    let mut session = Session::new(None, SessionConfig::default());
    type_text(&mut session, "shan");
    session.set_engine(Some(shared_engine()));
    assert_eq!(session.remaining(), "shan");
    assert_eq!(key(&mut session, Key::Space).commit.as_deref(), Some("山"));
}

#[test]
fn ps002_partial_selection_and_backspace_restore_original_keystrokes() {
    let mut session = session();
    type_text(&mut session, "SHAN'hai");
    assert!(session.choose(1).commit.is_none());
    assert_eq!(session.selected_text(), "山");
    assert_eq!(session.remaining(), "hai");
    assert_eq!(session.input(), "SHAN'hai");
    assert_eq!(session.frame().preedit_cursor_bytes, "山hai".len());
    key(&mut session, Key::Backspace);
    assert_eq!(session.remaining(), "SHAN'hai");
    assert!(session.selected_text().is_empty());
    key(&mut session, Key::Backspace);
    assert_eq!(session.remaining(), "SHAN'ha");
}

#[test]
fn ps003_space_enter_escape_have_distinct_semantics() {
    let mut session = session();
    type_text(&mut session, "shan");
    assert_eq!(key(&mut session, Key::Space).commit.as_deref(), Some("山"));
    type_text(&mut session, "shan");
    assert_eq!(
        key(&mut session, Key::Enter).commit.as_deref(),
        Some("shan")
    );
    type_text(&mut session, "shan");
    assert!(key(&mut session, Key::Escape).commit.is_none());
    assert!(session.frame().preedit.is_empty());
    type_text(&mut session, "unknown");
    assert_eq!(
        key(&mut session, Key::Space).commit.as_deref(),
        Some("unknown")
    );
}

#[test]
fn enter_and_deactivate_commit_selected_text_plus_raw_remainder() {
    for action in [false, true] {
        let mut session = session();
        type_text(&mut session, "SHAN'hai");
        session.choose(1);
        let frame = if action {
            session.reset(ResetReason::InputMethodSwitch)
        } else {
            key(&mut session, Key::Enter)
        };
        assert_eq!(frame.commit.as_deref(), Some("山hai"));
        assert!(session.frame().preedit.is_empty());
    }
}

#[test]
fn if003_visible_selection_and_global_selection_are_distinct() {
    let mut a = session();
    let mut b = session();
    type_text(&mut a, "shan");
    type_text(&mut b, "shan");
    key(&mut a, Key::PageDown);
    key(&mut b, Key::PageDown);
    assert_eq!(a.frame().page_index, 1);
    assert_eq!(a.select(1).commit.as_deref(), Some("删"));
    assert_eq!(b.choose(1).commit.as_deref(), Some("衫"));
}

#[test]
fn page_edges_invalid_numbers_and_config_change_remain_consistent() {
    let mut session = session();
    type_text(&mut session, "shan");
    let frame = key(&mut session, Key::PageUp);
    assert!(!frame.has_previous_page);
    assert!(frame.has_next_page);
    key(&mut session, Key::PageDown);
    key(&mut session, Key::PageDown);
    let frame = key(&mut session, Key::PageDown);
    assert_eq!(frame.page_index, 2);
    assert!(!frame.has_next_page);
    assert_eq!(frame.candidates.len(), 1);
    assert!(key(&mut session, Key::Character('2')).handled);
    assert!(key(&mut session, Key::Character('0')).handled);
    assert!(key(&mut session, Key::Character('9')).handled);
    assert_eq!(session.remaining(), "shan");
    assert!(!session.select(usize::MAX).handled);
    assert!(!session.choose(usize::MAX).handled);
    session.configure(SessionConfig::with_page_size(9).unwrap());
    assert_eq!(session.frame().candidate_cursor, Some(6));
    assert_eq!(session.frame().page_index, 0);
    assert_eq!(key(&mut session, Key::Space).commit.as_deref(), Some("跚"));
    assert!(SessionConfig::with_page_size(0).is_err());
    assert!(SessionConfig::with_page_size(10).is_err());
}

#[test]
fn raw_cursor_editing_never_navigates_application_during_composition() {
    let mut session = session();
    type_text(&mut session, "shanhai");
    key(&mut session, Key::Home);
    key(&mut session, Key::Delete);
    assert_eq!(session.remaining(), "hanhai");
    key(&mut session, Key::Character('s'));
    assert_eq!(session.remaining(), "shanhai");
    assert_eq!(session.frame().preedit_cursor_bytes, 1);
    key(&mut session, Key::End);
    key(&mut session, Key::Left);
    key(&mut session, Key::Backspace);
    assert_eq!(session.remaining(), "shanhi");
    key(&mut session, Key::Right);
    key(&mut session, Key::Delete);
    assert_eq!(session.remaining(), "shanhi");
}

#[test]
fn candidate_navigation_tracks_pages() {
    let mut session = session();
    type_text(&mut session, "shan");
    for _ in 0..4 {
        key(&mut session, Key::NextCandidate);
    }
    assert_eq!(session.frame().page_index, 1);
    assert_eq!(session.frame().candidate_cursor, Some(1));
    assert_eq!(key(&mut session, Key::Space).commit.as_deref(), Some("删"));
}

#[test]
fn ps006_punctuation_preserves_unknown_input_and_respects_trusted_digits() {
    let mut session = session();
    let mut event = Event::press(Key::Character('.'));
    event.before_cursor = Some('3');
    assert!(!session.key(event).handled);
    event.before_cursor = Some('山');
    assert_eq!(session.key(event).commit.as_deref(), Some("。"));
    type_text(&mut session, "unknown");
    assert_eq!(
        key(&mut session, Key::Character(',')).commit.as_deref(),
        Some("unknown，")
    );
    type_text(&mut session, "shan");
    assert_eq!(
        key(&mut session, Key::Character('!')).commit.as_deref(),
        Some("山！")
    );
    let mut config = SessionConfig::default();
    config.chinese_punctuation = false;
    session.configure(config);
    assert!(!key(&mut session, Key::Character(',')).handled);
    type_text(&mut session, "unknown");
    assert_eq!(
        key(&mut session, Key::Character(',')).commit.as_deref(),
        Some("unknown,")
    );
}

#[test]
fn mode_switch_commits_raw_and_english_passes_through() {
    let mut session = session();
    type_text(&mut session, "SHAN");
    let frame = key(&mut session, Key::ToggleMode);
    assert_eq!(frame.commit.as_deref(), Some("SHAN"));
    assert_eq!(frame.mode, Mode::English);
    assert!(!key(&mut session, Key::Character('a')).handled);
    assert!(session.remaining().is_empty());
    assert_eq!(key(&mut session, Key::ToggleMode).mode, Mode::Chinese);
}

#[test]
fn release_and_application_shortcuts_do_not_mutate_composition() {
    let mut session = session();
    type_text(&mut session, "shan");
    let mut release = Event::press(Key::Enter);
    release.pressed = false;
    assert!(!session.key(release).handled);
    let mut shortcut = Event::press(Key::Character('c'));
    shortcut.modifiers.control = true;
    assert!(!session.key(shortcut).handled);
    shortcut.modifiers.control = false;
    shortcut.modifiers.alt = true;
    assert!(!session.key(shortcut).handled);
    shortcut.modifiers.alt = false;
    shortcut.modifiers.super_key = true;
    assert!(!session.key(shortcut).handled);
    let mut shifted = Event::press(Key::Delete);
    shifted.modifiers.shift = true;
    assert!(!session.key(shifted).handled);
    assert_eq!(session.remaining(), "shan");
}

#[test]
fn successive_prefix_and_final_selection_commit_exactly_once() {
    let mut session = session();
    type_text(&mut session, "shan'hai");
    assert!(session.choose(1).commit.is_none());
    let event_frame = key(&mut session, Key::Space);
    assert_eq!(event_frame.commit.as_deref(), Some("山海"));
    assert!(session.input().is_empty());
    assert!(session.frame().commit.is_none());
    assert_eq!(event_frame.commit.as_deref(), Some("山海"));
}

#[test]
fn detaching_engine_preserves_partial_selection_and_pending_raw() {
    let mut session = session();
    type_text(&mut session, "shan'hai");
    session.choose(1);
    session.set_engine(None);
    assert_eq!(session.frame().preedit, "山hai");
    assert_eq!(
        key(&mut session, Key::Space).commit.as_deref(),
        Some("山hai")
    );
}

#[test]
fn ps011_shared_engine_does_not_share_composition_and_resets_do_not_commit() {
    let engine = shared_engine();
    let mut a = Session::new(Some(engine.clone()), SessionConfig::default());
    let mut b = Session::new(Some(engine), SessionConfig::default());
    type_text(&mut a, "shan");
    type_text(&mut b, "hai");
    assert!(a.reset(ResetReason::FocusLost).commit.is_none());
    assert_eq!(b.remaining(), "hai");
    assert_eq!(key(&mut b, Key::Space).commit.as_deref(), Some("海"));
    assert!(a.frame().preedit.is_empty());
    type_text(&mut a, "shan");
    assert!(a.reset(ResetReason::ApplicationReset).commit.is_none());
}

#[test]
fn idle_keys_and_post_commit_backspace_go_to_application() {
    let mut session = session();
    for event in [
        Key::Backspace,
        Key::Delete,
        Key::Left,
        Key::Right,
        Key::Space,
        Key::Enter,
        Key::Escape,
        Key::Other,
    ] {
        assert!(!key(&mut session, event).handled);
    }
    type_text(&mut session, "shan");
    key(&mut session, Key::Space);
    assert!(!key(&mut session, Key::Backspace).handled);
}

#[test]
fn synthetic_event_sequences_keep_all_frame_cursors_in_bounds() {
    let alphabet = [
        Key::Character('s'),
        Key::Character('h'),
        Key::Character('a'),
        Key::Character('n'),
        Key::Character('\''),
        Key::Left,
        Key::Right,
        Key::Backspace,
        Key::Delete,
        Key::Space,
        Key::Escape,
        Key::PageDown,
        Key::NextCandidate,
    ];
    let mut session = session();
    let mut seed = 91_u64; // New deterministic synthetic event generator, no personal inputs.
    for _ in 0..4000 {
        seed = seed.wrapping_mul(6364136223846793005).wrapping_add(1);
        let frame = key(
            &mut session,
            alphabet[(seed >> 32) as usize % alphabet.len()],
        );
        assert!(frame.preedit.is_char_boundary(frame.preedit_cursor_bytes));
        assert!(frame
            .candidate_cursor
            .is_none_or(|i| i < frame.candidates.len()));
        assert!(frame.candidates.len() <= 3);
        assert!(session.frame().commit.is_none());
    }
}
