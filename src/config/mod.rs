// SPDX-License-Identifier: GPL-3.0-or-later
//! New versioned settings file; does not import legacy Fcitx configuration.
use std::{
    collections::BTreeMap,
    path::{Path, PathBuf},
};
#[derive(Clone, Debug)]
pub struct Settings {
    pub page_size: usize,
    pub punctuation: bool,
    pub sentence: bool,
    pub correction: bool,
    pub completion: bool,
    pub initials: bool,
    pub learning: bool,
    pub horizontal: bool,
    pub shift_toggle: bool,
    pub remember_mode: bool,
    pub statistical_model: bool,
    pub emoji: bool,
    pub voice: bool,
    pub voice_resident: bool,
    pub voice_hotkey: u32,
    pub mode_hotkey: u32,
    pub internal_code_hotkey: u32,
    pub unicode_hotkey: u32,
    pub unicode_prefix: bool,
    pub voice_modifier_hold: bool,
    pub show_pinyin: bool,
    pub voice_always_resident: bool,
    pub page_brackets: bool,
    pub page_minus_equal: bool,
}
impl Default for Settings {
    fn default() -> Self {
        Self {
            page_size: 5,
            punctuation: true,
            sentence: true,
            correction: true,
            completion: true,
            initials: true,
            learning: true,
            horizontal: false,
            shift_toggle: true,
            remember_mode: false,
            statistical_model: false,
            emoji: false,
            voice: false,
            voice_resident: true,
            voice_hotkey: shortcut_default_index("VoiceHotkey"),
            mode_hotkey: shortcut_default_index("ModeHotkey"),
            internal_code_hotkey: shortcut_default_index("InternalCodeHotkey"),
            unicode_hotkey: shortcut_default_index("UnicodeHotkey"),
            unicode_prefix: true,
            voice_modifier_hold: false,
            show_pinyin: true,
            voice_always_resident: false,
            page_brackets: true,
            page_minus_equal: true,
        }
    }
}
// Stable ordering is shared with the native ABI and GTK settings. New actions
// must register here before getting their own key-event dispatch branch.
fn shortcut_default_index(action: &str) -> u32 {
    let default = include_str!("shortcuts.tsv")
        .lines()
        .find_map(|line| {
            let mut columns = line.split('\t');
            (columns.next()? == action).then(|| columns.next().unwrap())
        })
        .expect("registered shortcut action");
    shortcut_choices(action)
        .iter()
        .position(|key| *key == default)
        .expect("registered default") as u32
}
fn shortcut_choices(action: &str) -> Vec<&'static str> {
    include_str!("shortcuts.tsv")
        .lines()
        .find_map(|line| {
            let mut columns = line.split('\t');
            (columns.next()? == action).then(|| columns.nth(1).unwrap().split(',').collect())
        })
        .expect("registered shortcut action")
}
fn shortcut_physical_keys(key: &str) -> Vec<&str> {
    match key {
        "None" => vec![],
        "Shift" => vec!["Shift_L", "Shift_R"],
        "Control" => vec!["Control_L", "Control_R"],
        "Alt" => vec!["Alt_L", "Alt_R"],
        "Super" => vec!["Super_L", "Super_R"],
        other => vec![other],
    }
}
pub fn path() -> PathBuf {
    std::env::var_os("XDG_CONFIG_HOME")
        .filter(|v| !v.is_empty())
        .map(PathBuf::from)
        .unwrap_or_else(|| {
            PathBuf::from(std::env::var_os("HOME").unwrap_or_default()).join(".config")
        })
        .join("cowtype/settings-v1.ini")
}
impl Settings {
    pub fn parse(text: &str) -> Result<Self, &'static str> {
        let mut values = BTreeMap::new();
        for line in text.lines().map(str::trim) {
            if line.is_empty() || line.starts_with(['#', ';']) || line == "[CowType]" {
                continue;
            }
            let (key, value) = line.split_once('=').ok_or("invalid settings line")?;
            values.insert(key.trim(), value.trim());
        }
        if values.get("Version") != Some(&"1") {
            return Err("unsupported settings version");
        }
        let mut settings = Self::default();
        if let Some(value) = values.get("PageSize") {
            settings.page_size = value.parse().map_err(|_| "invalid page size")?;
            if !(3..=9).contains(&settings.page_size) {
                return Err("page size must be 3–9");
            }
        }
        for (name, target) in [
            ("ModeHotkey", &mut settings.mode_hotkey),
            ("InternalCodeHotkey", &mut settings.internal_code_hotkey),
            ("VoiceHotkey", &mut settings.voice_hotkey),
            ("UnicodeHotkey", &mut settings.unicode_hotkey),
        ] {
            // Explicit unified binding wins over the removed legacy Shift switch.
            let legacy = if name == "ModeHotkey"
                && !values.contains_key(name)
                && values
                    .get("ShiftToggle")
                    .is_some_and(|value| matches!(*value, "True" | "true"))
            {
                Some("Shift")
            } else {
                None
            };
            if let Some(value) = values.get(name).copied().or(legacy) {
                *target = shortcut_choices(name)
                    .iter()
                    .position(|key| *key == value)
                    .ok_or("unsupported feature shortcut")? as u32;
            }
        }
        // A new default must not invalidate an existing voice F9 binding.
        // Only an absent Unicode binding may be disabled during migration.
        if !values.contains_key("UnicodeHotkey") {
            let unicode_keys = shortcut_physical_keys(
                shortcut_choices("UnicodeHotkey")[settings.unicode_hotkey as usize],
            );
            let overlaps = [
                ("ModeHotkey", settings.mode_hotkey),
                ("VoiceHotkey", settings.voice_hotkey),
                ("InternalCodeHotkey", settings.internal_code_hotkey),
            ]
            .iter()
            .any(|(name, index)| {
                shortcut_physical_keys(shortcut_choices(name)[*index as usize])
                    .iter()
                    .any(|key| unicode_keys.contains(key))
            });
            if overlaps {
                settings.unicode_hotkey = shortcut_choices("UnicodeHotkey")
                    .iter()
                    .position(|key| *key == "None")
                    .unwrap() as u32;
            }
        }
        let mut assigned = std::collections::BTreeSet::new();
        for (action, index) in [
            ("ModeHotkey", settings.mode_hotkey),
            ("InternalCodeHotkey", settings.internal_code_hotkey),
            ("VoiceHotkey", settings.voice_hotkey),
            ("UnicodeHotkey", settings.unicode_hotkey),
        ] {
            let choices = shortcut_choices(action);
            let key = choices[index as usize];
            for physical in shortcut_physical_keys(key) {
                if !assigned.insert(physical) {
                    return Err("feature shortcut conflict");
                }
            }
        }
        for (name, target) in [
            ("Punctuation", &mut settings.punctuation),
            ("Sentence", &mut settings.sentence),
            ("Correction", &mut settings.correction),
            ("Completion", &mut settings.completion),
            ("Initials", &mut settings.initials),
            ("Learning", &mut settings.learning),
            ("Horizontal", &mut settings.horizontal),
            ("ShiftToggle", &mut settings.shift_toggle),
            ("RememberMode", &mut settings.remember_mode),
            ("StatisticalModel", &mut settings.statistical_model),
            ("Emoji", &mut settings.emoji),
            ("UnicodePrefix", &mut settings.unicode_prefix),
            ("Voice", &mut settings.voice),
            ("VoiceResident", &mut settings.voice_resident),
            ("VoiceModifierHold", &mut settings.voice_modifier_hold),
            ("ShowPinyin", &mut settings.show_pinyin),
            ("VoiceAlwaysResident", &mut settings.voice_always_resident),
            ("PageBrackets", &mut settings.page_brackets),
            ("PageMinusEqual", &mut settings.page_minus_equal),
        ] {
            if let Some(value) = values.get(name) {
                *target = match *value {
                    "True" | "true" => true,
                    "False" | "false" => false,
                    _ => return Err("invalid boolean setting"),
                };
            }
        }
        Ok(settings)
    }
    pub fn load(path: &Path) -> Result<Self, String> {
        match std::fs::read_to_string(path) {
            Ok(text) if text.len() <= 65536 => Self::parse(&text).map_err(str::to_owned),
            Ok(_) => Err("settings file too large".into()),
            Err(e) if e.kind() == std::io::ErrorKind::NotFound => Ok(Self::default()),
            Err(_) => {
                Err("settings cannot be read; using defaults without overwriting file".into())
            }
        }
    }
}

#[cfg(test)]
mod hotkey_tests {
    use super::*;
    #[test]
    fn unicode_prefix_is_default_on_and_independent_of_hotkey() {
        assert!(
            Settings::parse("Version=1\nUnicodeHotkey=None\n")
                .unwrap()
                .unicode_prefix
        );
        let disabled = Settings::parse("Version=1\nUnicodePrefix=False\n").unwrap();
        assert!(!disabled.unicode_prefix);
        assert_eq!(
            disabled.unicode_hotkey,
            shortcut_default_index("UnicodeHotkey")
        );
        assert!(Settings::parse("Version=1\nUnicodePrefix=invalid\n").is_err());
    }
    #[test]
    fn feature_shortcuts_are_unique_including_disabled_features() {
        let default = Settings::parse("Version=1\n").unwrap();
        assert_eq!(default.mode_hotkey, 5);
        assert_eq!(default.internal_code_hotkey, 6);
        for text in [
            "ModeHotkey=F7",
            "InternalCodeHotkey=F8",
            "VoiceHotkey=F6",
            "InternalCodeHotkey=F13",
        ] {
            assert!(Settings::parse(&format!("Version=1\n{text}\n")).is_err());
        }
        assert!(Settings::parse("Version=1\nModeHotkey=None\nInternalCodeHotkey=None\n").is_ok());
        assert!(Settings::parse(
            "Version=1\nModeHotkey=F11\nInternalCodeHotkey=F12\nVoiceHotkey=Alt_L\n"
        )
        .is_ok());
    }
    #[test]
    fn modifiers_are_shared_and_generic_bindings_conflict_with_either_side() {
        assert_eq!(
            shortcut_choices("ModeHotkey"),
            shortcut_choices("VoiceHotkey")
        );
        assert_eq!(
            shortcut_choices("ModeHotkey"),
            shortcut_choices("InternalCodeHotkey")
        );
        for role in [
            "ModeHotkey",
            "VoiceHotkey",
            "InternalCodeHotkey",
            "UnicodeHotkey",
        ] {
            for key in [
                "Shift",
                "Alt",
                "Control",
                "Super",
                "Shift_R",
                "Control_L",
                "None",
            ] {
                assert!(Settings::parse(&format!("Version=1\n{role}={key}\n")).is_ok());
            }
        }
        for (generic, side) in [
            ("Shift", "Shift_R"),
            ("Control", "Control_L"),
            ("Alt", "Alt_R"),
        ] {
            assert!(Settings::parse(&format!(
                "Version=1\nModeHotkey={generic}\nVoiceHotkey={side}\n"
            ))
            .is_err());
        }
        let legacy = Settings::parse("Version=1\nShiftToggle=True\n").unwrap();
        assert_eq!(
            shortcut_choices("ModeHotkey")[legacy.mode_hotkey as usize],
            "Shift"
        );
        assert_eq!(
            Settings::parse("Version=1\nShiftToggle=True\nModeHotkey=F6\n")
                .unwrap()
                .mode_hotkey,
            5
        );
    }
    #[test]
    fn unicode_has_its_own_binding_and_respects_old_voice_f9_configs() {
        assert_eq!(Settings::default().unicode_hotkey, 8);
        let old = Settings::parse("Version=1\nVoiceHotkey=F9\n").unwrap();
        assert_eq!(old.voice_hotkey, 8);
        assert_eq!(old.unicode_hotkey, 12);
        assert!(Settings::parse("Version=1\nVoiceHotkey=F9\nUnicodeHotkey=F9\n").is_err());
        let new = Settings::parse("Version=1\nUnicodeHotkey=Control_R\nInternalCodeHotkey=Alt_L\n")
            .unwrap();
        assert_ne!(new.unicode_hotkey, new.internal_code_hotkey);
    }
    #[test]
    fn cldr_symbols_are_explicit_and_default_off() {
        assert!(!Settings::parse("Version=1\n").unwrap().emoji);
        assert!(Settings::parse("Version=1\nEmoji=True\n").unwrap().emoji);
        assert!(Settings::parse("Version=1\nEmoji=auto\n").is_err());
    }
    #[test]
    fn statistical_model_is_explicit_and_defaults_off() {
        assert!(!Settings::parse("Version=1\n").unwrap().statistical_model);
        assert!(
            Settings::parse("Version=1\nStatisticalModel=True\n")
                .unwrap()
                .statistical_model
        );
        assert!(Settings::parse("Version=1\nStatisticalModel=auto\n").is_err());
    }
    #[test]
    fn voice_shortcut_defaults_and_rejects_unknown_keys() {
        let mut settings = String::from("[CowType]\nVersion=1\n");
        assert_eq!(Settings::parse(&settings).unwrap().voice_hotkey, 7);
        settings.push_str("VoiceHotkey=ISO_Level3_Shift\n");
        assert_eq!(Settings::parse(&settings).unwrap().voice_hotkey, 19);
        settings.push_str("VoiceHotkey=Unknown\n");
        assert!(Settings::parse(&settings).is_err());
    }
}
