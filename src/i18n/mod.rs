// SPDX-License-Identifier: GPL-3.0-or-later
//! Resource-only catalogs shared by every host. Diagnostic callers use en().
const EN: &str = include_str!("../../i18n/en_US.tsv");
const ZH: &str = include_str!("../../i18n/zh_CN.tsv");
fn lookup(chinese: bool, key: &str) -> Option<&'static str> {
    use std::{collections::BTreeMap, sync::OnceLock};
    static EN_TEXT: OnceLock<BTreeMap<&'static str, String>> = OnceLock::new();
    static ZH_TEXT: OnceLock<BTreeMap<&'static str, String>> = OnceLock::new();
    let (cache, catalog) = if chinese {
        (&ZH_TEXT, ZH)
    } else {
        (&EN_TEXT, EN)
    };
    cache
        .get_or_init(|| {
            catalog
                .lines()
                .filter_map(|line| line.split_once('\t'))
                .map(|(key, text)| (key, text.replace("\\n", "\n")))
                .collect()
        })
        .get(key)
        .map(String::as_str)
}

pub fn en(key: &str) -> &str {
    lookup(false, key).unwrap_or(key)
}
pub fn tr(key: &str) -> &str {
    let locale = std::env::var("COWTYPE_LANGUAGE")
        .ok()
        .filter(|s| !s.is_empty())
        .or_else(|| std::env::var("LC_ALL").ok().filter(|s| !s.is_empty()))
        .or_else(|| std::env::var("LC_MESSAGES").ok().filter(|s| !s.is_empty()))
        .or_else(|| std::env::var("LANG").ok())
        .unwrap_or_default();
    if locale.starts_with("zh") {
        lookup(true, key).unwrap_or_else(|| en(key))
    } else {
        en(key)
    }
}

#[cfg(test)]
mod tests {
    #[test]
    fn shared_catalogs_decode_line_breaks_and_preserve_lua_braces() {
        for chinese in [false, true] {
            let text = super::lookup(chinese, "scripts.editor_hint").unwrap();
            assert!(text.contains('\n'));
            assert!(!text.contains("\\n"));
            assert!(text.contains('{'));
        }
        assert_eq!(
            super::en("unknown.translation.key"),
            "unknown.translation.key"
        );
    }
}
