// SPDX-License-Identifier: GPL-3.0-or-later
//! New versioned settings file; does not import legacy Fcitx configuration.
use std::{
    collections::BTreeMap,
    path::{Path, PathBuf},
};
pub const SHUANGPIN_PROFILES: [&str; 9] = [
    "Ziranma",
    "MS",
    "Ziguang",
    "ABC",
    "Zhongwenzhixing",
    "PinyinJiajia",
    "Xiaohe",
    "GB",
    "Custom",
];
#[derive(Clone, Debug)]
pub struct Settings {
    pub page_size: usize,
    pub auto_space: bool,
    pub shuangpin: bool,
    pub shuangpin_profile: u32,
    pub shuangpin_custom: String,
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
    pub system_dictionary: bool,
    pub emoji: bool,
    pub emoji_first: bool,
    pub symbol_candidate_limit: usize,
    pub emoji_candidate_limit: usize,
    pub correction_candidate_limit: usize,
    pub emoji_skin_tone_picker: bool,
    pub emoji_default_skin_tone: u8,
    pub voice: bool,
    pub voice_resident: bool,
    pub voice_hotkey: u32,
    pub mode_hotkey: u32,
    pub internal_code_hotkey: u32,
    pub traditional_hotkey: u32,
    pub unicode_hotkey: u32,
    pub unicode_prefix: bool,
    pub translate: bool,
    pub translate_hotkey: u32,
    pub mode_hotkey_spec: String,
    pub internal_code_hotkey_spec: String,
    pub voice_hotkey_spec: String,
    pub unicode_hotkey_spec: String,
    pub translate_hotkey_spec: String,
    pub traditional_hotkey_spec: String,
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
            auto_space: false,
            shuangpin: false,
            shuangpin_profile: 0,
            shuangpin_custom: String::new(),
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
            system_dictionary: false,
            emoji: false,
            emoji_first: false,
            symbol_candidate_limit: 4,
            emoji_candidate_limit: 4,
            correction_candidate_limit: 5,
            emoji_skin_tone_picker: true,
            emoji_default_skin_tone: 0,
            voice: false,
            voice_resident: true,
            voice_hotkey: shortcut_default_index("VoiceHotkey"),
            mode_hotkey: shortcut_default_index("ModeHotkey"),
            internal_code_hotkey: shortcut_default_index("InternalCodeHotkey"),
            traditional_hotkey: shortcut_default_index("TraditionalHotkey"),
            unicode_hotkey: shortcut_default_index("UnicodeHotkey"),
            unicode_prefix: true,
            translate: false,
            translate_hotkey: shortcut_default_index("TranslateHotkey"),
            mode_hotkey_spec: "Shift".into(),
            internal_code_hotkey_spec: "Control+Shift+G".into(),
            voice_hotkey_spec: "Control+Shift+V".into(),
            unicode_hotkey_spec: "Control+Shift+U".into(),
            translate_hotkey_spec: "Control+Shift+T".into(),
            traditional_hotkey_spec: "Control+Shift+F".into(),
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
fn canonical_shortcut(value: &str) -> Result<String, &'static str> {
    let value = value.trim();
    if value == "None" {
        return Ok(value.to_string());
    }
    let tokens: Vec<_> = value.split('+').collect();
    if tokens.is_empty() || tokens.len() > 5 {
        return Err("invalid shortcut");
    }
    if tokens.len() == 1
        && matches!(
            tokens[0],
            "Shift"
                | "Control"
                | "Alt"
                | "Super"
                | "Shift_L"
                | "Shift_R"
                | "Control_L"
                | "Control_R"
                | "Alt_L"
                | "Alt_R"
                | "Super_L"
                | "Super_R"
                | "ISO_Level3_Shift"
        )
    {
        return Ok(tokens[0].to_string());
    }
    let key = *tokens.last().unwrap();
    let valid_key = if key.len() == 1 {
        key.as_bytes()[0].is_ascii_alphanumeric()
    } else if let Some(number) = key.strip_prefix('F') {
        number.parse::<u8>().is_ok_and(|n| (1..=35).contains(&n))
    } else {
        matches!(
            key,
            "space"
                | "Tab"
                | "Return"
                | "Escape"
                | "BackSpace"
                | "Delete"
                | "Left"
                | "Right"
                | "Up"
                | "Down"
                | "Home"
                | "End"
                | "Insert"
                | "Page_Up"
                | "Page_Down"
                | "comma"
                | "period"
                | "slash"
                | "semicolon"
                | "equal"
                | "minus"
                | "plus"
                | "bracketleft"
                | "bracketright"
                | "backslash"
                | "grave"
                | "apostrophe"
                | "quotedbl"
                | "less"
                | "greater"
                | "question"
                | "underscore"
                | "KP_Enter"
                | "KP_Add"
                | "KP_Subtract"
                | "KP_Multiply"
                | "KP_Divide"
                | "KP_0"
                | "KP_1"
                | "KP_2"
                | "KP_3"
                | "KP_4"
                | "KP_5"
                | "KP_6"
                | "KP_7"
                | "KP_8"
                | "KP_9"
        )
    };
    if !valid_key {
        return Err("unsupported shortcut key");
    }
    let mut modifiers = Vec::new();
    for token in &tokens[..tokens.len() - 1] {
        let modifier = match *token {
            "Ctrl" | "Control" => "Control",
            "Alt" => "Alt",
            "Shift" => "Shift",
            "Super" | "Meta" => "Super",
            _ => return Err("invalid shortcut modifier"),
        };
        if modifiers.contains(&modifier) {
            return Err("duplicate shortcut modifier");
        }
        modifiers.push(modifier);
    }
    if modifiers.is_empty() && key.len() == 1 && key.as_bytes()[0].is_ascii_alphabetic() {
        return Err("unmodified letter shortcuts are not allowed");
    }
    let key = if key.len() == 1 && key.as_bytes()[0].is_ascii_alphabetic() {
        if modifiers.contains(&"Shift") {
            key.to_ascii_uppercase()
        } else {
            key.to_ascii_lowercase()
        }
    } else {
        key.to_string()
    };
    let mut result = String::new();
    for modifier in ["Control", "Alt", "Shift", "Super"] {
        if modifiers.contains(&modifier) {
            if !result.is_empty() {
                result.push('+');
            }
            result.push_str(modifier);
        }
    }
    if !result.is_empty() {
        result.push('+');
    }
    result.push_str(&key);
    Ok(result)
}
fn shortcut_conflict(a: &str, b: &str) -> bool {
    if a == "None" || b == "None" {
        return false;
    }
    if a == b {
        return true;
    }
    let aliases = |key: &str| match key {
        "Shift" => Some(&["Shift_L", "Shift_R"][..]),
        "Control" => Some(&["Control_L", "Control_R"][..]),
        "Alt" => Some(&["Alt_L", "Alt_R"][..]),
        "Super" => Some(&["Super_L", "Super_R"][..]),
        _ => None,
    };
    aliases(a).is_some_and(|keys| keys.contains(&b))
        || aliases(b).is_some_and(|keys| keys.contains(&a))
}
pub fn path() -> PathBuf {
    std::env::var_os("XDG_CONFIG_HOME")
        .filter(|v| !v.is_empty())
        .map(PathBuf::from)
        .unwrap_or_else(|| {
            PathBuf::from(std::env::var_os("HOME").unwrap_or_default()).join(".config")
        })
        .join("cowtype/settings-v2.ini")
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
        if values.get("Version") != Some(&"2") {
            return Err("settings format is outdated or unsupported; run cowtype-config-migrate and restart CowType");
        }
        let mut settings = Self::default();
        if let Some(value) = values.get("ShuangpinProfile") {
            settings.shuangpin_profile = SHUANGPIN_PROFILES
                .iter()
                .position(|name| name == value)
                .ok_or("invalid shuangpin profile")?
                as u32;
        }
        if let Some(value) = values.get("ShuangpinCustom") {
            if value.len() > 4096
                || value.contains(['\0', '\n', '\r'])
                || (!value.is_empty() && !Path::new(value).is_absolute())
            {
                return Err("invalid shuangpin profile path");
            }
            settings.shuangpin_custom = value.to_string();
        }
        if let Some(value) = values.get("PageSize") {
            settings.page_size = value.parse().map_err(|_| "invalid page size")?;
            if !(3..=9).contains(&settings.page_size) {
                return Err("page size must be 3–9");
            }
        }
        if let Some(value) = values.get("EmojiDefaultSkinTone") {
            settings.emoji_default_skin_tone =
                value.parse().map_err(|_| "invalid emoji skin tone")?;
            if settings.emoji_default_skin_tone > 5 {
                return Err("invalid emoji skin tone");
            }
        }
        for (name, target) in [
            ("SymbolCandidateLimit", &mut settings.symbol_candidate_limit),
            ("EmojiCandidateLimit", &mut settings.emoji_candidate_limit),
            (
                "CorrectionCandidateLimit",
                &mut settings.correction_candidate_limit,
            ),
        ] {
            if let Some(value) = values.get(name) {
                *target = value.parse().map_err(|_| "invalid candidate limit")?;
                if *target > 12 {
                    return Err("candidate limit must be 0–12");
                }
            }
        }
        for (name, target, legacy_target) in [
            (
                "ModeHotkey",
                &mut settings.mode_hotkey_spec,
                &mut settings.mode_hotkey,
            ),
            (
                "InternalCodeHotkey",
                &mut settings.internal_code_hotkey_spec,
                &mut settings.internal_code_hotkey,
            ),
            (
                "VoiceHotkey",
                &mut settings.voice_hotkey_spec,
                &mut settings.voice_hotkey,
            ),
            (
                "UnicodeHotkey",
                &mut settings.unicode_hotkey_spec,
                &mut settings.unicode_hotkey,
            ),
            (
                "TranslateHotkey",
                &mut settings.translate_hotkey_spec,
                &mut settings.translate_hotkey,
            ),
            (
                "TraditionalHotkey",
                &mut settings.traditional_hotkey_spec,
                &mut settings.traditional_hotkey,
            ),
        ] {
            if let Some(value) = values.get(name) {
                *target = canonical_shortcut(value)?;
                *legacy_target = shortcut_choices(name)
                    .iter()
                    .position(|key| *key == target.as_str())
                    .unwrap_or(0) as u32;
            }
        }
        let shortcuts = [
            &settings.mode_hotkey_spec,
            &settings.internal_code_hotkey_spec,
            &settings.voice_hotkey_spec,
            &settings.unicode_hotkey_spec,
            &settings.translate_hotkey_spec,
            &settings.traditional_hotkey_spec,
        ];
        for i in 0..shortcuts.len() {
            for j in 0..i {
                if shortcut_conflict(shortcuts[i], shortcuts[j]) {
                    return Err("feature shortcut conflict");
                }
            }
        }
        for (name, target) in [
            ("AutoSpace", &mut settings.auto_space),
            ("Shuangpin", &mut settings.shuangpin),
            ("Punctuation", &mut settings.punctuation),
            ("Sentence", &mut settings.sentence),
            ("Correction", &mut settings.correction),
            ("Completion", &mut settings.completion),
            ("Initials", &mut settings.initials),
            ("Learning", &mut settings.learning),
            ("Horizontal", &mut settings.horizontal),
            ("RememberMode", &mut settings.remember_mode),
            ("StatisticalModel", &mut settings.statistical_model),
            ("SystemDictionary", &mut settings.system_dictionary),
            ("Emoji", &mut settings.emoji),
            ("EmojiFirst", &mut settings.emoji_first),
            ("EmojiSkinTonePicker", &mut settings.emoji_skin_tone_picker),
            ("UnicodePrefix", &mut settings.unicode_prefix),
            ("Translate", &mut settings.translate),
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
            Err(e) if e.kind() == std::io::ErrorKind::NotFound => {
                if path.with_file_name("settings-v1.ini").exists() {
                    Err(
                        "legacy settings detected; run cowtype-config-migrate and restart CowType"
                            .into(),
                    )
                } else {
                    Ok(Self::default())
                }
            }
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
    fn translation_is_optional_and_shortcuts_are_full_specs() {
        let defaults = Settings::parse("Version=2\n").unwrap();
        assert!(!defaults.translate);
        assert_eq!(defaults.translate_hotkey_spec, "Control+Shift+T");
        let old = Settings::parse("Version=2\nVoiceHotkey=F10\n").unwrap();
        assert_eq!(old.voice_hotkey_spec, "F10");
        assert!(Settings::parse("Version=2\nVoiceHotkey=F10\nTranslateHotkey=F10\n").is_err());
        assert!(
            Settings::parse("Version=2\nTranslate=True\nTranslateHotkey=Control\n")
                .unwrap()
                .translate
        );
    }
    #[test]
    fn unicode_prefix_is_default_on_and_independent_of_hotkey() {
        assert!(
            Settings::parse("Version=2\nUnicodeHotkey=None\n")
                .unwrap()
                .unicode_prefix
        );
        let disabled = Settings::parse("Version=2\nUnicodePrefix=False\n").unwrap();
        assert!(!disabled.unicode_prefix);
        assert_eq!(disabled.unicode_hotkey_spec, "Control+Shift+U");
        assert!(Settings::parse("Version=2\nUnicodePrefix=invalid\n").is_err());
    }
    #[test]
    fn feature_shortcuts_are_unique_including_disabled_features() {
        let default = Settings::parse("Version=2\n").unwrap();
        assert_eq!(default.mode_hotkey_spec, "Shift");
        assert_eq!(default.internal_code_hotkey_spec, "Control+Shift+G");
        assert_eq!(
            canonical_shortcut("Control+comma"),
            Ok("Control+comma".into())
        );
        for text in [
            "ModeHotkey=Control+Shift+G",
            "InternalCodeHotkey=Control+Shift+V",
            "InternalCodeHotkey=F36",
        ] {
            assert!(Settings::parse(&format!("Version=2\n{text}\n")).is_err());
        }
        assert!(Settings::parse("Version=2\nModeHotkey=None\nInternalCodeHotkey=None\n").is_ok());
        assert!(Settings::parse(
            "Version=2\nModeHotkey=F11\nInternalCodeHotkey=F12\nVoiceHotkey=Alt_L\n"
        )
        .is_ok());
        assert!(Settings::parse("Version=2\nTraditionalHotkey=Control+Shift+g\n").is_err());
    }
    #[test]
    fn modifiers_are_shared_and_generic_bindings_conflict_with_either_side() {
        for role in [
            "ModeHotkey",
            "VoiceHotkey",
            "InternalCodeHotkey",
            "UnicodeHotkey",
            "TranslateHotkey",
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
                let mut text = String::from("Version=2\n");
                for other in [
                    "ModeHotkey",
                    "VoiceHotkey",
                    "InternalCodeHotkey",
                    "UnicodeHotkey",
                    "TranslateHotkey",
                    "TraditionalHotkey",
                ] {
                    if other != role {
                        text.push_str(&format!("{other}=None\n"));
                    }
                }
                text.push_str(&format!("{role}={key}\n"));
                assert!(Settings::parse(&text).is_ok());
            }
        }
        for (generic, side) in [
            ("Shift", "Shift_R"),
            ("Control", "Control_L"),
            ("Alt", "Alt_R"),
        ] {
            assert!(Settings::parse(&format!(
                "Version=2\nModeHotkey={generic}\nVoiceHotkey={side}\n"
            ))
            .is_err());
        }
        assert!(Settings::parse("Version=1\nShiftToggle=True\n").is_err());
    }
    #[test]
    fn unicode_has_its_own_binding_and_rejects_conflicts() {
        assert_eq!(Settings::default().unicode_hotkey_spec, "Control+Shift+U");
        let old = Settings::parse("Version=2\nVoiceHotkey=F9\n").unwrap();
        assert_eq!(old.voice_hotkey_spec, "F9");
        assert!(Settings::parse("Version=2\nVoiceHotkey=F9\nUnicodeHotkey=F9\n").is_err());
        let new = Settings::parse("Version=2\nUnicodeHotkey=Control_R\nInternalCodeHotkey=Alt_L\n")
            .unwrap();
        assert_ne!(new.unicode_hotkey, new.internal_code_hotkey);
    }
    #[test]
    fn shuangpin_and_autospace_are_optional_and_validated() {
        let defaults = Settings::parse("Version=2\n").unwrap();
        assert!(!defaults.auto_space && !defaults.shuangpin);
        assert_eq!(defaults.shuangpin_profile, 0);
        for (index, name) in SHUANGPIN_PROFILES.iter().enumerate() {
            let settings = Settings::parse(&format!("Version=2\nShuangpin=True\nShuangpinProfile={name}\nShuangpinCustom=/tmp/scheme.conf\nAutoSpace=True\n")).unwrap();
            assert!(settings.shuangpin && settings.auto_space);
            assert_eq!(settings.shuangpin_profile, index as u32);
            assert_eq!(settings.shuangpin_custom, "/tmp/scheme.conf");
        }
        for line in [
            "ShuangpinProfile=invalid",
            "ShuangpinCustom=relative",
            "AutoSpace=maybe",
        ] {
            assert!(Settings::parse(&format!("Version=2\n{line}\n")).is_err());
        }
    }
    #[test]
    fn cldr_symbols_are_explicit_and_default_off() {
        let defaults = Settings::parse("Version=2\n").unwrap();
        assert_eq!(defaults.symbol_candidate_limit, 4);
        assert_eq!(defaults.emoji_candidate_limit, 4);
        assert_eq!(defaults.correction_candidate_limit, 5);
        assert!(!defaults.emoji_first);
        assert!(
            Settings::parse("Version=2\nEmojiFirst=True\n")
                .unwrap()
                .emoji_first
        );
        assert!(Settings::parse("Version=2\nEmojiFirst=auto\n").is_err());
        assert!(defaults.emoji_skin_tone_picker);
        assert_eq!(defaults.emoji_default_skin_tone, 0);
        for tone in 0..=5 {
            let value = Settings::parse(&format!(
                "Version=2\nEmojiDefaultSkinTone={tone}\nEmojiSkinTonePicker=False\n"
            ))
            .unwrap();
            assert_eq!(value.emoji_default_skin_tone, tone);
            assert!(!value.emoji_skin_tone_picker);
        }
        for value in ["6", "-1", "invalid"] {
            assert!(
                Settings::parse(&format!("Version=2\nEmojiDefaultSkinTone={value}\n")).is_err()
            );
        }
        assert!(!Settings::parse("Version=2\n").unwrap().emoji);
        assert!(Settings::parse("Version=2\nEmoji=True\n").unwrap().emoji);
        assert!(Settings::parse("Version=2\nEmoji=auto\n").is_err());
    }
    #[test]
    fn candidate_limits_are_bounded_and_configurable() {
        let settings = Settings::parse(
            "Version=2\nSymbolCandidateLimit=0\nEmojiCandidateLimit=12\nCorrectionCandidateLimit=3\n",
        )
        .unwrap();
        assert_eq!(settings.symbol_candidate_limit, 0);
        assert_eq!(settings.emoji_candidate_limit, 12);
        assert_eq!(settings.correction_candidate_limit, 3);
        for key in [
            "SymbolCandidateLimit",
            "EmojiCandidateLimit",
            "CorrectionCandidateLimit",
        ] {
            for value in ["13", "-1", "invalid"] {
                assert!(Settings::parse(&format!("Version=2\n{key}={value}\n")).is_err());
            }
        }
    }
    #[test]
    fn system_dictionary_is_optional_and_defaults_off() {
        assert!(!Settings::parse("Version=2\n").unwrap().system_dictionary);
        assert!(
            Settings::parse("Version=2\nSystemDictionary=True\n")
                .unwrap()
                .system_dictionary
        );
        assert!(Settings::parse("Version=2\nSystemDictionary=auto\n").is_err());
    }
    #[test]
    fn statistical_model_is_explicit_and_defaults_off() {
        assert!(!Settings::parse("Version=2\n").unwrap().statistical_model);
        assert!(
            Settings::parse("Version=2\nStatisticalModel=True\n")
                .unwrap()
                .statistical_model
        );
        assert!(Settings::parse("Version=2\nStatisticalModel=auto\n").is_err());
    }
    #[test]
    fn voice_shortcut_defaults_and_rejects_unknown_keys() {
        let mut settings = String::from("[CowType]\nVersion=2\n");
        assert_eq!(Settings::parse(&settings).unwrap().voice_hotkey, 7);
        settings.push_str("VoiceHotkey=ISO_Level3_Shift\n");
        assert_eq!(Settings::parse(&settings).unwrap().voice_hotkey, 19);
        settings.push_str("VoiceHotkey=Unknown\n");
        assert!(Settings::parse(&settings).is_err());
    }
}
