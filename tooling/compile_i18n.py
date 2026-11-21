#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""Generate native resources from the same catalogs used by Rust and Python."""
import json
import sys
from pathlib import Path
root, target = map(Path, sys.argv[1:])
def read(name):
    return dict(l.split('\t', 1) for l in (root / (name + '.tsv')).read_text().splitlines())
en, zh = read('en_US'), read('zh_CN')
assert en.keys() == zh.keys()
q = lambda s: json.dumps(s, ensure_ascii=False)
text = '''// Generated from i18n/*.tsv. Do not edit.
#pragma once
#include <cstdlib>
#include <string_view>
inline const char *ct_tr(std::string_view key) {
 const char *locale=nullptr;
 for(const char *name:{"COWTYPE_LANGUAGE","LC_ALL","LC_MESSAGES","LANG"}) {
  const auto value=std::getenv(name);if(value && *value){locale=value;break;}
 }
 bool chinese=locale && std::string_view(locale).starts_with("zh");
'''
text += ''.join(f' if(key=={q(k)}) return chinese?{q(zh[k])}:{q(v)};\n' for k,v in en.items())
text += ' return "Missing translation";\n}\n'
target.write_text(text)
