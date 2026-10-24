#include "grav_engine.h"
#include "fastmath.h"

void grav_engine_init(grav_engine_t *e)
{
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
    uint32_t master_q12
) {
    float target = note >= 0 ? 0.25f * velocity : 0.0f;
    float inc = note >= 0
        ? 440.0f * fm_exp2f((float)(note - 69) / 12.0f) / 44100.0f
        : 0.0f;
    float gain = (float)master_q12 / 4096.0f * 8300000.0f;

    for (uint32_t i = 0; i < n; i++) {
        e->level += (target - e->level) * 0.002f;
        e->gain += (gain - e->gain) * 0.004f;

        e->phase += inc;
        if (e->phase >= 1.0f)
            e->phase -= 1.0f;

        float s = wave
            ? 4.0f * fm_fabsf(e->phase - 0.5f) - 1.0f
            : fm_sinf(e->phase * 6.2831853f);

        s *= e->level * e->gain;

        lr[2 * i] = (int32_t)s;
        lr[2 * i + 1] = (int32_t)s;
    }
}
