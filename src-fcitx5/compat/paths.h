// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <filesystem>
#include <vector>

#ifndef COWTYPE_FCITX_USE_STANDARD_PATHS
#error "Link cowtype-fcitx-compat to select the Fcitx5 API version"
#endif

#if COWTYPE_FCITX_USE_STANDARD_PATHS
#include <fcitx-utils/standardpaths.h>
#else
#include <fcitx-utils/standardpath.h>
#endif

namespace cowtype::compat {
#if COWTYPE_FCITX_USE_STANDARD_PATHS
using PathType = fcitx::StandardPathsType;
#else
using PathType = fcitx::StandardPath::Type;
#endif

// Return the same value type and search order on both sides of Fcitx5 5.1.19.
inline std::vector<std::filesystem::path> directories(PathType type) {
#if COWTYPE_FCITX_USE_STANDARD_PATHS
    const auto paths = fcitx::StandardPaths::global().directories(type);
#else
    const auto paths = fcitx::StandardPath::global().directories(type);
#endif
    return {paths.begin(), paths.end()};
}
} // namespace cowtype::compat
