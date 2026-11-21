// SPDX-License-Identifier: GPL-3.0-or-later
//! Simplified Chinese punctuation behavior.
//! Public reference: fcitx5-chinese-addons e8d930f4 punctuation zh_CN profile.
//! Public character mapping references are recorded in docs/SOURCES.json.
#[derive(Debug, Default)]
pub(super) struct Punctuation {
    single_closing: bool,
    double_closing: bool,
}
impl Punctuation {
    pub fn has(ch: char) -> bool {
        matches!(
            ch,
            ',' | '.'
                | '?'
                | '!'
                | ':'
                | ';'
                | '\''
                | '"'
                | '<'
                | '>'
                | '\\'
                | '#'
                | '$'
                | '%'
                | '^'
                | '_'
                | '('
                | ')'
                | '['
                | ']'
                | '{'
                | '}'
                | '~'
                | '`'
        )
    }
    pub fn convert(&mut self, ch: char) -> &'static str {
        match ch {
            ',' => "，",
            '.' => "。",
            '?' => "？",
            '!' => "！",
            ':' => "：",
            ';' => "；",
            '\'' => {
                self.single_closing = !self.single_closing;
                if self.single_closing {
                    "‘"
                } else {
                    "’"
                }
            }
            '"' => {
                self.double_closing = !self.double_closing;
                if self.double_closing {
                    "“"
                } else {
                    "”"
                }
            }
            '<' => "《",
            '>' => "》",
            '\\' => "、",
            '#' => "＃",
            '$' => "￥",
            '%' => "％",
            '^' => "……",
            '_' => "——",
            '(' => "（",
            ')' => "）",
            '[' => "【",
            ']' => "】",
            '{' => "｛",
            '}' => "｝",
            '~' => "～",
            '`' => "·",
            _ => "",
        }
    }
    pub fn reset(&mut self) {
        *self = Self::default();
    }
}
