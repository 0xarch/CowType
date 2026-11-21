// SPDX-License-Identifier: GPL-3.0-or-later
#include "../../src-fcitx5/engine/shortcuts.h"
#include "../../src-fcitx5/gb18030/decoder.h"
#include <cassert>
#include <iostream>
int main() {
  assert(cowtype::InternalCode::supported());
  assert(cowtype::InternalCode::decode("D6D0") == "中");
  assert(cowtype::InternalCode::decode("b9fa") == "国");
  assert(cowtype::InternalCode::decode("90308130") == "𐀀");
  assert(cowtype::InternalCode::decode("9439fc36") == "😀");
  assert(cowtype::InternalCode::decode("a6d9") == "︐");
  assert(cowtype::InternalCode::decode("fe59") == "龴");
  for (const auto &code : {"817f", "8030", "81308030", "ff30ff30", "9030813a",
                           "1234", "xyz1", "d6d", "d6d000"})
    assert(cowtype::InternalCode::decode(code).empty());
  auto candidates = cowtype::InternalCode::candidates("d6d");
  assert(candidates.size() == 16);
  assert(candidates[0].text == "中" && candidates[0].code == "d6d0");
  auto four = cowtype::InternalCode::candidates("9030813");
  assert(four.size() == 10);
  assert(Shortcuts::resolve(FcitxKey_F6, 0, 5, 6, 0, true) ==
         Shortcuts::Action::Mode);
  assert(Shortcuts::resolve(FcitxKey_F7, 0, 5, 6, 0, true) ==
         Shortcuts::Action::InternalCode);
  assert(Shortcuts::resolve(FcitxKey_F7, 4, 5, 6, 0, true) ==
         Shortcuts::Action::None);
  assert(Shortcuts::resolve(FcitxKey_F8, 0, 5, 6, 0, true) ==
         Shortcuts::Action::Voice);
  assert(Shortcuts::resolve(FcitxKey_F6, 0, 12, 6, 0, false) ==
         Shortcuts::Action::None);
  std::cout << "GB18030 byte validation, 2022 mappings, prefix candidates, "
               "Unicode range and shortcut dispatch verified\n";
}
