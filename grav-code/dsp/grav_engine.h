#pragma once
#include <stdint.h>

typedef struct {
    float phase;
    float level;
    float gain;
} grav_engine_t;

void grav_engine_init(grav_engine_t *e);
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
);
