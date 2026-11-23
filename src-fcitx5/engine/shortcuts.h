// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "shortcuts_registry.h"
#include <algorithm>
#include <array>
#include <cstdint>
#include <string>
#include <string_view>
#include <utility>
#include <fcitx-utils/key.h>

// One physical-key matcher and gesture state machine for every feature.
class Shortcuts {
public:
  enum class Action { None, Mode, InternalCode, Voice, Unicode, Translate, Traditional };
  static unsigned mask(fcitx::KeySym key) {
    switch (key) {
    case FcitxKey_Shift_L:
    case FcitxKey_Shift_R:
      return 1;
    case FcitxKey_Control_L:
    case FcitxKey_Control_R:
      return 4;
    case FcitxKey_Alt_L:
    case FcitxKey_Alt_R:
      return 8;
    case FcitxKey_Super_L:
    case FcitxKey_Super_R:
      return 64;
    case FcitxKey_ISO_Level3_Shift:
      return 128;
    default:
      return 0;
    }
  }
  static bool matches(unsigned index, fcitx::KeySym sym) {
    if (index >= ctShortcutNames.size())
      return false;
    const auto name = ctShortcutNames[index];
    if (name == "None")
      return false;
    if (name == "Shift")
      return sym == FcitxKey_Shift_L || sym == FcitxKey_Shift_R;
    if (name == "Control")
      return sym == FcitxKey_Control_L || sym == FcitxKey_Control_R;
    if (name == "Alt")
      return sym == FcitxKey_Alt_L || sym == FcitxKey_Alt_R;
    if (name == "Super")
      return sym == FcitxKey_Super_L || sym == FcitxKey_Super_R;
    return fcitx::Key::keySymFromString(std::string(name)) == sym;
  }
  static Action resolve(fcitx::KeySym sym, unsigned modifiers, unsigned mode,
                        unsigned internal, unsigned speech, bool enabled,
                        unsigned unicode = 12, unsigned translate = 12,
                        bool translateEnabled = false) {
    if (modifiers & ((1 | 4 | 8 | 64 | 128) & ~mask(sym)))
      return Action::None;
    for (const auto &[key, action] : std::array<std::pair<unsigned, Action>, 5>{
             {{mode, Action::Mode},
              {internal, Action::InternalCode},
              {speech, enabled ? Action::Voice : Action::None},
              {unicode, Action::Unicode},
              {translate, translateEnabled ? Action::Translate : Action::None}}})
      if (matches(key, sym))
        return action;
    return Action::None;
  }
  static bool matches(std::string_view shortcut, fcitx::KeySym sym,
                      unsigned modifiers) {
    if(shortcut.empty() || shortcut=="None") return false;
    if(shortcut=="Shift" || shortcut=="Control" || shortcut=="Alt" || shortcut=="Super")
      return matches(static_cast<unsigned>(std::find(ctShortcutNames.begin(),ctShortcutNames.end(),shortcut)-ctShortcutNames.begin()),sym)
          && !(modifiers & ((1|4|8|64|128)&~mask(sym)));
    const auto current=fcitx::Key(sym,fcitx::KeyStates(modifiers&(1|4|8|64|128))).normalize();
    const fcitx::Key configured{std::string(shortcut)};
    return configured.isValid() && current.check(configured.normalize());
  }
  static Action resolveSpecs(fcitx::KeySym sym,unsigned modifiers,
      const std::array<std::string_view,6> &shortcuts,bool voiceEnabled,
      bool translateEnabled,bool traditionalEnabled) {
    const std::array<std::pair<std::string_view,Action>,6> actions{{
      {shortcuts[0],Action::Mode},{shortcuts[1],Action::InternalCode},
      {voiceEnabled?shortcuts[2]:std::string_view{},voiceEnabled?Action::Voice:Action::None},
      {shortcuts[3],Action::Unicode},
      {translateEnabled?shortcuts[4]:std::string_view{},translateEnabled?Action::Translate:Action::None},
      {traditionalEnabled?shortcuts[5]:std::string_view{},traditionalEnabled?Action::Traditional:Action::None}
    }};
    for(const auto &[shortcut,action]:actions)
      if(action!=Action::None && matches(shortcut,sym,modifiers)) return action;
    return Action::None;
  }
  struct Result {
    Action trigger = Action::None;
    bool accept = false, handled = false, armHold = false, interrupted = false,
         stopVoice = false, cancelVoice = false;
  };
  class Gesture {
    fcitx::KeySym key_ = FcitxKey_None;
    Action action_ = Action::None;
    uint64_t time_ = 0;
    bool chord_ = false, held_ = false, started_ = false;

  public:
    void reset() {
      key_ = FcitxKey_None;
      action_ = Action::None;
      time_ = 0;
      chord_ = held_ = started_ = false;
    }
    void voiceStarted(bool started) { started_ = started; }
    bool canHold() const {
      return key_ != FcitxKey_None && mask(key_) && action_ == Action::Voice &&
             !chord_ && !held_;
    }
    void held() { held_ = true; }
    Result handle(fcitx::KeySym sym, unsigned modifiers, bool release,
                  uint64_t now, Action action, bool holdVoice) {
      Result result;
      if (release) {
        if (sym != key_)
          return result;
        result.handled = true;
        result.accept = !mask(sym);
        const auto duration = now - time_;
        if (!chord_ && !(modifiers & ((1 | 4 | 8 | 64 | 128) & ~mask(sym)))) {
          if (mask(sym) && !held_ &&
              duration <
                  (action_ == Action::Voice && holdVoice ? 300000u : 500000u))
            result.trigger = action_;
          result.stopVoice = action_ == Action::Voice && started_ &&
                             (held_ || (!mask(sym) && duration >= 300000));
        }
        reset();
        return result;
      }
      if (sym == key_) {
        result.handled = true;
        result.accept = !mask(sym);
        return result;
      }
      if (key_ != FcitxKey_None) {
        result.interrupted = true;
        if (mask(key_)) {
          chord_ = true;
          result.cancelVoice = started_;
          started_ = false;
          return result;
        }
        reset();
      }
      if (action == Action::None)
        return result;
      key_ = sym;
      action_ = action;
      time_ = now;
      chord_ = held_ = started_ = false;
      result.handled = true;
      result.accept = !mask(sym);
      if (!mask(sym))
        result.trigger = action;
      else
        result.armHold = action == Action::Voice && holdVoice;
      return result;
    }
  };
};
