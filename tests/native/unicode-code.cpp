// SPDX-License-Identifier: GPL-3.0-or-later
#include "../../src-fcitx5/unicode/decoder.h"
#include <cassert>
#include <iostream>
int main() {
  assert(cowtype::UnicodeCode::decode("4e2d") == "中");
  assert(cowtype::UnicodeCode::decode("U+1F600") == "😀");
  assert(cowtype::UnicodeCode::decode("u+20000") == "𠀀");
  assert(cowtype::UnicodeCode::decode("u0020") == " ");
  assert(cowtype::UnicodeCode::decode("U4e2d") == "中");
  assert(cowtype::UnicodeCode::decode("000041") == "A");
  assert(cowtype::UnicodeCode::decode("20") == " ");
  assert(cowtype::UnicodeCode::decode("10FFFF") ==
         std::string("\xf4\x8f\xbf\xbf"));
  assert(cowtype::UnicodeCode::decode("E000") == std::string("\xee\x80\x80"));
  assert(cowtype::UnicodeCode::decode("200D") == std::string("\xe2\x80\x8d"));
  for (const auto *input :
       {"", "u", "U", "U+", "uD800", "u110000", "u0000", "D800", "dfff", "110000", "1FFFFF", "000000", "a", "7f", "9f",
        "1234567", "U++4E2D", "0x4e2d", "-1", "中"})
    assert(cowtype::UnicodeCode::decode(input).empty());
  assert(cowtype::UnicodeCode::accepts("", 'U'));
  assert(cowtype::UnicodeCode::accepts("U", '+'));
  assert(cowtype::UnicodeCode::accepts("U", '4'));
  assert(!cowtype::UnicodeCode::accepts("u10FFFF", '0'));
  assert(!cowtype::UnicodeCode::accepts("u4", '+'));
  assert(!cowtype::UnicodeCode::accepts("U+10FFFF", '0'));
  auto result = cowtype::UnicodeCode::candidates("u+1f600");
  assert(result.size() == 1 && result[0].code == "U+1F600" &&
         result[0].text == "😀");
  std::cout << "Unicode scalar decoding, ranges, controls, surrogate rejection "
               "and prefixed input verified\n";
}
