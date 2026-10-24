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
    GRAV_ENGINE_COUNT
};

void grav_plaits_init(void);
/* size samples (any number: longer runs are rendered GRAV_PLAITS_BLOCK at a time) of engine at a MIDI note (a
 * fraction for the LFO's pitch), the parameters 0..1 */
void grav_plaits_render(float note, float *out, float *aux, uint32_t size, uint32_t engine,
                        float timbre, float morph, float harmonics);

#ifdef __cplusplus
}
#endif
