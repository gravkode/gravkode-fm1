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
#define GRAV_SHED_FADE 128u              /* a voice the overload protection takes fades out over 2.9 ms, no click */


#define GRAV_RENDER_FRAMES 256u
#define GRAV_CHUNKS(n) (((n) + GRAV_PLAITS_BLOCK - 1u) / GRAV_PLAITS_BLOCK)
#ifndef FM1B_CPU2
#define FM1B_CPU2 0
#endif

grav_debug_t grav_debug;
volatile uint32_t grav_dual_core = 1;
volatile uint32_t grav_debug_reset;

/* The render, on one core or two (CPU0: the audio interrupt; CPU1: the worker, one job at a time).
 *   The effect off: CPU1 renders its share of the voices for the whole block in one job while CPU0 renders the
 *   rest; then CPU0 mixes them and passes the mix through (Beads off is a copy).
 *   The effect on: CPU1 runs Beads, a chunk (24 samples) behind the voices: in job c it renders its voices' chunk
 *   c and takes chunk c-1 of the mix through Beads, while CPU0 renders its voices' chunk c. The voices go to the
 *   core with less to do (the last blocks' timings), so Beads (more than half a block's time on the FM-1) no
 *   longer comes on top of them.
 * Each voice and Beads keep a random stream of their own, and every sum is made in the same order, so the sound
 * is the same, sample for sample, on one core or two and whatever the split. The workers own disjoint voices and
 * rows. No pointers in the uint32_t mailbox argument (also works on a 64-bit host). */
static struct {
    synth_t *synth;
    const grav_beads_params_t *fx;
    uint32_t n, masks[2], live;          /* the voices each core renders; live: both (the mix) */
    grav_plaits_params_t p[SYNTH_VOICES_MAX];
    float peak[SYNTH_VOICES_MAX];
    int own_envelope[SYNTH_VOICES_MAX];
    uint32_t fade[SYNTH_VOICES_MAX];     /* a voice fading out (shed): its fade's samples left at the block's start */
    float out[SYNTH_VOICES_MAX][GRAV_RENDER_FRAMES];
    float mix[GRAV_RENDER_FRAMES], fx_l[GRAV_RENDER_FRAMES], fx_r[GRAV_RENDER_FRAMES];
    uint32_t ticks[2], fx_ticks, voice_ticks[SYNTH_VOICES_MAX];
} work __attribute__((aligned(32)));

#define JOB_VOICES (1u << 8)             /* a job: its voices' chunk (bits 0..7), */
#define JOB_EFFECT (1u << 16)            /* the effect's chunk (bits 24..31), */
#define JOB_BLOCK (1u << 17)             /* or its voices' whole block */

/* chunk c of the voices one core renders (mask index core) */
static void render_chunk(uint32_t core, uint32_t c)
{
    float out[GRAV_PLAITS_BLOCK], aux[GRAV_PLAITS_BLOCK];
    const uint32_t base = c * GRAV_PLAITS_BLOCK;
    uint32_t count = work.n - base;
    if (count > GRAV_PLAITS_BLOCK) count = GRAV_PLAITS_BLOCK;
    for (uint32_t v = 0; v < SYNTH_VOICES_MAX; ++v) {
        if (!(work.masks[core] & (1u << v)))
            continue;
        const uint32_t start = plat_ticks24();
        synth_voice_t *voice = &work.synth->voice[v];
        const uint32_t fade = work.fade[v];
        float *row = &work.out[v][base];
        if (fade && base >= fade) {      /* faded out: silence, and no engine to run */
            memset(row, 0, count * sizeof *row);
        } else {
            float peak = work.peak[v];
            work.own_envelope[v] = grav_plaits_render(v, &work.p[v], out, aux, count);
            work.p[v].strike = 0;
            for (uint32_t j = 0; j < count; ++j) {
                const float level = envelope_process(&voice->env);
                float sample = out[j] * ((work.own_envelope[v] ? 1.0f : level) *
                                         (GRAV_VOICE_GAIN * voice->velocity));
                if (fade) {
                    const uint32_t i = base + j;
                    sample *= i < fade ? (float)(fade - i) * (1.0f / (float)GRAV_SHED_FADE) : 0.0f;
                }
                const float a = fm_fabsf(sample);
                if (a > peak) peak = a;
                row[j] = sample;
            }
            work.peak[v] = peak;
        }
        const uint32_t t = plat_ticks24() - start;
        work.voice_ticks[v] += t;
        work.ticks[core] += t;
    }
}

/* chunk c of the mix (every voice rendered, in order) through the effect */
static void effect_chunk(uint32_t c)
{
    const uint32_t start = plat_ticks24();
    const uint32_t base = c * GRAV_PLAITS_BLOCK;
    uint32_t count = work.n - base;
    if (count > GRAV_PLAITS_BLOCK) count = GRAV_PLAITS_BLOCK;
    for (uint32_t j = 0; j < count; ++j) {
        float sample = 0.0f;
        for (uint32_t v = 0; v < SYNTH_VOICES_MAX; ++v)
            if (work.live & (1u << v)) sample += work.out[v][base + j];
        work.mix[base + j] = sample;
    }
    grav_beads_process(work.fx, &work.mix[base], &work.fx_l[base], &work.fx_r[base], count);
    work.fx_ticks += plat_ticks24() - start;
}

/* CPU1's job (or CPU0's, when CPU1 cannot take it: the same work, in the same order) */
static void worker(uint32_t job)
{
    if (job & JOB_BLOCK) {
        for (uint32_t c = 0; c < GRAV_CHUNKS(work.n); ++c)
            render_chunk(1, c);
        return;
    }
    if (job & JOB_VOICES)
        render_chunk(1, job & 0xFFu);
    if (job & JOB_EFFECT)
        effect_chunk(job >> 24);
}

/* hand a job to CPU1, or do it here: 1 handed over, 0 done here */
static int dispatch(int dual, uint32_t job)
{
#if FM1B_CPU2
    if (dual && plat_cpu2_run(worker, job))
        return 1;
#else
    (void)dual;
#endif
    worker(job);
    return 0;
}

/* wait for CPU1's job: 0 done; -1 it was given up and the job dropped (cpu2_fallback): the block is lost */
static int join(int dispatched)
{
    if (!dispatched)
        return 0;
    const uint32_t start = plat_ticks24();
    int r = 0;
#if FM1B_CPU2
    r = plat_cpu2_wait();
    grav_debug.jobs++;
#endif
    grav_debug.wait_us += (plat_ticks24() - start) / PLAT_TICKS_PER_US;
    return r < 0 ? -1 : 0;
}

_Static_assert(SYNTH_VOICES_MAX == GRAV_VOICES, "a Plaits engine for each of the synth's voices");
_Static_assert(GRAV_CHUNKS(GRAV_RENDER_FRAMES) <= 0xFFu, "a chunk's index fits a job's byte");

void grav_engine_init(grav_engine_t *e)
{
    memset(&work, 0, sizeof work);
    memset(&grav_debug, 0, sizeof grav_debug);
    grav_dual_core = 1;
    grav_debug_reset = 0;
    grav_plaits_init();
    grav_beads_init();
    e->gain = 0.0f;
    e->voice_cost = e->fx_cost = 0;
    for (uint32_t v = 0; v < SYNTH_VOICES_MAX; v++) {
        e->strikes_seen[v] = 0;
        e->asleep[v] = 1;
        e->fade_left[v] = 0;
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
    grav_sound_t sound[SYNTH_TRACKS];
    for (uint32_t t = 0; t < SYNTH_TRACKS; t++) {
        sound[t].engine = engine;
        sound[t].timbre = timbre;
        sound[t].morph = morph;
        sound[t].harmonics = harmonics;
    }
    grav_engine_render_tracks(e, lr, n, synth, master_q12, sound, pitch_offset, fx);
}

static int is_six_op(uint32_t engine)
{
    return engine >= GRAV_ENGINE_SIX_OP_1 && engine <= GRAV_ENGINE_SIX_OP_3;
}

void grav_engine_render_tracks(grav_engine_t *e, int32_t *lr, uint32_t n, synth_t *synth, uint32_t master_q12,
                               const grav_sound_t sound[SYNTH_TRACKS], float pitch_offset,
                               const grav_beads_params_t *fx)
{
    const float gain = (float)master_q12 / 4096.0f * GRAV_OUT_FULL;
    uint8_t six_op[SYNTH_VOICES_MAX];
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
    work.fx = fx;
    work.n = n;
    work.masks[0] = work.masks[1] = 0;
    work.ticks[0] = work.ticks[1] = work.fx_ticks = 0;
    grav_debug.wait_us = 0;
    int dual = 0;
#if FM1B_CPU2
    dual = grav_dual_core && plat_cpu2_on();
#endif
    /* the effect on CPU1 while it runs (or fades); off, it is a copy: CPU0's */
    const uint32_t fx_core = dual && grav_beads_busy(fx) ? 1u : 0u;
    /* the cores' work so far this block, by the last blocks' timings: the effect, then each voice to the core with
     * less (a tie: CPU0, CPU1 every other voice with the effect off) */
    int32_t load[2] = {0, fx_core ? e->fx_cost : 0};
    uint32_t active = 0;
    for (uint32_t v = 0; v < SYNTH_VOICES_MAX; v++) {
        synth_voice_t *voice = &synth->voice[v];
        const uint32_t track = synth->voice_track[v];
        const grav_sound_t *snd = &sound[track < SYNTH_TRACKS ? track : 0];
        /* a track's engine changed: built anew in its voices, only with the previous block's worker joined */
        if (track < SYNTH_TRACKS)
            grav_plaits_voice_engine(v, snd->engine);
        six_op[v] = (uint8_t)is_six_op(grav_plaits_voice_engine_id(v));
        int strike = voice->strikes != e->strikes_seen[v];   /* a note began on it since the last block */
        e->strikes_seen[v] = voice->strikes;
        if (strike) {
            e->asleep[v] = 0;
            e->fade_left[v] = 0;
        }
        if (v >= synth->voices) {        /* (VOICES changed: every note was let go) */
            if (six_op[v] && !e->asleep[v])
                grav_plaits_reset_voice(v);
            e->asleep[v] = 1;
            e->fade_left[v] = 0;
        } else if (voice->shed && !e->asleep[v] && !e->fade_left[v]) {
            e->fade_left[v] = GRAV_SHED_FADE;   /* the overload protection took it: it fades out, then sleeps */
        }
        work.p[v].note = (float)voice->note + pitch_offset;
        work.p[v].timbre = snd->timbre;
        work.p[v].morph = snd->morph;
        work.p[v].harmonics = snd->harmonics;
        work.p[v].accent = voice->velocity;
        /* Let the outer ENV own the six-op note's release. Releasing the DX gate here would
         * also start its short patch release and cut off the app's longer release. */
        work.p[v].gate = voice->gate || (six_op[v] && voice->env.stage != ENV_IDLE);
        work.p[v].strike = (uint8_t)strike;
        work.peak[v] = 0.0f;
        work.own_envelope[v] = 0;
        work.fade[v] = e->fade_left[v];
        work.voice_ticks[v] = 0;
        if (!e->asleep[v]) {
            const uint32_t core = dual && load[1] < load[0] ? 1u : 0u;
            work.masks[core] |= 1u << v;
            load[core] += e->voice_cost + 1;
            active++;
        }
    }
    work.live = work.masks[0] | work.masks[1];

    const uint32_t chunks = GRAV_CHUNKS(n);
    int failed = 0;
    if (!fx_core) {
        /* CPU1 renders its voices' whole block; CPU0 its own, then the mix through the effect */
        const int dispatched = work.masks[1] ? dispatch(dual, JOB_BLOCK) : 0;
        for (uint32_t c = 0; c < chunks; ++c)
            render_chunk(0, c);
        failed = join(dispatched);
        for (uint32_t c = 0; c < chunks && !failed; ++c)
            effect_chunk(c);
    } else {
        /* CPU1 takes chunk c-1 through the effect (and renders its voices' chunk c) while CPU0 renders its voices'
         * chunk c; a chunk joined before the next is handed over */
        for (uint32_t c = 0; c <= chunks && !failed; ++c) {
            uint32_t job = 0;
            if (c < chunks && work.masks[1])
                job |= JOB_VOICES | c;
            if (c > 0)
                job |= JOB_EFFECT | ((c - 1u) << 24);
            const int dispatched = job ? dispatch(dual, job) : 0;
            if (c < chunks)
                render_chunk(0, c);
            failed = join(dispatched);
        }
    }
    if (failed) {                        /* CPU1 given up mid-job (cpu2_fallback): this block is lost, one core on */
        memset(lr, 0, 2u * n * sizeof *lr);
        return;
    }

    grav_debug.voice_us[0] = work.ticks[0] / PLAT_TICKS_PER_US;
    grav_debug.voice_us[1] = work.ticks[1] / PLAT_TICKS_PER_US;
    grav_debug.fx_us = work.fx_ticks / PLAT_TICKS_PER_US;
    grav_debug.fx_core = fx_core;
    grav_debug.masks[0] = work.masks[0];
    grav_debug.masks[1] = work.masks[1];
    grav_debug.active = active;
    grav_debug.steals = synth->steals;
    grav_debug.shed = synth->shed_count;

    /* MASTER and the limiter on each side */
    for (uint32_t i = 0; i < n; i++) {
        e->gain += (gain - e->gain) * 0.004f;   /* MASTER glides: no step in the sound */
        lr[2 * i] = (int32_t)(soft_limit(work.fx_l[i] * e->gain / GRAV_FULL_SCALE) * GRAV_FULL_SCALE);
        lr[2 * i + 1] = (int32_t)(soft_limit(work.fx_r[i] * e->gain / GRAV_FULL_SCALE) * GRAV_FULL_SCALE);
    }

    /* the timings the next block splits by, smoothed: a voice's block (on average), the effect's */
    if (active) {
        uint32_t sum = 0;
        for (uint32_t v = 0; v < SYNTH_VOICES_MAX; v++)
            sum += work.voice_ticks[v];
        e->voice_cost += ((int32_t)(sum / active) - e->voice_cost) / 4;
    }
    if (grav_beads_on())
        e->fx_cost += ((int32_t)work.fx_ticks - e->fx_cost) / 4;

    for (uint32_t v = 0; v < SYNTH_VOICES_MAX; v++) {
        synth_voice_t *voice = &synth->voice[v];
        if (e->asleep[v])
            continue;
        if (e->fade_left[v]) {           /* fading out: asleep once it is silent, its envelope at rest */
            e->fade_left[v] = e->fade_left[v] > n ? e->fade_left[v] - n : 0;
            if (!e->fade_left[v]) {
                if (six_op[v])
                    grav_plaits_reset_voice(v);
                envelope_init(&voice->env);
                e->asleep[v] = 1;
            }
            continue;                    /* (or the rest of its fade next block) */
        }
        /* a voice let go falls asleep once it is silent: its envelope done, or (an engine with its own) its sound */
        if (voice->gate)
            continue;
        if (work.own_envelope[v] ? work.peak[v] < GRAV_ASLEEP_PEAK : voice->env.stage == ENV_IDLE) {
            if (six_op[v])
                grav_plaits_reset_voice(v);   /* no deferred old DX release on the next note */
            e->asleep[v] = 1;
        }
    }
}
