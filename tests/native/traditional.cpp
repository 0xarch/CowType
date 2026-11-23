// SPDX-License-Identifier: GPL-3.0-or-later
#include "../../src-fcitx5/engine/traditional.h"
#include <cassert>
#include <iostream>

int main() {
    cowtype::TraditionalConverter converter;
    if (!converter.available()) {
        std::cout << "OpenCC runtime/configuration unavailable; optional feature skipped\n";
        return 0;
    }
    assert(converter.convert("汉语", true) == "漢語");
    assert(converter.convert("繁體中文", false) == "繁体中文");
    assert(converter.convert("", true).empty());
    std::cout << "Optional OpenCC simplified/traditional conversion verified\n";
}
