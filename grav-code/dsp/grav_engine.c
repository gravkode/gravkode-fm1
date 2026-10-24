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
    e->phase = 0.0f;
    e->level = 0.0f;
    e->gain = 0.0f;
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
    float harmonics
) {
    float target = note >= 0 ? 0.25f * velocity : 0.0f;
    float inc = note >= 0
        ? 440.0f * fm_exp2f((float)(note - 69) / 12.0f) / 44100.0f
        : 0.0f;
    float gain = (float)master_q12 / 4096.0f * (float)grav_cpp_test(4150000);

    static int32_t last_note = 69;

    if (note >= 0)
        last_note = note;

    float plaits_out[32];
    float plaits_aux[32];

    for (uint32_t base = 0; base < n; base += 32) {
        uint32_t count = n - base;
        if (count > 32) count = 32;

        if (wave)
            grav_plaits_render(
                (float)last_note,
                plaits_out, plaits_aux, count, (unsigned int)wave,
                timbre, morph, harmonics);

        for (uint32_t j = 0; j < count; j++) {
            uint32_t i = base + j;
        e->level += (target - e->level) * 0.002f;
        e->gain += (gain - e->gain) * 0.004f;

        e->phase += inc;
        if (e->phase >= 1.0f)
            e->phase -= 1.0f;

        float s = wave
            ? plaits_out[j]
            : fm_sinf(e->phase * 6.2831853f);

        s *= e->level * e->gain;

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
