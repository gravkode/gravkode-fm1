#pragma once
#include <stdint.h>
#include "../synth/synth.h"
#include "grav_beads.h"

typedef struct {
    uint32_t voice_us[2], fx_us, wait_us, jobs;
    uint32_t masks[2], active, steals, shed;
    uint32_t fx_core;                    /* the core the effect ran on (1: CPU1, a chunk behind the voices) */
} grav_debug_t;
extern grav_debug_t grav_debug;         /* audio-owned; UI copies with plat_lock */
extern volatile uint32_t grav_dual_core; /* UI request, sampled once at a block boundary */
extern volatile uint32_t grav_debug_reset;

typedef struct {
    float gain;                          /* MASTER, glided */
    uint32_t strikes_seen[SYNTH_VOICES_MAX];   /* each voice's strikes count at the last render */
    uint8_t asleep[SYNTH_VOICES_MAX];    /* quiet and let go: not rendered until its next note (the CPU, the battery) */
    uint32_t fade_left[SYNTH_VOICES_MAX];   /* taken by the overload protection: samples of its fade-out left */
    int32_t voice_cost, fx_cost;         /* a voice's block and the effect's, in plat_ticks24 ticks (smoothed): the
                                          * cores' split */
} grav_engine_t;

/* a track's sound: its engine and Plaits' three knobs (the LFO's share in) */
typedef struct {
    uint32_t engine;                     /* GRAV_ENGINE_* */
    float timbre, morph, harmonics;      /* 0..1 */
    float pitch;                         /* semitones on its notes (the LFO's), beside the render's pitch_offset */
} grav_sound_t;

void grav_engine_init(grav_engine_t *e);
/* n stereo frames, 24-bit: the synth's voices (their notes, velocities, strikes and envelopes), each on its track's
 * sound, mixed, through the effect (fx: grav_beads.h), then MASTER and the limiter */
void grav_engine_render_tracks(grav_engine_t *e, int32_t *lr, uint32_t n, synth_t *synth, uint32_t master_q12,
                               const grav_sound_t sound[SYNTH_TRACKS], float pitch_offset,
                               const grav_beads_params_t *fx);
/* the same with one sound for every voice */
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
    float pitch_offset,
    const grav_beads_params_t *fx
);
