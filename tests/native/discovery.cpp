// SPDX-License-Identifier: GPL-3.0-or-later
// Run only with explicitly isolated FCITX_* paths pointing at the staged package.
#include <fcitx/addonmanager.h>
#include <fcitx/inputmethodengine.h>
#include <fcitx/inputmethodmanager.h>
#include <fcitx/instance.h>
#include "isolation.h"
#include <iostream>
int main() {
    if (!isolatedPaths()) return 2;
    char name[] = "cowtype-package-discovery", disabled[] = "--disable=all", enabled[] = "--enable=cowtype";
    char *args[] = {name, disabled, enabled, nullptr};
    fcitx::Instance instance(3, args);
    instance.addonManager().registerDefaultLoader(nullptr);
    instance.initialize();
    auto *addon = instance.addonManager().addon("cowtype", true);
    if (!dynamic_cast<fcitx::InputMethodEngine *>(addon)) {
        std::cerr << "Staged CowType addon discovery failed\n";
        return 1;
    }
    if (!instance.inputMethodManager().entry("cowtype") ||
        !instance.inputMethodManager().entry("cowtype-wubi")) {
        std::cerr << "Staged Pinyin/Wubi input method entries missing\n";
        return 1;
    }
    std::cout << "Staged CowType addon discovered and loaded by Fcitx5\n";
}
