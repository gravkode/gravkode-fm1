#include "synth.h"

void synth_init(synth_t *s)
{
    s->note = -1;
    s->velocity = 1.0f;
    envelope_init(&s->amp_env);
    lfo_init(&s->mod_lfo, 44100.0f);
}

void synth_note_on(synth_t *s, int32_t note, uint32_t velocity)
{
    s->velocity = (float)velocity / 127.0f;
    s->note = note;
    envelope_note_on(&s->amp_env);
}

void synth_note_off(synth_t *s, int32_t note)
{
    if (s->note == note) {
        s->note = -1;
        envelope_note_off(&s->amp_env);
        }
}

void synth_panic(synth_t *s)
{
    s->note = -1;
    envelope_note_off(&s->amp_env);
}

void synth_process_events(synth_t *s)
{
    uint32_t event = s->note_event;

    if (event == s->processed_event)
        return;

    int32_t note = s->note;

    if (note >= 0)
        envelope_note_on(&s->amp_env);
    else
        envelope_note_off(&s->amp_env);

    s->processed_event = event;
}
