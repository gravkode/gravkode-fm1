#include "synth.h"

void synth_init(synth_t *s)
{
    s->note = -1;
    s->velocity = 1.0f;
    s->strikes = 0;
    s->held_n = 0;
    envelope_init(&s->amp_env);
    lfo_init(&s->mod_lfo, 44100.0f);
}

/* takes note out of the held list; 1 if it was there */
static int held_remove(synth_t *s, int32_t note)
{
    for (uint32_t i = 0; i < s->held_n; i++) {
        if (s->held[i] != (uint8_t)note)
            continue;
        for (; i + 1 < s->held_n; i++) {
            s->held[i] = s->held[i + 1];
            s->held_vel[i] = s->held_vel[i + 1];
        }
        s->held_n--;
        return 1;
    }
    return 0;
}

void synth_note_on(synth_t *s, int32_t note, uint32_t velocity)
{
    if (note < 0 || note > 127)
        return;
    if (velocity > 127u)
        velocity = 127u;
    held_remove(s, note);                    /* (the same note again: it moves to the top) */
    if (s->held_n == SYNTH_HELD_MAX)
        held_remove(s, s->held[0]);
    s->held[s->held_n] = (uint8_t)note;
    s->held_vel[s->held_n] = (uint8_t)velocity;
    s->held_n++;

    s->velocity = (float)velocity / 127.0f;
    s->note = note;
    s->strikes++;
    envelope_note_on(&s->amp_env);
}

void synth_note_off(synth_t *s, int32_t note)
{
    if (!held_remove(s, note) || note != s->note)
        return;                              /* not held, or held under the one sounding */
    if (s->held_n) {                         /* back to the note held before, as it is (no new attack) */
        s->note = s->held[s->held_n - 1];
        s->velocity = (float)s->held_vel[s->held_n - 1] / 127.0f;
        return;
    }
    s->note = -1;
    envelope_note_off(&s->amp_env);
}

void synth_panic(synth_t *s)
{
    s->held_n = 0;
    s->note = -1;
    envelope_note_off(&s->amp_env);
}
