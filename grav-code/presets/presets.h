#pragma once
#include <stdint.h>
#include "../sound/controls.h"

#define PRESET_FACTORY 4u
#define PRESET_USERS 64u
#define PRESET_CUSTOM (-1)

/* A sound, independent of track/voice allocation, ENV, LFO, FX and patterns. Drum and synth versions of an
 * engine have different controls: keep their presets separate. Names are at most ten ASCII characters. */
typedef struct {
    uint8_t engine, osc[3], tune, shape[3];
    char name[11];
    uint8_t drum;
    grav_controls_t controls;
} preset_t;

#define PRESET_LOG_RECORDS 120u
#define PRESET_PENDING 8u
typedef struct {
    int16_t live[PRESET_USERS];
    uint8_t state[PRESET_LOG_RECORDS];
    uint32_t seq, next;
    uint8_t n_pending, pending_slot[PRESET_PENDING], ok;
    preset_t pending[PRESET_PENDING];
} preset_bank_t;

#ifdef __cplusplus
extern "C" {
#endif
void presets_init(preset_bank_t *b);
int presets_valid(const preset_bank_t *b);
int preset_valid(const preset_t *p);
const preset_t *preset_user(const preset_bank_t *b, uint32_t slot);
int preset_used(const preset_bank_t *b, uint32_t slot);
int preset_put(preset_bank_t *b, uint32_t slot, const preset_t *p);
/* IDs: 0..3 factory, 4..67 user slots, -1 custom. Only matching engine AND track kind are browsed. */
int preset_get(const preset_bank_t *b, uint32_t engine, int drum, int id, preset_t *p);
int preset_next(const preset_bank_t *b, uint32_t engine, int drum, int id, int32_t delta);
int preset_match(const preset_bank_t *b, const preset_t *p);
#ifdef __cplusplus
}
#endif
