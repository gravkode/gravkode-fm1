#include "lfo.h"
#include "fastmath.h"

#define LFO_TWO_PI 6.28318530717958647692f

void lfo_init(lfo_t *lfo, float sample_rate)
{
    lfo->phase = 0.0f;
    lfo->frequency = 1.0f;
    lfo->depth = 0.0f;
    lfo->sample_rate = sample_rate;
}

void lfo_set(lfo_t *lfo, float frequency, float depth)
{
    if (frequency < 0.0f) frequency = 0.0f;
    if (depth < 0.0f) depth = 0.0f;
    if (depth > 1.0f) depth = 1.0f;

    lfo->frequency = frequency;
    lfo->depth = depth;
}

float lfo_process(lfo_t *lfo)
{
    float value = fm_sinf(lfo->phase * LFO_TWO_PI) * lfo->depth;

    return value;
}

void lfo_advance(lfo_t *lfo, unsigned int samples)
{
    if (lfo->sample_rate <= 0.0f)
        return;

    lfo->phase += lfo->frequency * (float)samples / lfo->sample_rate;

    while (lfo->phase >= 1.0f)
        lfo->phase -= 1.0f;
}
