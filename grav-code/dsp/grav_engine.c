#include "grav_engine.h"
#include "grav_plaits.h"

/* a sample of 1.0 from an engine, at full MASTER: just under the 24-bit full scale (2^23), the rest the headroom
 * left for an engine's peaks above 1.0 */
#define GRAV_OUT_FULL 8300000.0f
#define GRAV_VOICE_GAIN 0.25f            /* the voice at velocity 127, of that */

void grav_engine_init(grav_engine_t *e)
{
    grav_plaits_init();
    e->last_note = 69;
    e->level = 0.0f;
    e->gain = 0.0f;
    e->velocity = 1.0f;
    e->strikes_seen = 0;
}

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
) {
    int32_t note = voice->note;

    if (note >= 0) {
        e->last_note = note;
        e->velocity = voice->velocity;
    }

    float voice_gain = GRAV_VOICE_GAIN * e->velocity;
    float gain = (float)master_q12 / 4096.0f * GRAV_OUT_FULL;

    grav_plaits_params_t p;
    p.engine = engine;
    p.note = (float)e->last_note + pitch_offset;
    p.timbre = timbre;
    p.morph = morph;
    p.harmonics = harmonics;
    p.accent = e->velocity;
    p.gate = note >= 0;
    p.strike = voice->strikes != e->strikes_seen;   /* a note began since the last block */
    e->strikes_seen = voice->strikes;

    float plaits_out[GRAV_PLAITS_BLOCK];
    float plaits_aux[GRAV_PLAITS_BLOCK];

    for (uint32_t base = 0; base < n; base += GRAV_PLAITS_BLOCK) {
        uint32_t count = n - base;
        if (count > GRAV_PLAITS_BLOCK) count = GRAV_PLAITS_BLOCK;

        /* an engine with an envelope of its own (a drum, the 6-op FM, a word) plays through, as Plaits' low-pass
         * gate lets it; the envelope runs on all the same, ready for the next engine */
        int own_envelope = grav_plaits_render(&p, plaits_out, plaits_aux, count);
        p.strike = 0;

        for (uint32_t j = 0; j < count; j++) {
            uint32_t i = base + j;
            e->level = envelope_process(&voice->amp_env);
            e->gain += (gain - e->gain) * 0.004f;   /* MASTER glides: no step in the sound */

            float level = own_envelope ? 1.0f : e->level;
            float s = plaits_out[j] * (level * voice_gain * e->gain);
            if (s > 8388607.0f) s = 8388607.0f;
            if (s < -8388608.0f) s = -8388608.0f;

            lr[2 * i] = (int32_t)s;
            lr[2 * i + 1] = (int32_t)s;
        }
    }
}
