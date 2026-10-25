/* Mutable Instruments Beads (vendor/beads) as GRAV CODE's effect: the voices' mix through its granular processor,
 * delay, tape and reverb (grav_beads.cpp) */
#pragma once
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* QUALITY, from the cleanest: Beads' media in reverse order */
enum { GRAV_FX_DIGITAL = 0, GRAV_FX_VINTAGE, GRAV_FX_CLEAN_TAPE, GRAV_FX_DIRTY_TAPE, GRAV_FX_QUALITIES };

typedef struct {
    float density, time, size, shape;    /* 0..1, as Beads' knobs (DENSITY: none at 0.5, clocked left, random right;
                                          * SIZE at 1: the delay) */
    float pitch;                         /* 0..1, Beads' PITCH knob: -24..+24 semitones, 0 at 0.5 (grav_beads_pitch) */
    float feedback, dry_wet, reverb;     /* 0..1 */
    uint8_t quality;                     /* GRAV_FX_* */
    uint8_t freeze;                      /* the recording held: the grains play what is in it */
    uint8_t seed;                        /* SEED held: a grain as it goes down, grains while held (at DENSITY's rate) */
    uint8_t seed_auto;                   /* grains made on their own, at DENSITY's rate (Beads' SEED latched) */
} grav_beads_params_t;

void grav_beads_init(void);
/* n samples of the mono mix (in) through the effect into l and r. With DRY/WET and REVERB at 0 (and not frozen) it
 * is off: l and r are the mix, and Beads takes no time; it fades in and out (and through a change of QUALITY) */
void grav_beads_process(const grav_beads_params_t *p, const float *in, float *l, float *r, uint32_t n);
int grav_beads_on(void);                 /* running (not off) */
int grav_beads_busy(const grav_beads_params_t *p);   /* running, or to start with p: the render's choice of core */
float grav_beads_pitch(float knob);      /* PITCH's knob (0..1) in semitones, as Beads' panel reads it */

#ifdef __cplusplus
}
#endif
