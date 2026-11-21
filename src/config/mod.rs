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
    pub voice: bool,
    pub voice_resident: bool,
    pub voice_hotkey: u32,
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
            voice: false,
            voice_resident: true,
            voice_hotkey: 0,
            voice_modifier_hold: false,
            show_pinyin: true,
            voice_always_resident: false,
            page_brackets: true,
            page_minus_equal: true,
        }
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
        if let Some(value) = values.get("VoiceHotkey") {
            settings.voice_hotkey = [
                "F8",
                "F9",
                "F10",
                "Alt_L",
                "Alt_R",
                "ISO_Level3_Shift",
                "Control_R",
            ]
            .iter()
            .position(|name| name == value)
            .ok_or("unsupported voice shortcut")? as u32;
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
        assert_eq!(Settings::parse(&settings).unwrap().voice_hotkey, 0);
        settings.push_str("VoiceHotkey=ISO_Level3_Shift\n");
        assert_eq!(Settings::parse(&settings).unwrap().voice_hotkey, 5);
        settings.push_str("VoiceHotkey=Super_L\n");
        assert!(Settings::parse(&settings).is_err());
    }
}
