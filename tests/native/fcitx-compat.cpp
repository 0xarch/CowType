// SPDX-License-Identifier: GPL-3.0-or-later
#include "../../src-fcitx5/compat/strings.h"
#include <cassert>
#include <iostream>
#include <vector>

int main() {
    const std::vector<std::string> fields = {
        "plain", "two words", "quote\"slash\\", "line\nfeed", "", "测试"};
    std::string line = " \t";
    for (const auto &field : fields)
        line += (field.empty() ? "\"\"" : fcitx::stringutils::escapeForValue(field)) + " \t";
    for (auto consume : {cowtype::compat::consumeValue,
                         cowtype::compat::consumeLegacyValue}) {
        std::string_view remaining(line);
        for (const auto &expected : fields) {
            std::string actual;
            assert(!consume(remaining, &actual).empty());
            assert(actual == expected);
        }
        assert(consume(remaining, nullptr).empty());
        assert(remaining.empty());
    }
    std::cout << "Fcitx dictionary escape compatibility passed\n";
}
