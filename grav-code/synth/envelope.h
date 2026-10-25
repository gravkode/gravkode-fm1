#pragma once
#include <stdint.h>

typedef enum {
    ENV_IDLE = 0,
    ENV_ATTACK,
    ENV_DECAY,
    ENV_SUSTAIN,
    ENV_RELEASE,
    ENV_RETRIGGER
} envelope_stage_t;

typedef struct {
    envelope_stage_t stage;
    float level;
    float release_step;
    float release_samples;
    float attack_step;
    float decay_step;
    float sustain;
    float retrigger_samples;
    float retrigger_step;
} envelope_t;

void envelope_init(envelope_t *env);
void envelope_note_on(envelope_t *env);
void envelope_note_off(envelope_t *env);
void envelope_set(envelope_t *env,
                  float attack_seconds,
                  float decay_seconds,
                  float sustain,
                  float release_seconds,
                  float sample_rate);
float envelope_process(envelope_t *env);
