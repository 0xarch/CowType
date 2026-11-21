// SPDX-License-Identifier: GPL-3.0-or-later
//! New ABI 2, all callers rebuilt. Opaque handles are owned and single-thread-affine.
//! C pointers must refer to valid live handles / readable NUL-terminated UTF-8.
//! Borrowed strings last until the next mutation of that same handle. Getters do
//! not mutate or repeat commits; the host consumes each mutating call's frame once.
#![allow(clippy::missing_safety_doc)] // The uniform pointer contract is documented above and in the C header.
use crate::{
    dictionary::Dictionary,
    engine::{wubi::WubiTable, Engine},
    session::{Event, Frame, Key, Modifiers, ResetReason, Session, SessionConfig},
};
use std::{
    cell::RefCell,
    ffi::{c_char, CStr, CString},
    panic::{catch_unwind, AssertUnwindSafe},
    ptr,
    sync::Arc,
};

thread_local! { static ERROR: RefCell<CString> = RefCell::new(CString::default()); }
fn error(text: &str) {
    ERROR.with(|e| *e.borrow_mut() = cstring(text));
}
fn cstring(text: &str) -> CString {
    CString::new(text.replace('\0', "�")).expect("NUL replaced")
}
fn protect<T>(fallback: T, run: impl FnOnce() -> T) -> T {
    catch_unwind(AssertUnwindSafe(run)).unwrap_or_else(|_| {
        error("internal failure; input method state must be reset");
        fallback
    })
}
unsafe fn string<'a>(value: *const c_char) -> Result<&'a str, &'static str> {
    if value.is_null() {
        return Err("missing string argument");
    }
    unsafe { CStr::from_ptr(value) }
        .to_str()
        .map_err(|_| "invalid UTF-8 argument")
}
pub struct EngineHandle {
    engine: Arc<Engine>,
    warning: CString,
}
pub struct SessionHandle {
    session: Session,
    frame: Frame,
    preedit: CString,
    commit: CString,
    words: Vec<CString>,
    readings: Vec<CString>,
}
impl SessionHandle {
    fn update(&mut self, frame: Frame) {
        self.preedit = cstring(&frame.preedit);
        self.commit = cstring(frame.commit.as_deref().unwrap_or(""));
        self.words = frame.candidates.iter().map(|c| cstring(&c.text)).collect();
        self.readings = frame
            .candidates
            .iter()
            .map(|c| cstring(&c.reading))
            .collect();
        self.frame = frame;
    }
}

#[no_mangle]
pub extern "C" fn cowtype2_abi_version() -> u32 {
    2000
}
#[no_mangle]
pub extern "C" fn cowtype2_last_error() -> *const c_char {
    ERROR.with(|e| e.borrow().as_ptr())
}
#[no_mangle]
pub unsafe extern "C" fn cowtype2_engine_new(
    path: *const c_char,
    optional: *const c_char,
    scheme: u32,
) -> *mut EngineHandle {
    protect(ptr::null_mut(), || {
        let run = || -> Result<EngineHandle, &'static str> {
            let path = unsafe { string(path)? };
            let mut data = std::fs::read(path).map_err(|_| "system dictionary cannot be read")?;
            if scheme == 1 {
                let table =
                    WubiTable::from_tsv(&data).map_err(|_| "Wubi dictionary has no usable rows")?;
                if !table.diagnostics.is_empty() {
                    return Err("Wubi dictionary contains malformed rows");
                }
                return Ok(EngineHandle {
                    engine: Arc::new(Engine::from_wubi(table.value)),
                    warning: CString::default(),
                });
            }
            if scheme != 0 && scheme != 2 {
                return Err("unsupported input scheme");
            }
            let mut warning = CString::default();
            if !optional.is_null() {
                let optional = unsafe { string(optional)? };
                if !optional.is_empty() {
                    match std::fs::read(optional) {
                        Ok(extra)
                            if Dictionary::from_tsv(&extra)
                                .is_ok_and(|r| r.diagnostics.is_empty()) =>
                        {
                            data.push(b'\n');
                            data.extend(extra);
                        }
                        Ok(_) => warning = cstring(crate::i18n::tr("message.cd99eb09686c")),
                        Err(e) if e.kind() == std::io::ErrorKind::NotFound => (),
                        Err(_) => warning = cstring(crate::i18n::tr("message.c9ecf7403c6a")),
                    }
                }
            }
            let dictionary =
                Dictionary::from_tsv(&data).map_err(|_| "pinyin dictionary has no usable rows")?;
            if !dictionary.diagnostics.is_empty() {
                return Err("pinyin dictionary contains malformed rows");
            }
            Ok(EngineHandle {
                // External pinyin backends own their index. Validate the public TSV
                // but do not build and retain a second unused full Rust index.
                engine: Arc::new(Engine::new(if scheme == 2 {
                    Dictionary {
                        entries: Vec::new(),
                    }
                } else {
                    dictionary.value
                })),
                warning,
            })
        };
        match run() {
            Ok(handle) => Box::into_raw(Box::new(handle)),
            Err(reason) => {
                error(reason);
                ptr::null_mut()
            }
        }
    })
}
// The native host validates/compiles its own dictionary. Avoid reading millions
// of TSV rows into an unused Rust index before attaching that decoder.
#[no_mangle]
pub extern "C" fn cowtype2_engine_external_new() -> *mut EngineHandle {
    protect(ptr::null_mut(), || {
        Box::into_raw(Box::new(EngineHandle {
            engine: Arc::new(Engine::new(Dictionary {
                entries: Vec::new(),
            })),
            warning: CString::default(),
        }))
    })
}
#[no_mangle]
pub unsafe extern "C" fn cowtype2_engine_free(handle: *mut EngineHandle) {
    if !handle.is_null() {
        drop(unsafe { Box::from_raw(handle) });
    }
}
#[no_mangle]
pub unsafe extern "C" fn cowtype2_engine_warning(handle: *const EngineHandle) -> *const c_char {
    unsafe { handle.as_ref() }
        .map(|h| h.warning.as_ptr())
        .unwrap_or(ptr::null())
}
#[no_mangle]
pub unsafe extern "C" fn cowtype2_session_new(handle: *const EngineHandle) -> *mut SessionHandle {
    protect(ptr::null_mut(), || {
        let engine = unsafe { handle.as_ref() }.map(|h| h.engine.clone());
        let session = Session::new(engine, SessionConfig::default());
        let frame = session.frame();
        Box::into_raw(Box::new(SessionHandle {
            session,
            frame,
            preedit: CString::default(),
            commit: CString::default(),
            words: Vec::new(),
            readings: Vec::new(),
        }))
    })
}
#[no_mangle]
pub unsafe extern "C" fn cowtype2_session_free(handle: *mut SessionHandle) {
    if !handle.is_null() {
        drop(unsafe { Box::from_raw(handle) });
    }
}
#[no_mangle]
pub unsafe extern "C" fn cowtype2_session_set_engine(
    handle: *mut SessionHandle,
    engine: *const EngineHandle,
) {
    protect((), || {
        if let Some(h) = unsafe { handle.as_mut() } {
            h.session
                .set_engine(unsafe { engine.as_ref() }.map(|e| e.engine.clone()));
            h.update(h.session.frame());
        }
    });
}
fn key(code: u32) -> Key {
    match code {
        0x110001 => Key::Space,
        0x110002 => Key::Enter,
        0x110003 => Key::Escape,
        0x110004 => Key::Backspace,
        0x110005 => Key::Delete,
        0x110006 => Key::Left,
        0x110007 => Key::Right,
        0x110008 => Key::Home,
        0x110009 => Key::End,
        0x11000a => Key::PreviousCandidate,
        0x11000b => Key::NextCandidate,
        0x11000c => Key::PageUp,
        0x11000d => Key::PageDown,
        0x11000e => Key::ToggleMode,
        _ => char::from_u32(code)
            .map(Key::Character)
            .unwrap_or(Key::Other),
    }
}
#[no_mangle]
pub unsafe extern "C" fn cowtype2_session_key(
    handle: *mut SessionHandle,
    code: u32,
    modifiers: u32,
    release: bool,
    previous: u32,
) -> bool {
    protect(false, || {
        let Some(h) = (unsafe { handle.as_mut() }) else {
            return false;
        };
        let frame = h.session.key(Event {
            key: key(code),
            pressed: !release,
            before_cursor: char::from_u32(previous),
            modifiers: Modifiers {
                shift: modifiers & 1 != 0,
                control: modifiers & 4 != 0,
                alt: modifiers & 8 != 0,
                super_key: modifiers & 64 != 0,
            },
        });
        let handled = frame.handled;
        h.update(frame);
        handled
    })
}
#[no_mangle]
pub unsafe extern "C" fn cowtype2_session_select(handle: *mut SessionHandle, index: usize) -> bool {
    protect(false, || {
        let Some(h) = (unsafe { handle.as_mut() }) else {
            return false;
        };
        let frame = h.session.select(index);
        let handled = frame.handled;
        h.update(frame);
        handled
    })
}
#[no_mangle]
pub unsafe extern "C" fn cowtype2_session_reset(handle: *mut SessionHandle, commit: bool) {
    protect((), || {
        if let Some(h) = unsafe { handle.as_mut() } {
            let frame = h.session.reset(if commit {
                ResetReason::InputMethodSwitch
            } else {
                ResetReason::FocusLost
            });
            h.update(frame);
        }
    });
}
#[no_mangle]
pub unsafe extern "C" fn cowtype2_session_preedit(h: *const SessionHandle) -> *const c_char {
    unsafe { h.as_ref() }
        .map(|h| h.preedit.as_ptr())
        .unwrap_or(ptr::null())
}
#[no_mangle]
pub unsafe extern "C" fn cowtype2_session_commit(h: *const SessionHandle) -> *const c_char {
    unsafe { h.as_ref() }
        .map(|h| h.commit.as_ptr())
        .unwrap_or(ptr::null())
}
#[no_mangle]
pub unsafe extern "C" fn cowtype2_session_cursor(h: *const SessionHandle) -> usize {
    unsafe { h.as_ref() }
        .map(|h| h.frame.preedit_cursor_bytes)
        .unwrap_or(0)
}
#[no_mangle]
pub unsafe extern "C" fn cowtype2_session_count(h: *const SessionHandle) -> usize {
    unsafe { h.as_ref() }.map(|h| h.words.len()).unwrap_or(0)
}
#[no_mangle]
pub unsafe extern "C" fn cowtype2_session_word(
    h: *const SessionHandle,
    index: usize,
) -> *const c_char {
    unsafe { h.as_ref() }
        .and_then(|h| h.words.get(index))
        .map(|s| s.as_ptr())
        .unwrap_or(ptr::null())
}
#[no_mangle]
pub unsafe extern "C" fn cowtype2_session_reading(
    h: *const SessionHandle,
    index: usize,
) -> *const c_char {
    unsafe { h.as_ref() }
        .and_then(|h| h.readings.get(index))
        .map(|s| s.as_ptr())
        .unwrap_or(ptr::null())
}
#[no_mangle]
pub unsafe extern "C" fn cowtype2_session_candidate_cursor(h: *const SessionHandle) -> i32 {
    unsafe { h.as_ref() }
        .and_then(|h| h.frame.candidate_cursor)
        .map(|n| n as i32)
        .unwrap_or(-1)
}
#[no_mangle]
pub unsafe extern "C" fn cowtype2_session_has_page(h: *const SessionHandle, next: bool) -> bool {
    unsafe { h.as_ref() }
        .map(|h| {
            if next {
                h.frame.has_next_page
            } else {
                h.frame.has_previous_page
            }
        })
        .unwrap_or(false)
}
#[no_mangle]
pub unsafe extern "C" fn cowtype2_session_mode(h: *const SessionHandle) -> u32 {
    unsafe { h.as_ref() }
        .map(|h| {
            if h.frame.mode == crate::session::Mode::Chinese {
                0
            } else {
                1
            }
        })
        .unwrap_or(0)
}

// Runtime is a new ABI-2 extension, independent from dictionary handle lifetime.
pub struct RuntimeHandle {
    settings: crate::config::Settings,
    service: crate::learning::service::Service,
    service_warning: CString,
    warning: CString,
}
#[no_mangle]
pub extern "C" fn cowtype2_runtime_new() -> *mut RuntimeHandle {
    protect(ptr::null_mut(), || {
        let (settings, warning) = match crate::config::Settings::load(&crate::config::path()) {
            Ok(s) => (s, String::new()),
            Err(e) => (crate::config::Settings::default(), e),
        };
        Box::into_raw(Box::new(RuntimeHandle {
            settings,
            service: crate::learning::service::Service::new(crate::learning::path()),
            warning: cstring(&warning),
            service_warning: CString::default(),
        }))
    })
}
#[no_mangle]
pub unsafe extern "C" fn cowtype2_runtime_free(h: *mut RuntimeHandle) {
    if !h.is_null() {
        drop(unsafe { Box::from_raw(h) });
    }
}
#[no_mangle]
pub unsafe extern "C" fn cowtype2_runtime_reload(h: *mut RuntimeHandle) -> bool {
    protect(false, || {
        let Some(h) = (unsafe { h.as_mut() }) else {
            return false;
        };
        match crate::config::Settings::load(&crate::config::path()) {
            Ok(settings) => {
                h.settings = settings;
                h.warning = CString::default();
                true
            }
            Err(e) => {
                h.warning = cstring(&e);
                false
            }
        }
    })
}
#[no_mangle]
pub unsafe extern "C" fn cowtype2_runtime_option(h: *const RuntimeHandle, key: u32) -> u32 {
    let Some(h) = (unsafe { h.as_ref() }) else {
        return 0;
    };
    let s = &h.settings;
    match key {
        0 => s.page_size as u32,
        1 => s.horizontal as u32,
        2 => s.shift_toggle as u32,
        3 => s.voice as u32,
        4 => s.voice_resident as u32,
        5 => s.voice_hotkey,
        6 => s.voice_modifier_hold as u32,
        7 => s.show_pinyin as u32,
        8 => s.voice_always_resident as u32,
        9 => s.page_brackets as u32,
        10 => s.page_minus_equal as u32,
        11 => (s.shift_toggle && s.remember_mode) as u32,
        12 => s.statistical_model as u32,
        _ => 0,
    }
}
#[no_mangle]
pub unsafe extern "C" fn cowtype2_runtime_warning(h: *mut RuntimeHandle) -> *const c_char {
    let Some(h) = (unsafe { h.as_mut() }) else {
        return ptr::null();
    };
    if h.warning.as_bytes().is_empty() {
        if let Ok(error) = h.service.error.lock() {
            h.service_warning = cstring(&error);
            return h.service_warning.as_ptr();
        }
    }
    h.warning.as_ptr()
}
#[no_mangle]
pub unsafe extern "C" fn cowtype2_session_configure(
    h: *mut SessionHandle,
    r: *const RuntimeHandle,
) {
    protect((), || {
        let (Some(h), Some(r)) = (unsafe { h.as_mut() }, unsafe { r.as_ref() }) else {
            return;
        };
        let mut c = SessionConfig::with_page_size(r.settings.page_size).unwrap_or_default();
        c.chinese_punctuation = r.settings.punctuation;
        c.sentence = r.settings.sentence;
        c.correction = r.settings.correction;
        c.completion = r.settings.completion;
        c.initials = r.settings.initials;
        c.learning = r.settings.learning;
        c.emoji = r.settings.emoji;
        h.session.configure(c);
        h.session.set_learning(Some(r.service.state.clone()));
        h.update(h.session.frame());
    });
}
#[no_mangle]
pub unsafe extern "C" fn cowtype2_session_forget(h: *mut SessionHandle) -> bool {
    protect(false, || {
        let Some(h) = (unsafe { h.as_mut() }) else {
            return false;
        };
        let changed = h.session.forget_current();
        h.update(h.session.frame());
        changed
    })
}

/// External decoder ownership is transferred on success only. Queries are synchronous;
/// strings delivered to emit need only live until emit returns. Drop runs after all sessions.
type EmitCandidate =
    unsafe extern "C" fn(*mut std::ffi::c_void, *const c_char, *const c_char, usize, u32);
type QueryCandidates = unsafe extern "C" fn(
    *mut std::ffi::c_void,
    *const c_char,
    u32,
    EmitCandidate,
    *mut std::ffi::c_void,
);
#[derive(Debug)]
struct NativeProvider {
    context: *mut std::ffi::c_void,
    query: QueryCandidates,
    free: unsafe extern "C" fn(*mut std::ffi::c_void),
}
// The host contract requires serialization of shared decoder state and no thread affinity.
unsafe impl Send for NativeProvider {}
unsafe impl Sync for NativeProvider {}
impl Drop for NativeProvider {
    fn drop(&mut self) {
        unsafe { (self.free)(self.context) }
    }
}
impl crate::engine::CandidateProvider for NativeProvider {
    fn query(
        &self,
        raw: &str,
        sentence: bool,
        correction: bool,
        completion: bool,
        initials: bool,
    ) -> Vec<crate::engine::Candidate> {
        unsafe extern "C" fn emit(
            out: *mut std::ffi::c_void,
            text: *const c_char,
            reading: *const c_char,
            consumed: usize,
            corrected: u32,
        ) {
            if let (Ok(text), Ok(reading)) = (unsafe { string(text) }, unsafe { string(reading) }) {
                let out = unsafe { &mut *out.cast::<Vec<crate::engine::Candidate>>() };
                out.push(crate::engine::Candidate {
                    text: text.into(),
                    reading: reading.into(),
                    consumed_bytes: consumed,
                    has_remaining: false,
                    kind: if corrected != 0 {
                        crate::engine::MatchKind::Correction
                    } else {
                        crate::engine::MatchKind::Exact
                    },
                    custom: false,
                });
            }
        }
        if raw.len() > 128 || raw.as_bytes().contains(&0) {
            return Vec::new();
        }
        let mut out: Vec<crate::engine::Candidate> = Vec::new();
        let flags = sentence as u32
            | ((correction as u32) << 1)
            | ((completion as u32) << 2)
            | ((initials as u32) << 3);
        unsafe {
            (self.query)(
                self.context,
                cstring(raw).as_ptr(),
                flags,
                emit,
                (&mut out as *mut Vec<crate::engine::Candidate>).cast(),
            )
        };
        out.retain(|c| {
            c.consumed_bytes > 0
                && c.consumed_bytes <= raw.len()
                && raw.is_char_boundary(c.consumed_bytes)
                && !c.text.is_empty()
        });
        for c in &mut out {
            c.has_remaining = c.consumed_bytes < raw.len();
        }
        out
    }
}
#[no_mangle]
pub unsafe extern "C" fn cowtype2_engine_set_decoder(
    handle: *mut EngineHandle,
    context: *mut std::ffi::c_void,
    query: QueryCandidates,
    free: unsafe extern "C" fn(*mut std::ffi::c_void),
) -> bool {
    let Some(handle) = (unsafe { handle.as_mut() }) else {
        return false;
    };
    let Some(engine) = Arc::get_mut(&mut handle.engine) else {
        return false;
    };
    engine.set_provider(Arc::new(NativeProvider {
        context,
        query,
        free,
    }));
    true
}
