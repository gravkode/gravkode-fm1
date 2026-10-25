/* GRAV CODE's sequencer: 8 tracks (S1..S4 the synth's, 0.16; D1..D4 the drums', 0.17) of 64 steps in 4 pages of 16,
 * each its own length of 1..64 (polymeters), one tempo and swing, MIDI clock out and in. It runs in the audio
 * interrupt: each block it gives the events that fall in it, at their sample offsets on a 24-sample grid (app.c renders between them: 0.5 ms, not a block's 5.8 ms). The UI edits the pattern
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
#define SEQ_TRACKS 8u
#define SEQ_S_TRACKS 4u                  /* the synth tracks, 0..3; the drum tracks 4..7 */
#define SEQ_SOUNDING 48u                 /* notes the sequencer holds at once */
#define SEQ_EVENTS 128u                  /* a block's events, at most */
#define SEQ_MAGIC_V1 0x53455131u         /* "SEQ1": 0.15's record, one track (seq_pattern_v1_t) */
#define SEQ_MAGIC 0x53455132u            /* "SEQ2": the synth tracks' record (app.c OBJ_SEQ; 0.16's whole pattern) */
#define SEQ_MAGIC_DRUMS 0x44524D31u      /* "DRM1": the drum tracks' record (app.c OBJ_DRUMS) */

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
    uint16_t bpm;                        /* the tempo it was saved with (SEQ_BPM_MIN..MAX); since 0.18 the song's
                                          * tempo is seq_t's, every pattern's */
    uint8_t swing;                       /* SEQ_SWING_MIN..MAX: the first of each pair of steps' share, in % */
    uint8_t rsv;
    seq_track_t track[SEQ_TRACKS];
    uint32_t drum_magic;                 /* SEQ_MAGIC_DRUMS: the end of the drum tracks' record */
} seq_pattern_t;

/* saved in two records (a store object holds 3840 bytes at most), each as it lies in the pattern (no copy to save):
 * its head, the synth tracks with the tempo (as 0.16 saved its whole pattern), and its tail, the drum tracks */
typedef struct {
    uint32_t magic;                      /* SEQ_MAGIC */
    uint16_t bpm;
    uint8_t swing, rsv;
    seq_track_t track[SEQ_S_TRACKS];
} seq_record_t;

typedef struct {
    seq_track_t track[SEQ_TRACKS - SEQ_S_TRACKS];
    uint32_t magic;                      /* SEQ_MAGIC_DRUMS */
} seq_drum_record_t;

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
#define SEQ_NO_PATTERN 0xFFu

/* The patterns (0.18): pat is the one playing and edited; another is queued (next), and once the one playing is saved
 * (edits == saved: the UI counts its edits under plat_lock, and the saves that caught up with them) and the main
 * loop has found the next one's steps (next_src, in the flash; 0: an empty pattern) and set next_ready, the audio
 * interrupt copies it in at the pattern's end: when its longest track (of those with a step on) comes round. */
typedef struct {
    seq_pattern_t pat;
    uint16_t bpm;                        /* the song's tempo: SEQ_BPM_MIN..SEQ_BPM_MAX, quarter notes a minute; a
                                          * step is a 16th */
    volatile uint8_t pattern;            /* the pattern playing: 0..15 */
    volatile uint8_t next;               /* the one queued, SEQ_NO_PATTERN: none */
    const seq_pattern_t *volatile next_src;
    volatile uint8_t next_ready;
    volatile uint32_t edits, saved;
    uint32_t pat_steps;                  /* steps since the pattern began */
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
uint32_t seq_bpm(const seq_t *s);        /* the tempo playing: the song's, or the clock in's */
/* a pattern's length: its longest track's of those with a step on (SEQ_PAGE if none has) */
uint32_t seq_length(const seq_pattern_t *p);
int seq_has_steps(const seq_pattern_t *p);
/* a pattern copied in to play (src: 0, an empty one), from its first step (the audio interrupt at a pattern's end;
 * the main loop, under plat_lock, while stopped) */
void seq_load(seq_t *s, const seq_pattern_t *src);
int seq_valid(const seq_pattern_t *p);   /* a loaded record that can be played */
/* 0.15's record, read into S1 of a pattern (the others empty); 0 if it is no such record */
int seq_from_v1(const seq_pattern_v1_t *v1, seq_pattern_t *p);
/* a pattern's two records, where they lie in it: OBJ_SEQ's (a seq_record_t) and OBJ_DRUMS' (a seq_drum_record_t) */
static inline const void *seq_record_of(const seq_pattern_t *p) { return p; }
static inline const void *seq_drum_record_of(const seq_pattern_t *p) { return &p->track[SEQ_S_TRACKS]; }
int seq_from_record(const seq_record_t *r, seq_pattern_t *p);          /* the synth tracks, tempo, swing; 0: refused */
int seq_from_drum_record(const seq_drum_record_t *d, seq_pattern_t *p);   /* the drum tracks; 0: refused */
