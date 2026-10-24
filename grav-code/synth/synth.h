#pragma once
#include <stdint.h>
#include "envelope.h"
#include "lfo.h"

typedef struct {
    volatile int32_t note;
    volatile float velocity;
    volatile uint32_t note_event;
    uint32_t processed_event;
    envelope_t amp_env;
    lfo_t mod_lfo;
} synth_t;

void synth_init(synth_t *s);
void synth_note_on(synth_t *s, int32_t note, uint32_t velocity);
void synth_note_off(synth_t *s, int32_t note);
void synth_panic(synth_t *s);
void synth_process_events(synth_t *s);
