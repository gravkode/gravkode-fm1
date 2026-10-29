#pragma once
#include <stdint.h>
#include "../sound/controls.h"

#define PRESET_FACTORY 4u
#define PRESET_TITLE 17u                 /* (0.27) a factory sound's name: up to 16 (a user's record keeps its 10) */
enum { PRESET_CAT_SYNTH, PRESET_CAT_BASS, PRESET_CAT_LEAD, PRESET_CAT_PAD, PRESET_CAT_PLUCK, PRESET_CAT_KEYS,
       PRESET_CAT_DRUM, PRESET_CAT_FX, PRESET_CAT_USER, PRESET_CATEGORIES };
#define PRESET_USERS 64u
#define PRESET_CUSTOM (-1)

/* A sound, independent of track/voice allocation, FX and patterns; since 0.37 with its LFO and envelope (a synth
 * track's MOD and ENV pages: mod_set PRESET_MOD; a sound saved before has none, and leaves the track's as they are). Drum and synth versions of an
 * engine have different controls: keep their presets separate. Names are at most ten ASCII characters. */
typedef struct {
    uint8_t engine, osc[3], tune, shape[3];
    char name[11];
    uint8_t drum;
    grav_controls_t controls;
    uint8_t mod_set;                     /* (0.37) PRESET_MOD: lfo[] and env[] are its (a sound saved before: 0xFF) */
    uint8_t lfo[3];                      /* LFO RATE, LFO DEPTH, LFO DEST (0..100, as MOD's knobs) */
    uint8_t env[3];                      /* ATTACK, DECAY, RELEASE (0..100) */
    uint8_t mod_rsv;
} preset_t;
#define PRESET_MOD 0xA5u
#define PRESET_SIZE_036 184u             /* a sound as 0.22..0.36 saved it: up to mod_set (no LFO, no envelope) */
#define PRESET_LFO_DEFAULT {50, 0, 0}
#define PRESET_ENV_DEFAULT {10, 50, 30}

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
/* (0.27) a sound's whole name (a factory one's up to 16), 0 if there is none; its kind, for the browser (a user's:
 * USER), and the kinds' names */
const char *preset_title(const preset_bank_t *b, uint32_t engine, int drum, int id);
uint32_t preset_category(uint32_t engine, int id, const char *name);
extern const char *const PRESET_CATEGORY[PRESET_CATEGORIES];
int preset_match(const preset_bank_t *b, const preset_t *p);
#ifdef __cplusplus
}
#endif
