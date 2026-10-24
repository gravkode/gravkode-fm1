#include "envelope.h"

static float clamp01(float x)
{
    if (x < 0.0f) return 0.0f;
    if (x > 1.0f) return 1.0f;
    return x;
}

static float step_for(float seconds, float sample_rate)
{
    if (seconds <= 0.0f || sample_rate <= 0.0f)
        return 1.0f;

    return 1.0f / (seconds * sample_rate);
}

void envelope_init(envelope_t *env)
{
    env->stage = ENV_IDLE;
    env->level = 0.0f;
    env->attack_step = 1.0f;
    env->decay_step = 1.0f;
    env->release_step = 1.0f;
    env->release_samples = 1.0f;
    env->sustain = 0.7f;
}

void envelope_set(envelope_t *env,
                  float attack_seconds,
                  float decay_seconds,
                  float sustain,
                  float release_seconds,
                  float sample_rate)
{
    env->attack_step = step_for(attack_seconds, sample_rate);
    env->sustain = clamp01(sustain);
    env->decay_step = (1.0f - env->sustain)
                    * step_for(decay_seconds, sample_rate);

    /* RELEASE step is calculated at note-off. */
    env->release_samples = release_seconds > 0.0f && sample_rate > 0.0f
        ? release_seconds * sample_rate : 1.0f;
}

void envelope_note_on(envelope_t *env)
{
    env->stage = ENV_ATTACK;
}

void envelope_note_off(envelope_t *env)
{
    if (env->stage != ENV_IDLE) {
        if (env->stage == ENV_ATTACK && env->level <= 0.0f)
            env->level = env->attack_step;
        env->release_step = env->level / env->release_samples;
        env->stage = ENV_RELEASE;
    }
}

float envelope_process(envelope_t *env)
{
    switch (env->stage) {
    case ENV_ATTACK:
        env->level += env->attack_step;
        if (env->level >= 1.0f) {
            env->level = 1.0f;
            env->stage = ENV_DECAY;
        }
        break;

    case ENV_DECAY:
        env->level -= env->decay_step;
        if (env->level <= env->sustain) {
            env->level = env->sustain;
            env->stage = ENV_SUSTAIN;
        }
        break;

    case ENV_SUSTAIN:
        env->level = env->sustain;
        break;

    case ENV_RELEASE:
        env->level -= env->release_step;
        if (env->level <= 0.0f) {
            env->level = 0.0f;
            env->stage = ENV_IDLE;
        }
        break;

    case ENV_IDLE:
    default:
        env->level = 0.0f;
        break;
    }

    return env->level;
}
