/* vkey_engine.h - generic C ABI for the portable Vietnamese input engine.
 *
 * Closed-source boundary: consumers link the prebuilt vkey_engine library
 * (DLL / .so) and include this header; the Rust source is not distributed.
 * All text crosses as UTF-16 to match Windows wchar_t.
 *
 * Lifetime: vkey_engine_create() returns an owning handle; release it once
 * with vkey_engine_destroy(). Every other call borrows the handle.
 */
#ifndef VKEY_ENGINE_H
#define VKEY_ENGINE_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Bump when the ABI changes; check against vkey_engine_abi_version(). */
#define VKEY_ENGINE_ABI_VERSION 2u

/* Input methods (the `method` argument to vkey_engine_create). */
#define VKEY_METHOD_TELEX        0u
#define VKEY_METHOD_VNI          1u
#define VKEY_METHOD_SIMPLE_TELEX 2u
#define VKEY_METHOD_COMBINED     3u

/* Feature flags (OR together into the `features` argument). */
#define VKEY_FEAT_MODERN_ORTHOGRAPHY   (1u << 0)
#define VKEY_FEAT_QUICK_START_CONSONANT (1u << 1)
#define VKEY_FEAT_QUICK_CONSONANT      (1u << 2)
#define VKEY_FEAT_QUICK_END_CONSONANT  (1u << 3)
#define VKEY_FEAT_SPELL_CHECK          (1u << 4)
#define VKEY_FEAT_ALLOW_ENGLISH_BYPASS (1u << 5)
#define VKEY_FEAT_ALLOW_ZWJF           (1u << 6)

/* Opaque engine handle. */
typedef struct VKeyEngine VKeyEngine;

/* Create an engine for `method` with the `features` bitmask. */
VKeyEngine *vkey_engine_create(uint32_t method, uint32_t features);

/* Destroy a handle from vkey_engine_create(). NULL is ignored. */
void vkey_engine_destroy(VKeyEngine *engine);

/* Clear the active composition. */
void vkey_engine_reset(VKeyEngine *engine);

/* Push one decoded key (a Unicode scalar value, e.g. an ASCII letter). */
void vkey_engine_push_char(VKeyEngine *engine, uint32_t codepoint);

/* Remove one rendered symbol from the composition. */
void vkey_engine_backspace(VKeyEngine *engine);

/* Copy the rendered composition into `buf` as UTF-16 (up to `cap` code units)
 * and return the total UTF-16 length needed. Pass buf=NULL, cap=0 to query the
 * length. The active composition is bounded, so a 256-unit buffer always fits. */
size_t vkey_engine_peek_utf16(const VKeyEngine *engine, uint16_t *buf, size_t cap);

/* Commit the composition, reset the engine, and copy the committed text into
 * `buf` as UTF-16; returns the committed UTF-16 length. */
size_t vkey_engine_commit_utf16(VKeyEngine *engine, uint16_t *buf, size_t cap);

/* Number of Unicode scalars in the current rendered composition. */
size_t vkey_engine_count(const VKeyEngine *engine);

/* ABI revision implemented by this library. */
uint32_t vkey_engine_abi_version(void);

/* --- ABI v2: host query surface --------------------------------------------
 * Added in ABI 2. Present only when vkey_engine_abi_version() >= 2. */

/* Whether the current rendered composition is flagged as a non-Vietnamese
 * (English) shape by the engine's English-protection heuristic. */
bool vkey_engine_is_english_word(const VKeyEngine *engine);

/* Whether the active composition carries a tone/modifier escape (a repeated
 * tone/modifier key forced literal, or a cancelled tone). */
bool vkey_engine_is_tone_escaped(const VKeyEngine *engine);

/* Whether a quick-consonant expansion (e.g. nn->ng, cc->ch) is present in the
 * active composition. */
bool vkey_engine_has_active_quick_consonant(const VKeyEngine *engine);

/* Copy the case-preserved physical input scalars of the active composition into
 * `buf` as UTF-16 (up to `cap` code units); returns the total UTF-16 length.
 * Pass buf=NULL, cap=0 to query the length. */
size_t vkey_engine_peek_raw_utf16(const VKeyEngine *engine, uint16_t *buf, size_t cap);

/* Seed the active composition from already-rendered UTF-16 text, treating every
 * scalar as literal input (for backspace-revive into a committed word). Returns
 * true on success, false (leaving the engine reset) on invalid UTF-16 or
 * capacity overflow. Re-applying a tone/modifier to seeded glyphs is NOT
 * faithfully supported — that needs a raw snapshot captured at commit time. */
bool vkey_engine_seed_text_utf16(VKeyEngine *engine, const uint16_t *buf, size_t len);

#ifdef __cplusplus
} /* extern "C" */
#endif

#endif /* VKEY_ENGINE_H */
