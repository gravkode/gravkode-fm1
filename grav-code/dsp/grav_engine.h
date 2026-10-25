#pragma once
#include <stdint.h>
#include "../synth/synth.h"

typedef struct {
    float gain;                          /* MASTER, glided */
    uint32_t strikes_seen[SYNTH_VOICES_MAX];   /* each voice's strikes count at the last render */
    uint8_t asleep[SYNTH_VOICES_MAX];    /* quiet and let go: not rendered until its next note (the CPU, the battery) */
} grav_engine_t;

void grav_engine_init(grav_engine_t *e);
/* n stereo frames, 24-bit: the synth's voices (their notes, velocities, strikes and envelopes) on engine
 * (GRAV_ENGINE_*), mixed */
void grav_engine_render(
    grav_engine_t *e,
    int32_t *lr,
    uint32_t n,
    synth_t *synth,
    uint32_t engine,
    uint32_t master_q12,
    float timbre,
    float morph,
    float harmonics,
    float pitch_offset
);
