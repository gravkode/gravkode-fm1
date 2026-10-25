#include "grav_engine.h"
#include "grav_plaits.h"
#include "fastmath.h"
#include "plat.h"
#include <string.h>

/* a sample of 1.0 from an engine, at full MASTER: just under the 24-bit full scale (2^23), the rest the headroom
 * left for an engine's peaks above 1.0 */
#define GRAV_OUT_FULL 8300000.0f
#define GRAV_VOICE_GAIN 0.25f            /* a voice at velocity 127, of that */
#define GRAV_FULL_SCALE 8388607.0f
#define GRAV_LIMIT_KNEE 0.5f             /* the limiter leaves all below half the full scale (-6 dB) untouched */
#define GRAV_ASLEEP_PEAK 1e-5f           /* a voice let go and this quiet (-100 dB) for a block falls asleep */


#define GRAV_RENDER_FRAMES 256u
#ifndef FM1B_CPU2
#define FM1B_CPU2 0
#endif

grav_debug_t grav_debug;
volatile uint32_t grav_dual_core = 1;
volatile uint32_t grav_debug_reset;

/* One publication per audio block; workers own disjoint voices and output rows.
 * No pointers in the uint32_t mailbox argument (also works on a 64-bit host). */
static struct {
    synth_t *synth;
    uint32_t n, masks[2];
    grav_plaits_params_t p[SYNTH_VOICES_MAX];
    float peak[SYNTH_VOICES_MAX];
    int own_envelope[SYNTH_VOICES_MAX];
    float out[SYNTH_VOICES_MAX][GRAV_RENDER_FRAMES];
    uint32_t ticks[2];
} work __attribute__((aligned(32)));

static void render_voices(uint32_t core)
{
    const uint32_t start = plat_ticks24();
    float out[GRAV_PLAITS_BLOCK], aux[GRAV_PLAITS_BLOCK];
    for (uint32_t v = 0; v < SYNTH_VOICES_MAX; ++v) {
        if (!(work.masks[core] & (1u << v)))
            continue;
        synth_voice_t *voice = &work.synth->voice[v];
        float peak = 0.0f;
        for (uint32_t base = 0; base < work.n; base += GRAV_PLAITS_BLOCK) {
            uint32_t count = work.n - base;
            if (count > GRAV_PLAITS_BLOCK) count = GRAV_PLAITS_BLOCK;
            work.own_envelope[v] = grav_plaits_render(v, &work.p[v], out, aux, count);
            work.p[v].strike = 0;
            for (uint32_t j = 0; j < count; ++j) {
                const float level = envelope_process(&voice->env);
                const float sample = out[j] * ((work.own_envelope[v] ? 1.0f : level) *
                                               (GRAV_VOICE_GAIN * voice->velocity));
                const float a = fm_fabsf(sample);
                if (a > peak) peak = a;
                work.out[v][base + j] = sample;
            }
        }
        work.peak[v] = peak;
    }
    work.ticks[core] = plat_ticks24() - start;
}

_Static_assert(SYNTH_VOICES_MAX == GRAV_VOICES, "a Plaits engine for each of the synth's voices");

void grav_engine_init(grav_engine_t *e)
{
    memset(&work, 0, sizeof work);
    memset(&grav_debug, 0, sizeof grav_debug);
    grav_dual_core = 1;
    grav_debug_reset = 0;
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
    if (grav_debug_reset) {
        memset(&grav_debug, 0, sizeof grav_debug);
        synth->steals = synth->shed_count = 0;
        grav_debug_reset = 0;
    }
    /* The platform calls with APP_BLOCK <= 256. Reject an invalid call before any buffer access. */
    if (n > GRAV_RENDER_FRAMES) {
        memset(lr, 0, 2u * n * sizeof *lr);
        return;
    }
    work.synth = synth;
    work.n = n;
    work.masks[0] = work.masks[1] = 0;
    work.ticks[0] = work.ticks[1] = 0;
    uint32_t active = 0;
    int dual = 0;
#if FM1B_CPU2
    dual = grav_dual_core && plat_cpu2_on();
#endif
    /* Switching engines constructs every voice, only with the previous block's worker joined. */
    grav_plaits_engine(engine);
    for (uint32_t v = 0; v < SYNTH_VOICES_MAX; v++) {
        synth_voice_t *voice = &synth->voice[v];
        int strike = voice->strikes != e->strikes_seen[v];   /* a note began on it since the last block */
        e->strikes_seen[v] = voice->strikes;
        if (strike)
            e->asleep[v] = 0;
        if (voice->shed || v >= synth->voices)
            e->asleep[v] = 1;
        work.p[v].note = (float)voice->note + pitch_offset;
        work.p[v].timbre = timbre;
        work.p[v].morph = morph;
        work.p[v].harmonics = harmonics;
        work.p[v].accent = voice->velocity;
        work.p[v].gate = voice->gate;
        work.p[v].strike = (uint8_t)strike;
        work.peak[v] = 0.0f;
        work.own_envelope[v] = 0;
        if (!e->asleep[v]) {
            work.masks[dual ? (active & 1u) : 0u] |= 1u << v;
            active++;
        }
    }

    int dispatched = 0;
#if FM1B_CPU2
    if (work.masks[1])
        dispatched = plat_cpu2_run(render_voices, 1);
#endif
    if (!dispatched) {
        work.masks[0] |= work.masks[1];
        work.masks[1] = 0;
    }
    render_voices(0);
    uint32_t wait_start = plat_ticks24();
#if FM1B_CPU2
    if (dispatched) {
        /* cpu2_fallback=false: an interrupted DSP job is never replayed on altered engine state. */
        if (plat_cpu2_wait() < 0) {
            memset(lr, 0, 2u * n * sizeof *lr);
            return;
        }
        grav_debug.jobs++;
    }
#endif
    grav_debug.wait_us = dispatched ? (plat_ticks24() - wait_start) / PLAT_TICKS_PER_US : 0;
    grav_debug.voice_us[0] = work.ticks[0] / PLAT_TICKS_PER_US;
    grav_debug.voice_us[1] = work.ticks[1] / PLAT_TICKS_PER_US;
    grav_debug.masks[0] = work.masks[0];
    grav_debug.masks[1] = work.masks[1];
    grav_debug.active = active;
    grav_debug.steals = synth->steals;
    grav_debug.shed = synth->shed_count;

    float mix[GRAV_PLAITS_BLOCK];
    float fx_l[GRAV_PLAITS_BLOCK], fx_r[GRAV_PLAITS_BLOCK];
    uint32_t fx_ticks = 0;
    for (uint32_t base = 0; base < n; base += GRAV_PLAITS_BLOCK) {
        uint32_t count = n - base;
        if (count > GRAV_PLAITS_BLOCK) count = GRAV_PLAITS_BLOCK;
        for (uint32_t j = 0; j < count; ++j) {
            float sample = 0.0f;
            /* Preserve summation order in both modes, making one/two-core output comparable. */
            for (uint32_t v = 0; v < SYNTH_VOICES_MAX; ++v)
                if (!e->asleep[v]) sample += work.out[v][base + j];
            mix[j] = sample;
        }

        /* the effect (off: the mix in both), then MASTER and the limiter on each side */
        uint32_t fx_start = plat_ticks24();
        grav_beads_process(fx, mix, fx_l, fx_r, count);
        fx_ticks += plat_ticks24() - fx_start;
        for (uint32_t j = 0; j < count; j++) {
            uint32_t i = base + j;
            e->gain += (gain - e->gain) * 0.004f;   /* MASTER glides: no step in the sound */
            lr[2 * i] = (int32_t)(soft_limit(fx_l[j] * e->gain / GRAV_FULL_SCALE) * GRAV_FULL_SCALE);
            lr[2 * i + 1] = (int32_t)(soft_limit(fx_r[j] * e->gain / GRAV_FULL_SCALE) * GRAV_FULL_SCALE);
        }
    }

    grav_debug.fx_us = fx_ticks / PLAT_TICKS_PER_US;

    /* a voice let go falls asleep once it is silent: its envelope done, or (an engine with its own) its sound */
    for (uint32_t v = 0; v < SYNTH_VOICES_MAX; v++) {
        synth_voice_t *voice = &synth->voice[v];
        if (e->asleep[v] || voice->gate)
            continue;
        if (work.own_envelope[v] ? work.peak[v] < GRAV_ASLEEP_PEAK : voice->env.stage == ENV_IDLE)
            e->asleep[v] = 1;
    }
}
