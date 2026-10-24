#pragma once
#include <stdint.h>
#include "envelope.h"
#include "lfo.h"

#define SYNTH_HELD_MAX 32u              /* notes remembered while held (keys and MIDI); more drop the oldest */

/* One voice, the last note wins. The notes held are kept in order, so letting go of the note that sounds goes back
 * to the one held before it (no new attack); letting go of the last one releases the envelope. Called from the
 * audio interrupt only (the main loop's keys come through the event queue). */
typedef struct {
    volatile int32_t note;               /* the note sounding, -1 none */
    volatile float velocity;             /* its velocity, 0..1 */
    uint32_t strikes;                    /* notes begun so far (not counting a return to a held one): a change is a
                                          * new attack, for an engine that strikes (Modal) */
    uint8_t held[SYNTH_HELD_MAX];        /* the notes held, oldest first: the last one sounds */
    uint8_t held_vel[SYNTH_HELD_MAX];    /* and the velocity each came with */
    uint32_t held_n;
    envelope_t amp_env;
    lfo_t mod_lfo;
} synth_t;

void synth_init(synth_t *s);
void synth_note_on(synth_t *s, int32_t note, uint32_t velocity);
void synth_note_off(synth_t *s, int32_t note);
void synth_panic(synth_t *s);
