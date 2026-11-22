// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "../../src-fcitx5/compat/paths.h"
#include "../../src-fcitx5/compat/addon.h"
#include <cstdlib>
#include <iostream>
#include <string>
inline bool isolatedPaths() {
    const char *root = std::getenv("COWTYPE_TEST_ROOT");
    if (!root) return false;
    using cowtype::compat::PathType;
    for (auto type : {PathType::Data, PathType::Config, PathType::PkgData,
                      PathType::PkgConfig, PathType::Addon}) {
        for (const auto &path : cowtype::compat::directories(type)) {
            if (!path.lexically_normal().string().starts_with(std::string(root) + "/")) {
                std::cerr << "Refusing non-isolated Fcitx path: " << path << '\n';
                return false;
            }
        }
    }
    return true;
}
