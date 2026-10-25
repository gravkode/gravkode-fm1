/* GRAV CODE's sequencer: 4 tracks (0.16; S1..S4, the synth's) of 64 steps in 4 pages of 16, each its own length of
 * 1..64 (polymeters), one tempo and swing, MIDI clock out and in. It runs in the audio interrupt: each block it gives the events that fall in it, at their sample
 * offsets on a 24-sample grid (app.c renders between them: 0.5 ms, not a block's 5.8 ms). The UI edits the pattern
 * (under plat_lock) and asks for start and stop through want. */
#pragma once
#include <stdint.h>

#define SEQ_STEPS 64u
#define SEQ_PAGE 16u                     /* steps on a page: the white keys */
#define SEQ_PAGES (SEQ_STEPS / SEQ_PAGE)
#define SEQ_NOTES 4u                     /* a step's chord, at most */
#define SEQ_BPM_MIN 30u
#define SEQ_BPM_MAX 300u
#define SEQ_SWING_MIN 50u                /* straight */
#define SEQ_SWING_MAX 75u
#define SEQ_GRID 24u                     /* the events' grid in samples: GRAV_PLAITS_BLOCK, 0.54 ms */
#define SEQ_TRACKS 4u
#define SEQ_SOUNDING 32u                 /* notes the sequencer holds at once */
#define SEQ_EVENTS 96u                   /* a block's events, at most */
#define SEQ_MAGIC_V1 0x53455131u         /* "SEQ1": 0.15's record, one track (seq_pattern_v1_t) */
#define SEQ_MAGIC 0x53455132u            /* "SEQ2": the pattern's record (app.c OBJ_SEQ) */

typedef struct {
    uint8_t on;                          /* 1: the step plays */
    uint8_t n;                           /* notes in its chord, 1..SEQ_NOTES */
    uint8_t note[SEQ_NOTES];             /* MIDI notes */
    uint8_t vel;                         /* 1..127 */
    uint8_t len;                         /* its length in steps, 1..SEQ_STEPS: the note ends an eighth of a step
                                          * before the end of its last step */
} seq_step_t;

typedef struct {
    uint8_t length;                      /* steps that play, 1..SEQ_STEPS */
    uint8_t rsv[3];
    seq_step_t step[SEQ_STEPS];
} seq_track_t;

typedef struct {
    uint32_t magic;                      /* SEQ_MAGIC */
    uint16_t bpm;                        /* SEQ_BPM_MIN..SEQ_BPM_MAX, quarter notes a minute; a step is a 16th */
    uint8_t swing;                       /* SEQ_SWING_MIN..MAX: the first of each pair of steps' share, in % */
    uint8_t rsv;
    seq_track_t track[SEQ_TRACKS];
} seq_pattern_t;

typedef struct {                         /* 0.15's record: one track */
    uint32_t magic;                      /* SEQ_MAGIC_V1 */
    uint16_t bpm;
    uint8_t swing, length;
    seq_step_t step[SEQ_STEPS];
} seq_pattern_v1_t;

/* a block's events, in the order to apply them: by offset, a note's end before a note's start at the same one */
enum { SEQ_EV_OFF = 1, SEQ_EV_STOP, SEQ_EV_START, SEQ_EV_CLOCK, SEQ_EV_ON };
typedef struct {
    uint16_t at;                         /* sample offset in the block, a multiple of SEQ_GRID */
    uint8_t type, note, vel, track;      /* a note's track (0..SEQ_TRACKS - 1) */
} seq_event_t;

/* the UI's requests (and MIDI in's stop), taken by the audio interrupt at its next block */
enum { SEQ_WANT_NONE, SEQ_WANT_PLAY, SEQ_WANT_STOP };

typedef struct {
    seq_pattern_t pat;
    volatile uint8_t want;               /* SEQ_WANT_* */
    volatile uint8_t playing;
    volatile uint8_t ext;                /* following MIDI clock in (one came in the last half second) */
    uint8_t all_off;                     /* a MIDI start: the notes sounding end at the next block */
    volatile int32_t step[SEQ_TRACKS];   /* each track's step playing, 0..its length - 1; -1: none yet */
    volatile uint32_t hits[SEQ_TRACKS];  /* notes each track began (the screen's and the lights' flashes) */
    volatile uint32_t count;             /* steps played since the start (their parity swings them) */
    int32_t due_q8;                      /* samples x 256 from this block's start to the next step (SEQ_FAR: none) */
    int32_t clock_q8;                    /* to the next MIDI clock out (24 a quarter note, straight) */
    uint32_t ext_ticks;                  /* MIDI clocks in since the start: every 6th is a step */
    uint32_t ext_quiet;                  /* samples since the last clock in */
    uint32_t win_samples, win_ticks;     /* the clock in's tempo: samples over a beat's 24 ticks */
    int32_t ext_step_q8;                 /* a step's length by the clock in */
    struct { uint8_t note, track; int32_t off_q8; } sounding[SEQ_SOUNDING];
    uint32_t nsounding;
} seq_t;

void seq_defaults(seq_pattern_t *p);
void seq_init(seq_t *s);
/* audio interrupt: the events of the next n samples (into ev, at most max), sorted; returns how many */
uint32_t seq_block(seq_t *s, uint32_t n, seq_event_t *ev, uint32_t max);
void seq_midi_clock(seq_t *s);           /* audio interrupt, MIDI in: a clock (F8) */
void seq_midi_start(seq_t *s, int cont); /* audio interrupt, MIDI in: start (FA) or continue (FB); stop (FC) is
                                          * want = SEQ_WANT_STOP */
uint32_t seq_bpm(const seq_t *s);        /* the tempo playing: the pattern's, or the clock in's */
int seq_valid(const seq_pattern_t *p);   /* a loaded record that can be played */
/* 0.15's record, read into S1 of a pattern (the others empty); 0 if it is no such record */
int seq_from_v1(const seq_pattern_v1_t *v1, seq_pattern_t *p);
