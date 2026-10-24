#pragma once
/* Mutable Instruments Plaits' engines (grav_plaits.cpp), for the C side */
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define GRAV_PLAITS_BLOCK 24u           /* the most samples an engine renders at once (plaits::kMaxBlockSize) */

enum {                                   /* the engines, by the settings' engine_id */
    GRAV_ENGINE_WAVESHAPING = 0,
    GRAV_ENGINE_FM,
    GRAV_ENGINE_VIRTUAL_ANALOG,
    GRAV_ENGINE_MODAL,
    GRAV_ENGINE_COUNT
};

typedef struct {
    uint32_t engine;                     /* GRAV_ENGINE_* */
    float note;                          /* a MIDI note; a fraction for the LFO's pitch */
    float timbre, morph, harmonics;      /* 0..1 */
    float accent;                        /* 0..1, the note's velocity: how hard Modal strikes */
    uint8_t gate;                        /* a note is held: Modal keeps exciting its resonator, as the others sound */
    uint8_t strike;                      /* a new note began: Modal strikes its resonator first */
} grav_plaits_params_t;

void grav_plaits_init(void);
/* size samples (any number: longer runs are rendered GRAV_PLAITS_BLOCK at a time, a strike in the first) */
void grav_plaits_render(const grav_plaits_params_t *p, float *out, float *aux, uint32_t size);

#ifdef __cplusplus
}
#endif
