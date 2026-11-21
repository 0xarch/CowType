// SPDX-License-Identifier: GPL-3.0-or-later
// CowType integration inspired by fcitx5-gb18030 (KylinSoft, 2023).
// Independently implemented: strict byte validation and per-call iconv state.
#pragma once
#include <array>
#include <fcitx-utils/utf8.h>
#include <iconv.h>
#include <string>
#include <vector>

namespace cowtype {
struct InternalCodeCandidate {
  std::string text, code;
};
class InternalCode {
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
  static std::string decode(const std::string &code) {
    if (code.size() != 4 && code.size() != 8)
      return {};
    std::array<char, 4> bytes{};
    for (size_t i = 0; i < code.size() / 2; ++i) {
      int a = hex(code[2 * i]), b = hex(code[2 * i + 1]);
      if (a < 0 || b < 0)
        return {};
      bytes[i] = char(a * 16 + b);
    }
    const auto at = [&](size_t i) {
      return static_cast<unsigned char>(bytes[i]);
    };
    if (at(0) < 0x81 || at(0) > 0xfe)
      return {};
    if (code.size() == 4) {
      if (at(1) < 0x40 || at(1) > 0xfe || at(1) == 0x7f)
        return {};
    } else if (at(1) < 0x30 || at(1) > 0x39 || at(2) < 0x81 || at(2) > 0xfe ||
               at(3) < 0x30 || at(3) > 0x39)
      return {};
    auto converter = iconv_open("UTF-8", "GB18030");
    if (converter == reinterpret_cast<iconv_t>(-1))
      return {};
    char output[16]{};
    char *in = bytes.data(), *out = output;
    size_t left = code.size() / 2, available = sizeof(output);
    auto status = iconv(converter, &in, &left, &out, &available);
    iconv_close(converter);
    if (status == size_t(-1) || left)
      return {};
    std::string text(output, out);
    if (fcitx::utf8::length(text) != 1)
      return {};
    return text;
  }
  static bool supported() {
    // glibc before the 2022 mapping update may accept the same byte sequences
    // but return private-use characters. Refuse that mode instead of lying
    // about the standard or switching converters behind the user's back.
    static const bool available = [] {
      const std::array<std::pair<const char *, const char *>, 19> probes{
          {{"a6d9", "︐"},
           {"a6da", "︒"},
           {"a6db", "︑"},
           {"a6dc", "︓"},
           {"a6dd", "︔"},
           {"a6de", "︕"},
           {"a6df", "︖"},
           {"a6ec", "︗"},
           {"a6ed", "︘"},
           {"a6f3", "︙"},
           {"fe59", "龴"},
           {"fe61", "龵"},
           {"fe66", "龶"},
           {"fe67", "龷"},
           {"fe6d", "龸"},
           {"fe7e", "龹"},
           {"fe90", "龺"},
           {"fea0", "龻"},
           {"a8bc", "ḿ"}}};
      for (const auto &[code, text] : probes)
        if (decode(code) != text)
          return false;
      return true;
    }();
    return available;
  }

  static bool accepts(char c) { return hex(c) >= 0; }
  static std::vector<InternalCodeCandidate>
  candidates(const std::string &input) {
    std::vector<InternalCodeCandidate> out;
    if (input.size() == 4 || input.size() == 8) {
      auto text = decode(input);
      if (!text.empty())
        out.push_back({text, input});
    } else if (input.size() == 3 || input.size() == 7) {
      for (char c : std::string("0123456789abcdef")) {
        auto code = input + c;
        auto text = decode(code);
        if (!text.empty())
          out.push_back({text, code});
      }
    }
    return out;
  }
};
} // namespace cowtype
