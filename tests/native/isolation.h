// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <fcitx-utils/standardpaths.h>
#include <cstdlib>
#include <iostream>
#include <string>
inline bool isolatedPaths() {
    const char *root = std::getenv("COWTYPE_TEST_ROOT");
    if (!root) return false;
    for (auto type : {fcitx::StandardPathsType::Data, fcitx::StandardPathsType::Config,
                      fcitx::StandardPathsType::PkgData, fcitx::StandardPathsType::PkgConfig,
                      fcitx::StandardPathsType::Addon}) {
        for (const auto &path : fcitx::StandardPaths::global().directories(type)) {
            if (!path.lexically_normal().string().starts_with(std::string(root) + "/")) {
                std::cerr << "Refusing non-isolated Fcitx path: " << path << '\n';
                return false;
            }
        }
    }
    return true;
}
