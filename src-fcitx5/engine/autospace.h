// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <fcitx/inputcontext.h>
#include <fcitx/surroundingtext.h>
#include <fcitx-utils/utf8.h>
#include <string>
#include <vector>

namespace cowtype {
inline bool han(uint32_t c) {
    return (c>=0x3400 && c<=0x4dbf) || (c>=0x4e00 && c<=0x9fff) ||
           (c>=0xf900 && c<=0xfaff) || (c>=0x20000 && c<=0x323af);
}
inline bool latinNumber(uint32_t c) {
    return (c>='a' && c<='z') || (c>='A' && c<='Z') || (c>='0' && c<='9');
}
inline bool spaceBoundary(uint32_t a,uint32_t b) {
    return (han(a) && latinNumber(b)) || (latinNumber(a) && han(b));
}
// No retained context: applications remain the authority after cursor moves,
// deletions and field switches. Explicit whitespace and punctuation are preserved.
inline std::string autoSpace(const std::string &text,uint32_t previous) {
    std::string result;
    for(auto c:fcitx::utf8::MakeUTF8CharRange(text)) {
        if(spaceBoundary(previous,c)) result+=' ';
        result+=fcitx::utf8::UCS4ToUTF8(c);
        previous=c;
    }
    return result;
}
inline bool spacingContext(const fcitx::InputContext *ic) {
    using fcitx::CapabilityFlag;
    const auto flags=ic->capabilityFlags();
    return flags.test(CapabilityFlag::SurroundingText) &&
        !flags.testAny(fcitx::CapabilityFlags(CapabilityFlag::PasswordOrSensitive)|
            CapabilityFlag::Disable|CapabilityFlag::Url|CapabilityFlag::Email|
            CapabilityFlag::Number|CapabilityFlag::Digit|CapabilityFlag::Dialable|
            CapabilityFlag::Terminal) &&
        ic->surroundingText().isValid() &&
        ic->surroundingText().cursor()==ic->surroundingText().anchor();
}
inline uint32_t previousCharacter(const fcitx::InputContext *ic) {
    const auto &surrounding=ic->surroundingText();
    unsigned position=0;
    uint32_t previous=0;
    for(auto c:fcitx::utf8::MakeUTF8CharRange(surrounding.text())) {
        if(position++==surrounding.cursor()) break;
        previous=c;
    }
    return previous;
}
} // namespace cowtype
