#pragma once

typedef struct {
    float phase;
    float frequency;
    float depth;
    float sample_rate;
} lfo_t;

void lfo_init(lfo_t *lfo, float sample_rate);
void lfo_set(lfo_t *lfo, float frequency, float depth);
float lfo_process(lfo_t *lfo);
void lfo_advance(lfo_t *lfo, unsigned int samples);
