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
  assert(Shortcuts::resolve(FcitxKey_F6, 0, 5, 6, 7, true) ==
         Shortcuts::Action::Mode);
  assert(Shortcuts::resolve(FcitxKey_F7, 0, 5, 6, 7, true) ==
         Shortcuts::Action::InternalCode);
  assert(Shortcuts::resolve(FcitxKey_F7, 4, 5, 6, 7, true) ==
         Shortcuts::Action::None);
  assert(Shortcuts::resolve(FcitxKey_F8, 0, 5, 6, 7, true) ==
         Shortcuts::Action::Voice);
  assert(Shortcuts::resolve(FcitxKey_F6, 0, 12, 6, 7, false) ==
         Shortcuts::Action::None);
  // Every role uses the same keys and gesture rules, including both modifier
  // sides.
  auto index = [](std::string_view name) {
    for (size_t i = 0; i < ctShortcutNames.size(); ++i)
      if (ctShortcutNames[i] == name)
        return unsigned(i);
    assert(false);
    return 0u;
  };
  for (auto action : {Shortcuts::Action::Mode, Shortcuts::Action::InternalCode,
                      Shortcuts::Action::Voice}) {
    for (auto sym :
         {FcitxKey_Shift_L, FcitxKey_Shift_R, FcitxKey_Control_L,
          FcitxKey_Alt_L, FcitxKey_Super_R, FcitxKey_ISO_Level3_Shift}) {
      Shortcuts::Gesture gesture;
      auto down =
          gesture.handle(sym, Shortcuts::mask(sym), false, 100, action, false);
      assert(down.handled && !down.accept &&
             down.trigger == Shortcuts::Action::None);
      assert(gesture.handle(sym, 0, true, 200, action, false).trigger ==
             action);
      gesture.handle(sym, Shortcuts::mask(sym), false, 100, action, false);
      gesture.handle(FcitxKey_a, Shortcuts::mask(sym), false, 150,
                     Shortcuts::Action::None, false);
      assert(gesture.handle(sym, 0, true, 200, action, false).trigger ==
             Shortcuts::Action::None);
      gesture.handle(sym, 0, false, 100, action, false);
      assert(gesture.handle(sym, 0, true, 600100, action, false).trigger ==
             Shortcuts::Action::None);
    }
  }
  assert(Shortcuts::matches(index("Shift"), FcitxKey_Shift_L));
  assert(Shortcuts::matches(index("Shift"), FcitxKey_Shift_R));
  assert(Shortcuts::resolve(FcitxKey_Control_L, 4, index("Control"), 6, 7,
                            true) == Shortcuts::Action::Mode);
  assert(Shortcuts::resolve(FcitxKey_Control_L, 5, index("Control"), 6, 7,
                            true) == Shortcuts::Action::None);
  Shortcuts::Gesture hold;
  auto armed = hold.handle(FcitxKey_Shift_L, 1, false, 100,
                           Shortcuts::Action::Voice, true);
  assert(armed.armHold && hold.canHold());
  hold.held();
  hold.voiceStarted(true);
  assert(hold.handle(FcitxKey_Shift_L, 0, true, 400100,
                     Shortcuts::Action::Voice, true)
             .stopVoice);
  hold.handle(FcitxKey_F8, 0, false, 100, Shortcuts::Action::Voice, false);
  assert(
      hold.handle(FcitxKey_F8, 0, false, 200, Shortcuts::Action::Voice, false)
          .trigger == Shortcuts::Action::None);
  hold.voiceStarted(true);
  assert(
      hold.handle(FcitxKey_F8, 0, true, 400100, Shortcuts::Action::Voice, false)
          .stopVoice);
  std::cout << "GB18030 byte validation, 2022 mappings, prefix candidates, "
               "Unicode range and shortcut dispatch verified\n";
}
