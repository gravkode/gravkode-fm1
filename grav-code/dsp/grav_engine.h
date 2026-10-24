#pragma once
#include <stdint.h>
#include "../synth/synth.h"

typedef struct {
    int32_t last_note;                   /* the note played last: it sounds on through the release */
    float level;                         /* the envelope's level, at the last sample */
    float gain;                          /* MASTER, glided */
    float velocity;                      /* the last note's velocity, 0..1 */
    uint32_t strikes_seen;               /* the voice's strikes count at the last render */
} grav_engine_t;

void grav_engine_init(grav_engine_t *e);
/* n stereo frames of the voice (its note, velocity, strikes and envelope) on engine (GRAV_ENGINE_*), 24-bit */
void grav_engine_render(
    grav_engine_t *e,
    int32_t *lr,
    uint32_t n,
    synth_t *voice,
    uint32_t engine,
    uint32_t master_q12,
    float timbre,
    float morph,
    float harmonics,
    float pitch_offset
);
