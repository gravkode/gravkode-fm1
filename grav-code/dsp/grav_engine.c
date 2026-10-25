#include "grav_engine.h"
#include "grav_plaits.h"
#include "fastmath.h"

/* a sample of 1.0 from an engine, at full MASTER: just under the 24-bit full scale (2^23), the rest the headroom
 * left for an engine's peaks above 1.0 */
#define GRAV_OUT_FULL 8300000.0f
#define GRAV_VOICE_GAIN 0.25f            /* a voice at velocity 127, of that */
#define GRAV_FULL_SCALE 8388607.0f
#define GRAV_LIMIT_KNEE 0.5f             /* the limiter leaves all below half the full scale (-6 dB) untouched */
#define GRAV_ASLEEP_PEAK 1e-5f           /* a voice let go and this quiet (-100 dB) for a block falls asleep */

_Static_assert(SYNTH_VOICES_MAX == GRAV_VOICES, "a Plaits engine for each of the synth's voices");

void grav_engine_init(grav_engine_t *e)
{
    grav_plaits_init();
    grav_beads_init();
    e->gain = 0.0f;
    for (uint32_t v = 0; v < SYNTH_VOICES_MAX; v++) {
        e->strikes_seen[v] = 0;
        e->asleep[v] = 1;
    }
}

/* x in full scales (1.0: 0 dBFS): linear up to the knee, then bending smoothly towards the full scale (slope 1 at
 * the knee), so that voices adding up never clip hard */
static float soft_limit(float x)
{
    float a = fm_fabsf(x);
    if (a <= GRAV_LIMIT_KNEE)
        return x;
    const float room = 1.0f - GRAV_LIMIT_KNEE;
    float over = a - GRAV_LIMIT_KNEE;
    float y = GRAV_LIMIT_KNEE + room * over / (over + room);
    return x < 0.0f ? -y : y;
}

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
) {
    const float gain = (float)master_q12 / 4096.0f * GRAV_OUT_FULL;
    grav_plaits_params_t p[SYNTH_VOICES_MAX];
    float peak[SYNTH_VOICES_MAX];
    int own_envelope[SYNTH_VOICES_MAX];

    grav_plaits_engine(engine);
    for (uint32_t v = 0; v < SYNTH_VOICES_MAX; v++) {
        synth_voice_t *voice = &synth->voice[v];
        int strike = voice->strikes != e->strikes_seen[v];   /* a note began on it since the last block */
        e->strikes_seen[v] = voice->strikes;
        if (strike)
            e->asleep[v] = 0;
        if (voice->shed || v >= synth->voices)
            e->asleep[v] = 1;
        p[v].note = (float)voice->note + pitch_offset;
        p[v].timbre = timbre;
        p[v].morph = morph;
        p[v].harmonics = harmonics;
        p[v].accent = voice->velocity;
        p[v].gate = voice->gate;
        p[v].strike = (uint8_t)strike;
        peak[v] = 0.0f;
        own_envelope[v] = 0;
    }

    float plaits_out[GRAV_PLAITS_BLOCK];
    float plaits_aux[GRAV_PLAITS_BLOCK];
    float mix[GRAV_PLAITS_BLOCK];
    float fx_l[GRAV_PLAITS_BLOCK], fx_r[GRAV_PLAITS_BLOCK];

    for (uint32_t base = 0; base < n; base += GRAV_PLAITS_BLOCK) {
        uint32_t count = n - base;
        if (count > GRAV_PLAITS_BLOCK) count = GRAV_PLAITS_BLOCK;
        for (uint32_t j = 0; j < count; j++)
            mix[j] = 0.0f;

        for (uint32_t v = 0; v < SYNTH_VOICES_MAX; v++) {
            if (e->asleep[v])
                continue;
            synth_voice_t *voice = &synth->voice[v];
            /* an engine with an envelope of its own (a drum, the 6-op FM, a word) plays through, as Plaits'
             * low-pass gate lets it; the voice's envelope runs on all the same, ready for the next engine */
            own_envelope[v] = grav_plaits_render(v, &p[v], plaits_out, plaits_aux, count);
            p[v].strike = 0;
            const float voice_gain = GRAV_VOICE_GAIN * voice->velocity;
            for (uint32_t j = 0; j < count; j++) {
                float level = envelope_process(&voice->env);
                float s = plaits_out[j] * ((own_envelope[v] ? 1.0f : level) * voice_gain);
                float a = fm_fabsf(s);
                if (a > peak[v]) peak[v] = a;
                mix[j] += s;
            }
        }

        /* the effect (off: the mix in both), then MASTER and the limiter on each side */
        grav_beads_process(fx, mix, fx_l, fx_r, count);
        for (uint32_t j = 0; j < count; j++) {
            uint32_t i = base + j;
            e->gain += (gain - e->gain) * 0.004f;   /* MASTER glides: no step in the sound */
            lr[2 * i] = (int32_t)(soft_limit(fx_l[j] * e->gain / GRAV_FULL_SCALE) * GRAV_FULL_SCALE);
            lr[2 * i + 1] = (int32_t)(soft_limit(fx_r[j] * e->gain / GRAV_FULL_SCALE) * GRAV_FULL_SCALE);
        }
    }

    /* a voice let go falls asleep once it is silent: its envelope done, or (an engine with its own) its sound */
    for (uint32_t v = 0; v < SYNTH_VOICES_MAX; v++) {
        synth_voice_t *voice = &synth->voice[v];
        if (e->asleep[v] || voice->gate)
            continue;
        if (own_envelope[v] ? peak[v] < GRAV_ASLEEP_PEAK : voice->env.stage == ENV_IDLE)
            e->asleep[v] = 1;
    }
}
