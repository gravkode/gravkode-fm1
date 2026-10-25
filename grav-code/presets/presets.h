#pragma once
#include <stdint.h>

#define PRESET_FACTORY 4u
#define PRESET_USERS 64u
#define PRESET_CUSTOM (-1)

/* A sound, independent of track/voice allocation, ENV, LFO, FX and patterns. Drum and synth versions of an
 * engine have different controls: keep their presets separate. Names are at most ten ASCII characters. */
typedef struct {
    uint8_t engine, osc[3], tune, shape[3];
    char name[11];
    uint8_t drum;
} preset_t;

typedef struct {
    uint32_t magic;
    uint16_t version, reserved;
    uint32_t used[2];
    preset_t slot[PRESET_USERS];
} preset_bank_t;

void presets_init(preset_bank_t *b);
int presets_valid(const preset_bank_t *b);
int preset_used(const preset_bank_t *b, uint32_t slot);
int preset_put(preset_bank_t *b, uint32_t slot, const preset_t *p);
/* IDs: 0..3 factory, 4..67 user slots, -1 custom. Only matching engine AND track kind are browsed. */
int preset_get(const preset_bank_t *b, uint32_t engine, int drum, int id, preset_t *p);
int preset_next(const preset_bank_t *b, uint32_t engine, int drum, int id, int32_t delta);
int preset_match(const preset_bank_t *b, const preset_t *p);
