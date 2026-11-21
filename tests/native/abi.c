/* SPDX-License-Identifier: GPL-3.0-or-later */
#include "cowtype2.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#define CHECK(x) do { if (!(x)) { fprintf(stderr, "ABI check failed at line %d\n", __LINE__); return 1; } } while (0)
int main(int argc, char **argv) {
    CHECK(argc == 3);
    CHECK(cowtype2_abi_version() / 1000 == 2);
    cowtype2_engine_free(NULL); cowtype2_session_free(NULL);
    CHECK(cowtype2_session_count(NULL) == 0);
    CHECK(cowtype2_session_preedit(NULL) == NULL);
    CHECK(!cowtype2_session_key(NULL, 'a', 0, false, UINT32_MAX));
    CHECK(!cowtype2_engine_new("\xff", NULL, 0));
    CHECK(strlen(cowtype2_last_error()) > 0);
    CHECK(!cowtype2_engine_new(argv[2], NULL, 0));
    CHECK(!cowtype2_engine_new(argv[1], NULL, 99));
    CHECK(!cowtype2_engine_new(argv[2], NULL, 2));
    Cowtype2Engine *external = cowtype2_engine_new(argv[1], NULL, 2);
    CHECK(external);cowtype2_engine_free(external);
    Cowtype2Engine *fallback = cowtype2_engine_new(argv[1], argv[2], 0);
    CHECK(fallback && strlen(cowtype2_engine_warning(fallback)) > 0);
    cowtype2_engine_free(fallback);
    Cowtype2Engine *engine = cowtype2_engine_new(argv[1], NULL, 0);
    CHECK(engine);
    Cowtype2Session *session = cowtype2_session_new(engine);
    CHECK(session);
    cowtype2_engine_free(engine); /* Session owns its engine reference. */
    const char *input = "shan";
    for (const char *p = input; *p; ++p) { CHECK(cowtype2_session_key(session, *p, 0, false, UINT32_MAX)); }
    CHECK(cowtype2_session_count(session) == 5);
    CHECK(!strcmp(cowtype2_session_word(session, 0), "山"));
    CHECK(cowtype2_session_word(session, SIZE_MAX) == NULL);
    CHECK(cowtype2_session_cursor(session) == 4);
    CHECK(!cowtype2_session_key(session, CT2_ENTER, 0, true, UINT32_MAX));
    CHECK(cowtype2_session_key(session, CT2_SPACE, 0, false, UINT32_MAX));
    char *saved = strdup(cowtype2_session_commit(session));
    CHECK(saved && !strcmp(saved, "山"));
    CHECK(!cowtype2_session_key(session, CT2_SPACE, 0, true, UINT32_MAX));
    CHECK(!strcmp(cowtype2_session_commit(session), ""));
    cowtype2_session_free(session);
    CHECK(!strcmp(saved, "山")); free(saved);
    puts("ABI 2 C host: ownership, nulls, bounds, UTF-8 error and release-frame lifetime passed");
    return 0;
}
