// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <array>
#include <fcitx-utils/key.h>
// All CowType feature trigger keys are resolved here. Conflict validation lives
// in the shared settings schema (Rust/Python); normal editing keys remain
// local.
class Shortcuts {
public:
  enum class Action { None, Mode, InternalCode, Voice };
  static fcitx::KeySym function(unsigned index) {
    return index < 12 ? static_cast<fcitx::KeySym>(FcitxKey_F1 + index)
                      : FcitxKey_None;
  }
  static fcitx::KeySym voice(unsigned index) {
    static constexpr std::array<fcitx::KeySym, 7> keys{
        FcitxKey_F8,       FcitxKey_F9,    FcitxKey_F10,
        FcitxKey_Alt_L,    FcitxKey_Alt_R, FcitxKey_ISO_Level3_Shift,
        FcitxKey_Control_R};
    return keys[index < keys.size() ? index : 0];
  }
  static Action resolve(fcitx::KeySym sym, unsigned modifiers, unsigned mode,
                        unsigned internal, unsigned speech, bool voiceEnabled) {
    if (!(modifiers & (1 | 4 | 8 | 64 | 128))) {
      if (sym != FcitxKey_None && sym == function(mode))
        return Action::Mode;
      if (sym != FcitxKey_None && sym == function(internal))
        return Action::InternalCode;
    }
    if (voiceEnabled && sym == voice(speech))
      return Action::Voice;
    return Action::None;
  }
};
