#pragma once
#include "presets.h"

#define PRESET_LOG_LO 0xE5000u
#define PRESET_LOG_HI 0xED000u
enum { PRESET_FREE, PRESET_LIVE, PRESET_STALE };
#ifdef __cplusplus
extern "C" {
#endif
void preset_store_init(preset_bank_t *b); /* scan, no writes */
/* Flush one queued sound. 1 committed, 0 nothing queued, -1 failure (RAM pending and previous copy retained). */
int preset_store_flush(preset_bank_t *b);
/* Import valid 0.21 slots absent from the new log. Boot only, before audio; resumable after interruption. */
int preset_store_import_021(preset_bank_t *b, const void *data, uint32_t size);
#ifdef PLAT_HOST
void preset_store_test_fail(int fail);
#endif
#ifdef __cplusplus
}
#endif
