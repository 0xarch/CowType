// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <fcitx-utils/macros.h>
#include <fcitx-utils/stringutils.h>
#include <stdexcept>
#include <string>
#include <string_view>

#ifndef COWTYPE_FCITX_HAS_CONSUME_VALUE
#error "Link cowtype-fcitx-compat to select the Fcitx5 API version"
#endif

namespace cowtype::compat {
// Find token boundaries locally, then use the older public escape decoder.
// Dictionary text can contain quoted words, spaces, escaped quotes and slashes.
inline std::string_view consumeLegacyValue(std::string_view &input,
                                           std::string *output) {
    const auto start = input.find_first_not_of(FCITX_WHITESPACE);
    if (start == input.npos) {
        input = {};
        return {};
    }
    const auto rest = input.substr(start);
    auto length = rest.find_first_of(FCITX_WHITESPACE);
    if (rest.front() == '"') {
        bool escaped = false;
        for (size_t i = 1; i < rest.size(); ++i) {
            if (!escaped && rest[i] == '"') {
                length = i + 1;
                break;
            }
            escaped = !escaped && rest[i] == '\\';
        }
    }
    if (length == rest.npos) length = rest.size();
    const auto token = rest.substr(0, length);
    auto decoded = fcitx::stringutils::unescapeForValue(token);
    if (!decoded) throw std::runtime_error("Invalid system dictionary escape");
    if (output) *output = std::move(*decoded);
    input = rest.substr(length);
    return token;
}

inline std::string_view consumeValue(std::string_view &input,
                                     std::string *output) {
#if COWTYPE_FCITX_HAS_CONSUME_VALUE
    return fcitx::stringutils::consumeMaybeEscapedValue(input, FCITX_WHITESPACE,
                                                     output);
#else
    return consumeLegacyValue(input, output);
#endif
}
} // namespace cowtype::compat
