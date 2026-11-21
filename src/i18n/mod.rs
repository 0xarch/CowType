// SPDX-License-Identifier: GPL-3.0-or-later
//! Resource-only catalogs shared by every host. Diagnostic callers use en().
const EN: &str = include_str!("../../i18n/en_US.tsv");
const ZH: &str = include_str!("../../i18n/zh_CN.tsv");
fn lookup(catalog: &'static str, key: &str) -> Option<&'static str> {
    catalog
        .lines()
        .filter_map(|l| l.split_once('\t'))
        .find(|(k, _)| *k == key)
        .map(|(_, text)| text)
}
pub fn en(key: &str) -> &str {
    lookup(EN, key).unwrap_or(key)
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
        lookup(ZH, key).unwrap_or_else(|| en(key))
    } else {
        en(key)
    }
}
