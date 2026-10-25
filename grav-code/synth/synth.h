#pragma once
#include <stdint.h>
#include "envelope.h"
#include "lfo.h"

#define SYNTH_HELD_MAX 32u              /* notes remembered while held (keys and MIDI), per track; more drop the oldest */
#define SYNTH_VOICES_MAX 4u             /* the most voices (each a Plaits engine of its own: dsp/grav_plaits) */
#define SYNTH_TRACKS 4u                 /* the synth tracks (S1..S4), each with voices of its own */
#define SYNTH_NO_TRACK 0xFFu

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

/* A track: its voices (first..first + count - 1; none: it is silent), and the notes held on it. One voice: MONO
 * (the last note wins; letting go of it goes back to the note held before, without a new attack, and only the last
 * one lets the envelope release). More: POLY (each note a voice of the track's, a free one first, else the one let
 * go longest ago, else the oldest held). */
typedef struct {
    uint8_t first, count;
    uint8_t held[SYNTH_HELD_MAX];        /* the notes held, oldest first */
    uint8_t held_vel[SYNTH_HELD_MAX];    /* and the velocity each came with */
    uint32_t held_n;
} synth_track_t;

/* The voices, laid out by track in order (S1's first): the voices in use are 0..voices - 1. Called from the audio
 * interrupt only (the main loop's keys and settings come through the event queue). */
typedef struct {
    volatile int32_t note;               /* the last note still held on the track that last changed, -1 none */
    uint32_t voices;                     /* in use: the tracks' counts together (0..SYNTH_VOICES_MAX) */
    synth_voice_t voice[SYNTH_VOICES_MAX];
    uint8_t voice_track[SYNTH_VOICES_MAX];   /* the track each voice plays for (SYNTH_NO_TRACK: none) */
    synth_track_t track[SYNTH_TRACKS];
    uint32_t clock;                      /* notes begun so far: the voices' ages */
    uint32_t steals, shed_count;         /* diagnostic: reassigned ringing voices and overload cuts */
    lfo_t mod_lfo;
} synth_t;

void synth_init(synth_t *s);
/* the voices each track has (together at most SYNTH_VOICES_MAX): every voice let go at once when it changes */
void synth_set_tracks(synth_t *s, const uint8_t counts[SYNTH_TRACKS]);
void synth_track_note_on(synth_t *s, uint32_t track, int32_t note, uint32_t velocity);
void synth_track_note_off(synth_t *s, uint32_t track, int32_t note);
/* the same for S1 alone (it has every voice: MONO 1, POLY 2..4) */
void synth_set_voices(synth_t *s, uint32_t voices);
void synth_note_on(synth_t *s, int32_t note, uint32_t velocity);
void synth_note_off(synth_t *s, int32_t note);
void synth_panic(synth_t *s);
void synth_envelopes(synth_t *s, float attack, float decay, float sustain, float release, float sample_rate);
void synth_shed(synth_t *s);             /* the render ran too long: the oldest voice sounding fades out */
uint32_t synth_voices_held(const synth_t *s);   /* voices whose note is held (the tests) */
