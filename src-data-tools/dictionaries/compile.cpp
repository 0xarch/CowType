// SPDX-License-Identifier: GPL-3.0-or-later
// Offline compiler: source files must already be installed or explicitly
// prepared.
#include "dictionary.h"
#include <iostream>
int main(int argc, char **argv) {
  if (argc != 5) {
    std::cerr << "Usage: cowtype-dictionary-compiler ICE_TSV "
                 "WANXIANG_TSV_OR_EMPTY SYSTEM_DICT_OR_EMPTY OUTPUT_DIR\n";
    return 2;
  }
  try {
    cowtype::PinyinSources sources{argv[1], argv[2], argv[3], argv[4], ""};
    const auto key = cowtype::sourceKey(sources);
    auto result = cowtype::compilePinyin(sources);
    if (cowtype::sourceKey(sources) != key ||
        !cowtype::writeCache(sources.cache, key, result))
      throw std::runtime_error("Compiled dictionary could not be saved");
    std::cout << "Compiled dictionary: " << key
              << ".ctdict; warnings=" << result.warnings << '\n';
    return result.warnings ? 1 : 0;
  } catch (const std::exception &error) {
    std::cerr << "Dictionary compilation failed: " << error.what() << '\n';
    return 1;
  }
}
