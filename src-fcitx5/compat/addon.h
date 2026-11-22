// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <fcitx/addonfactory.h>

// Some 5.1.x releases only provide the original unqualified factory export.
#ifdef FCITX_ADDON_FACTORY_V2
#define COWTYPE_ADDON_FACTORY(Class) FCITX_ADDON_FACTORY_V2(cowtype, Class)
namespace cowtype::compat {
inline constexpr const char *factorySymbol = "fcitx_addon_factory_instance_cowtype";
}
#else
#define COWTYPE_ADDON_FACTORY(Class) FCITX_ADDON_FACTORY(Class)
namespace cowtype::compat {
inline constexpr const char *factorySymbol = "fcitx_addon_factory_instance";
}
#endif
