// SPDX-License-Identifier: GPL-3.0-or-later
// Explicit public-data benchmark; never reads personal configuration or
// history.
#include "libime.h"
#include <chrono>
#include <iostream>
#include <sys/resource.h>
static void emit(void *out, const char *, const char *, size_t, uint32_t) {
  ++*static_cast<size_t *>(out);
}
int main(int argc, char **argv) {
  if (argc != 6) {
    std::cerr
        << "Usage: cowtype-dictionary-benchmark ICE_TSV WANXIANG_TSV_OR_EMPTY "
           "SYSTEM_DICT_OR_EMPTY CACHE_DIR MODEL_0_OR_1\n";
    return 2;
  }
  try {
    const auto start = std::chrono::steady_clock::now();
    LibimeDecoder decoder(
        cowtype::PinyinSources{argv[1], argv[2], argv[3], argv[4], ""},
        std::string(argv[5]) == "1");
    const auto loaded = std::chrono::steady_clock::now();
    size_t candidates = 0;
    for (const auto *raw : {"jieshu", "yinggai", "moban", "moyang", "fajiao",
                            "hang", "a", "wozaixuexi"})
      decoder.queryImpl(raw, 13, emit, &candidates);
    struct rusage usage{};
    getrusage(RUSAGE_SELF, &usage);
    std::cout << "load_ms="
              << std::chrono::duration_cast<std::chrono::milliseconds>(loaded -
                                                                       start)
                     .count()
              << " peak_kib=" << usage.ru_maxrss
              << " cache=" << decoder.cacheHit
              << " model=" << decoder.modelLoaded
              << " warnings=" << decoder.warnings
              << " candidates=" << candidates << '\n';
  } catch (const std::exception &error) {
    std::cerr << error.what() << '\n';
    return 1;
  }
}
