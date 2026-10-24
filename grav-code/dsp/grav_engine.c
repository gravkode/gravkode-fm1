#include "grav_engine.h"
#include "fastmath.h"
extern int grav_cpp_test(int x);
extern void grav_plaits_init(void);
extern void grav_plaits_render(
    float note, float *out, float *aux, unsigned int size, unsigned int algorithm,
    float timbre, float morph, float harmonics);
void grav_engine_init(grav_engine_t *e)
{
    grav_plaits_init();
    e->last_note = 69;
    e->level = 0.0f;
    e->gain = 0.0f;
    e->velocity = 1.0f;
}

void grav_engine_render(
    grav_engine_t *e,
    int32_t *lr,
    uint32_t n,
    int32_t note,
    float velocity,
    uint8_t wave,
    uint32_t master_q12,
    float timbre,
    float morph,
    float harmonics,
    float pitch_offset,
    envelope_t *amp_env
) {

    if (note >= 0)
        e->velocity = velocity;

    float voice_gain = 0.25f * e->velocity;
    float gain = (float)master_q12 / 4096.0f * (float)grav_cpp_test(4150000);

    if (note >= 0)
        e->last_note = note;

    float plaits_out[32];
    float plaits_aux[32];

    for (uint32_t base = 0; base < n; base += 32) {
        uint32_t count = n - base;
        if (count > 32) count = 32;

        grav_plaits_render(
            (float)e->last_note + pitch_offset,
            plaits_out, plaits_aux, count, (unsigned int)wave,
            timbre, morph, harmonics);

        for (uint32_t j = 0; j < count; j++) {
            uint32_t i = base + j;
        e->level = envelope_process(amp_env);
        e->gain += (gain - e->gain) * 0.004f;

        float s = plaits_out[j];

        s *= e->level * voice_gain * e->gain;

        if (s > 8388607.0f) s = 8388607.0f;
        if (s < -8388608.0f) s = -8388608.0f;

        lr[2 * i] = (int32_t)s;
        lr[2 * i + 1] = (int32_t)s;
        }
    }
}

/* Float math compatibility for Mutable Instruments Plaits */
float fabsf(float x)
{
    return fm_fabsf(x);
}
