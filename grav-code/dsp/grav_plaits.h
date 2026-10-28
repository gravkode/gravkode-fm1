#pragma once
/* Mutable Instruments Plaits' engines (grav_plaits.cpp), for the C side */
#include <stdint.h>
#include "../sound/controls.h"

#ifdef __cplusplus
extern "C" {
#endif

#define GRAV_PLAITS_BLOCK 24u           /* the caller's chunk (the engines run 12 at a time, as on Plaits) */

enum {                                   /* the engines, by the settings' engine_id: the first four as 0.10 saved
                                          * them, the others after (ALGO steps through them in Plaits' order) */
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
    GRAV_ENGINE_COUNT,                   /* the 24 engines of Plaits' any voice takes */
    GRAV_ENGINE_SYN_KICK = GRAV_ENGINE_COUNT,   /* 0.19: a drum voice's alone, Plaits' second models (its AUX out) */
    GRAV_ENGINE_SYN_SNARE,
    GRAV_ENGINE_RING_HAT,
    GRAV_ENGINE_ALL
};

#define GRAV_VOICES 8u                  /* engines run side by side: one per voice (synth/synth.h SYNTH_VOICES_MAX) */
#define GRAV_BIG_VOICES 4u              /* voices 0..3 take any engine (the synth tracks'); 4..7 a drum (KICK,
                                         * SNARE, HI-HAT, and their second models: the drum tracks'), in small
                                         * slots of their own, with knobs of their own (dsp/grav_drums.h) */

typedef struct {
    float note;                          /* a MIDI note; a fraction for the LFO's pitch */
    float timbre, morph, harmonics;      /* 0..1 */
    float accent;                        /* 0..1, the note's velocity: how hard an engine that strikes strikes */
    uint8_t gate;                        /* the voice's note is held */
    uint8_t strike;                      /* a new note began on it */
    float shape[3];                      /* a drum voice's SHAPE knobs, 0..1 (dsp/grav_drums.h) */
    const grav_controls_t *controls;    /* optional per-track advanced settings; NULL keeps original Plaits */
} grav_plaits_params_t;

void grav_plaits_init(void);
/* the engine a voice plays (GRAV_ENGINE_*): a change builds it anew in that voice, as Plaits starts an engine it
 * switches to afresh. Each voice its own (a track's engine in its voices); grav_plaits_engine: every voice the same */
void grav_plaits_voice_engine(uint32_t voice, uint32_t engine);
void grav_plaits_engine(uint32_t engine);
uint32_t grav_plaits_voice_engine_id(uint32_t voice);   /* the engine built in it (GRAV_ENGINE_ALL: none) */
/* At zero outer amplitude, discard a six-op voice's held gate/FIFO before it sleeps. Joined worker only. */
void grav_plaits_reset_voice(uint32_t voice);
/* size samples of voice (12 or more, so that a strike is never lost: GRAV_PLAITS_BLOCK chunks), the engine's
 * 12-sample blocks through the voice's FIFO; 1 when the engine shaped them with an envelope of its own (a drum, the
 * a spoken word: GRAV CODE's envelope leaves them, as Plaits' low-pass gate does). The six-op FM banks keep
 * their DX operator envelopes but return 0 so the app's ENV also controls their final amplitude. */
int grav_plaits_render(uint32_t voice, const grav_plaits_params_t *p, float *out, float *aux, uint32_t size);

const char *grav_engine_name(uint32_t engine);           /* on the screen: 10 characters at most */
const char *grav_engine_knob(uint32_t engine, uint32_t k);   /* K1..K3 (k 0..2): what each does, 10 at most */
int grav_engine_is_drum(uint32_t engine);                /* KICK, SNARE, HI-HAT and their second models */
int grav_drum_kind(uint32_t engine);                     /* 0 a kick, 1 a snare, 2 a hi-hat; -1 no drum */
uint32_t grav_drum_step(uint32_t engine, int32_t steps); /* ALGO on a drum track: among the six, round */
/* a drum's SHAPE knobs (0..1) as Plaits folds them on HARMONICS: the kick's PUNCH, SWEEP and DRIVE from it; the
 * snare's SNAPPY, the hi-hat's NOISE it, DRIVE none, LEVEL full */
void grav_drum_shape(uint32_t engine, float harmonics, float shape[3]);
uint32_t grav_engine_step(uint32_t engine, int32_t steps);   /* ALGO: steps away in Plaits' order, round */
/* (0.27) the i-th engine in ALGO's order (a drum track's: of its six), and an engine's place in it */
uint32_t grav_engine_at(int drum, uint32_t i);
uint32_t grav_engine_rank(int drum, uint32_t engine);
uint32_t grav_plaits_ram_used(uint32_t voice);   /* bytes of a voice's RAM its engine's Init took */
void grav_fm_patch(uint32_t engine, uint32_t harmonics, uint8_t raw[GRAV_CONTROL_BYTES]);

#ifdef __cplusplus
}
#endif
