// SPDX-License-Identifier: GPL-3.0-or-later
// Direct Unicode scalar input. No GB18030 conversion or external tables.
#pragma once
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace cowtype {
struct UnicodeCandidate {
  std::string text, code;
};
class UnicodeCode {
  static int hex(char c) {
    if (c >= '0' && c <= '9')
      return c - '0';
    if (c >= 'a' && c <= 'f')
      return c - 'a' + 10;
    if (c >= 'A' && c <= 'F')
      return c - 'A' + 10;
    return -1;
  }

public:
  static std::string_view digits(std::string_view input) {
    if (input.starts_with("U+") || input.starts_with("u+"))
      input.remove_prefix(2);
    else if (input.starts_with("U") || input.starts_with("u"))
      input.remove_prefix(1);
    return input;
  }
  static bool accepts(std::string_view input, char c) {
    if (input.empty() && (c == 'u' || c == 'U'))
      return true;
    if ((input == "u" || input == "U") && c == '+')
      return true;
    return hex(c) >= 0 && digits(input).size() < 6;
  }
  static std::string decode(std::string_view input) {
    auto value = digits(input);
    if (value.empty() || value.size() > 6)
      return {};
    uint32_t cp = 0;
    for (char c : value) {
      int digit = hex(c);
      if (digit < 0)
        return {};
      cp = cp * 16 + digit;
    }
    // Exclude surrogate code points and C0/C1 controls (including NUL).
    // Format/combining characters, private-use and noncharacters remain
    // legitimate scalar input; rendering is the application's responsibility.
    if (cp > 0x10ffff || (cp >= 0xd800 && cp <= 0xdfff) || cp < 0x20 ||
        (cp >= 0x7f && cp <= 0x9f))
      return {};
    std::string text;
    auto byte = [&](uint32_t v) { text += static_cast<char>(v); };
    if (cp < 0x80)
      byte(cp);
    else if (cp < 0x800) {
      byte(0xc0 | (cp >> 6));
      byte(0x80 | (cp & 63));
    } else if (cp < 0x10000) {
      byte(0xe0 | (cp >> 12));
      byte(0x80 | ((cp >> 6) & 63));
      byte(0x80 | (cp & 63));
    } else {
      byte(0xf0 | (cp >> 18));
      byte(0x80 | ((cp >> 12) & 63));
      byte(0x80 | ((cp >> 6) & 63));
      byte(0x80 | (cp & 63));
    }
    return text;
  }
  static std::vector<UnicodeCandidate> candidates(const std::string &input) {
    auto text = decode(input);
    if (text.empty())
      return {};
    std::string code = "U+";
    for (char c : digits(input))
      code += c >= 'a' && c <= 'f' ? char(c - 'a' + 'A') : c;
    return {{text, code}};
  }
};
} // namespace cowtype
