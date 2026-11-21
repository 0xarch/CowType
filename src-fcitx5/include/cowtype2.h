/* SPDX-License-Identifier: GPL-3.0-or-later */
#ifndef COWTYPE2_H
#define COWTYPE2_H
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#ifdef __cplusplus
extern "C" {
#endif
/* ABI 2: no old cowtype_* symbols/layouts. All hosts must check major == 2.
 * Opaque owning handles; null free/getters are safe, other invalid pointers are
 * forbidden. One thread per session. Engines may be handed off after loading.
 * Sessions own Arc references and survive engine-handle release.
 * Inputs are copied, UTF-8 NUL terminated. Returned strings are borrowed until
 * the next mutation/free of that SAME handle; getters do not invalidate them.
 * Each mutating call replaces the event frame. Consume commit exactly once.
 */
typedef struct EngineHandle Cowtype2Engine;
typedef struct SessionHandle Cowtype2Session;
enum Cowtype2Key {
 CT2_SPACE=0x110001, CT2_ENTER, CT2_ESCAPE, CT2_BACKSPACE, CT2_DELETE,
 CT2_LEFT, CT2_RIGHT, CT2_HOME, CT2_END, CT2_PREVIOUS, CT2_NEXT,
 CT2_PAGE_UP, CT2_PAGE_DOWN, CT2_TOGGLE
};
uint32_t cowtype2_abi_version(void);
const char *cowtype2_last_error(void); /* thread-local, copy immediately on failure */
/* scheme: 0 standalone pinyin index, 1 Wubi, 2 validated external pinyin.
 * Scheme 2 requires cowtype2_engine_set_decoder before publishing to sessions. */
Cowtype2Engine *cowtype2_engine_new(const char *path, const char *optional, uint32_t scheme);
void cowtype2_engine_free(Cowtype2Engine *);
const char *cowtype2_engine_warning(const Cowtype2Engine *);
Cowtype2Session *cowtype2_session_new(const Cowtype2Engine *);
void cowtype2_session_free(Cowtype2Session *);
void cowtype2_session_set_engine(Cowtype2Session *, const Cowtype2Engine *);
/* Modifiers: Shift=1 Ctrl=4 Alt=8 Super=64; lock bits ignored. previous is a
 * trusted Unicode scalar, or UINT32_MAX if surrounding text is unavailable. */
bool cowtype2_session_key(Cowtype2Session *, uint32_t code, uint32_t modifiers, bool release, uint32_t previous);
bool cowtype2_session_select(Cowtype2Session *, size_t visible_index);
void cowtype2_session_reset(Cowtype2Session *, bool commit);
const char *cowtype2_session_preedit(const Cowtype2Session *);
const char *cowtype2_session_commit(const Cowtype2Session *);
size_t cowtype2_session_cursor(const Cowtype2Session *); /* UTF-8 bytes */
size_t cowtype2_session_count(const Cowtype2Session *);
const char *cowtype2_session_word(const Cowtype2Session *, size_t);
const char *cowtype2_session_reading(const Cowtype2Session *, size_t);
int32_t cowtype2_session_candidate_cursor(const Cowtype2Session *);
bool cowtype2_session_has_page(const Cowtype2Session *, bool next);
uint32_t cowtype2_session_mode(const Cowtype2Session *);
typedef struct RuntimeHandle Cowtype2Runtime;
Cowtype2Runtime *cowtype2_runtime_new(void);
void cowtype2_runtime_free(Cowtype2Runtime *);
bool cowtype2_runtime_reload(Cowtype2Runtime *);
/* options: 0 page size; 1 horizontal; 2 Shift tap; 3 voice enabled;
 * 4 resident model; 5 voice key index; 6 modifier hold; 7 show pinyin;
 * 8 always resident; 9 bracket paging; 10 minus/equal paging;
 * 11 remember language mode (effective only with Shift tap enabled) */
uint32_t cowtype2_runtime_option(const Cowtype2Runtime *, uint32_t);
const char *cowtype2_runtime_warning(Cowtype2Runtime *);
void cowtype2_session_configure(Cowtype2Session *, const Cowtype2Runtime *);
bool cowtype2_session_forget(Cowtype2Session *);
/* Decoder callback context is owned by the engine after successful attachment.
 * Must serialize its shared state; must not throw across the C boundary. */
typedef void (*Cowtype2Emit)(void *, const char *, const char *, size_t, uint32_t);
typedef void (*Cowtype2Query)(void *, const char *, uint32_t, Cowtype2Emit, void *);
bool cowtype2_engine_set_decoder(Cowtype2Engine *, void *, Cowtype2Query, void (*)(void *));
#ifdef __cplusplus
}
#endif
#endif
