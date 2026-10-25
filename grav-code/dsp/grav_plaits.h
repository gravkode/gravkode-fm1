#pragma once
/* Mutable Instruments Plaits' engines (grav_plaits.cpp), for the C side */
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define GRAV_PLAITS_BLOCK 24u           /* the caller's chunk (the engines run 12 at a time, as on Plaits) */

enum {                                   /* the engines, by the settings' engine_id: the first four as 0.10 saved
                                          * them, the others after (PRESET steps through them in Plaits' order) */
    GRAV_ENGINE_WAVESHAPING = 0,
    GRAV_ENGINE_FM,
    GRAV_ENGINE_VIRTUAL_ANALOG,
    GRAV_ENGINE_MODAL,
    GRAV_ENGINE_GRAIN,
    GRAV_ENGINE_ADDITIVE,
    GRAV_ENGINE_WAVETABLE,
    GRAV_ENGINE_CHORDS,
    GRAV_ENGINE_SPEECH,
    GRAV_ENGINE_SWARM,
    GRAV_ENGINE_NOISE,
    GRAV_ENGINE_PARTICLE,
    GRAV_ENGINE_STRING,
    GRAV_ENGINE_BASS_DRUM,
    GRAV_ENGINE_SNARE_DRUM,
    GRAV_ENGINE_HI_HAT,
    GRAV_ENGINE_VA_VCF,
    GRAV_ENGINE_PHASE_DISTORTION,
    GRAV_ENGINE_SIX_OP_1,
    GRAV_ENGINE_SIX_OP_2,
    GRAV_ENGINE_SIX_OP_3,
    GRAV_ENGINE_WAVE_TERRAIN,
    GRAV_ENGINE_STRING_MACHINE,
    GRAV_ENGINE_CHIPTUNE,
    GRAV_ENGINE_COUNT
};

#define GRAV_VOICES 4u                  /* engines run side by side: one per voice (synth/synth.h SYNTH_VOICES_MAX) */

typedef struct {
    float note;                          /* a MIDI note; a fraction for the LFO's pitch */
    float timbre, morph, harmonics;      /* 0..1 */
    float accent;                        /* 0..1, the note's velocity: how hard an engine that strikes strikes */
    uint8_t gate;                        /* the voice's note is held */
    uint8_t strike;                      /* a new note began on it */
} grav_plaits_params_t;

void grav_plaits_init(void);
/* the engine every voice plays (GRAV_ENGINE_*): a change builds it anew in each voice, as Plaits starts an engine
 * it switches to afresh */
void grav_plaits_engine(uint32_t engine);
/* size samples of voice (12 or more, so that a strike is never lost: GRAV_PLAITS_BLOCK chunks), the engine's
 * 12-sample blocks through the voice's FIFO; 1 when the engine shaped them with an envelope of its own (a drum, the
 * a spoken word: GRAV CODE's envelope leaves them, as Plaits' low-pass gate does). The six-op FM banks keep
 * their DX operator envelopes but return 0 so the app's ENV also controls their final amplitude. */
int grav_plaits_render(uint32_t voice, const grav_plaits_params_t *p, float *out, float *aux, uint32_t size);

const char *grav_engine_name(uint32_t engine);           /* on the screen: 10 characters at most */
uint32_t grav_engine_step(uint32_t engine, int32_t steps);   /* PRESET: steps away in Plaits' order, round */
uint32_t grav_plaits_ram_used(void);     /* bytes of its voice's RAM the engine's Init took (each voice the same) */

#ifdef __cplusplus
}
#endif
