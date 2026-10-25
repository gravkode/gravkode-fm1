#pragma once
#include <stdint.h>
#include "envelope.h"
#include "lfo.h"

#define SYNTH_HELD_MAX 32u              /* notes remembered while held (keys and MIDI); more drop the oldest */
#define SYNTH_VOICES_MAX 4u             /* the most voices (each a Plaits engine of its own: dsp/grav_plaits) */

/* A voice: the note it plays (or played: it rings on after it is let go), its velocity and envelope. */
typedef struct {
    int32_t note;                        /* -1: never played */
    float velocity;                      /* 0..1 */
    uint8_t gate;                        /* its note is held */
    uint8_t shed;                        /* silenced to save the CPU (synth_shed): quiet until its next note */
    uint32_t strikes;                    /* notes begun on it: a change is a new attack (an engine that strikes) */
    uint32_t age;                        /* when its note began (the synth's clock): the oldest is taken first */
    envelope_t env;
} synth_voice_t;

/* The voices, MONO (one: the last note wins; letting go of it goes back to the note held before, without a new
 * attack, and only the last one lets the envelope release) or POLY (2..SYNTH_VOICES_MAX: each note a voice, a free
 * one first, else the one let go longest ago, else the oldest held). Called from the audio interrupt only (the main
 * loop's keys and settings come through the event queue). */
typedef struct {
    volatile int32_t note;               /* the last note still held, -1 none (the screen, the tests) */
    uint32_t voices;                     /* in use: 1 (MONO) .. SYNTH_VOICES_MAX */
    synth_voice_t voice[SYNTH_VOICES_MAX];
    uint8_t held[SYNTH_HELD_MAX];        /* the notes held, oldest first */
    uint8_t held_vel[SYNTH_HELD_MAX];    /* and the velocity each came with */
    uint32_t held_n;
    uint32_t clock;                      /* notes begun so far: the voices' ages */
    uint32_t steals, shed_count;         /* diagnostic: reassigned ringing voices and overload cuts */
    lfo_t mod_lfo;
} synth_t;

void synth_init(synth_t *s);
void synth_set_voices(synth_t *s, uint32_t voices);   /* MONO (1) or POLY: every voice let go at once */
void synth_note_on(synth_t *s, int32_t note, uint32_t velocity);
void synth_note_off(synth_t *s, int32_t note);
void synth_panic(synth_t *s);
void synth_envelopes(synth_t *s, float attack, float decay, float sustain, float release, float sample_rate);
void synth_shed(synth_t *s);             /* the render ran too long: the oldest voice sounding falls silent */
uint32_t synth_voices_held(const synth_t *s);   /* voices whose note is held (the tests) */
