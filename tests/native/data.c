/* SPDX-License-Identifier: GPL-3.0-or-later */
/* Optional manual real-data acceptance: paths must be freshly prepared public data. */
#include "cowtype2.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>
static void query(Cowtype2Engine *engine, const char *input, const char *expected) {
    assert(engine);
    Cowtype2Session *session = cowtype2_session_new(engine);
    assert(session);
    for (const char *p = input; *p; ++p) assert(cowtype2_session_key(session, (unsigned char)*p, 0, false, 0xffffffff));
    int found = 0;
    do {
        for (size_t i = 0; i < cowtype2_session_count(session); ++i)
            if (!strcmp(cowtype2_session_word(session, i), expected)) found = 1;
        if (!cowtype2_session_has_page(session, true)) break;
        assert(cowtype2_session_key(session, CT2_PAGE_DOWN, 0, false, 0xffffffff));
    } while (!found);
    assert(found);
    cowtype2_session_free(session);
    cowtype2_engine_free(engine);
}
int main(int argc, char **argv) {
    assert(argc == 4);
    query(cowtype2_engine_new(argv[1], "", 0), "nihao", "你好");
    query(cowtype2_engine_new(argv[2], "", 1), "a", "工");
    query(cowtype2_engine_new(argv[1], argv[3], 0), "nihao", "你好");
    puts("Real Ice, Wubi and optional Wanxiang ABI queries passed");
}
