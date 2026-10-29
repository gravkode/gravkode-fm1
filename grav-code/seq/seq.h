/* GRAV CODE's sequencer: 8 tracks (S1..S4 the synth's, 0.16; D1..D4 the drums', 0.17) of 64 steps in 4 pages of 16,
 * each its own length of 1..64 (polymeters) and swing (0.25; the pattern's, or its own), one tempo, MIDI clock
 * out and in. It runs in the audio
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
    uint8_t swing;                       /* (0.25) its own swing, SEQ_SWING_MIN..MAX; 0 (as saved before): the
                                          * pattern's */
    uint8_t rsv[2];
    seq_step_t step[SEQ_STEPS];
} seq_track_t;

/* (0.35) a parameter lock: a step's own value of one of its track's knobs (SEQ_LK_*), from the step until the track's
 * next step that plays or locks; kept in its pattern's pool, sorted by key */
#define SEQ_LOCKS 512u
enum { SEQ_LK_OSC0, SEQ_LK_OSC1, SEQ_LK_OSC2,   /* OSC's (a drum's DRUM page: TONE, DECAY) */
       SEQ_LK_TUNE,                              /* a drum's TUNE */
       SEQ_LK_SHAPE0, SEQ_LK_SHAPE1, SEQ_LK_SHAPE2,   /* a drum's SHAPE */
       SEQ_LK_VOL, SEQ_LK_SEND,                  /* the mixer's */
       SEQ_LK_PARAMS };
#define SEQ_LOCK_KEY(track, step, param) ((uint16_t)((uint32_t)(track) << 10 | (uint32_t)(step) << 4 | (uint32_t)(param)))
typedef struct {
    uint16_t key;                        /* SEQ_LOCK_KEY */
    uint8_t value;                       /* 0..100 */
    uint8_t rsv;
} seq_lock_t;

typedef struct {
    uint32_t magic;                      /* SEQ_MAGIC */
    uint16_t bpm;                        /* the tempo it was saved with (SEQ_BPM_MIN..MAX); since 0.18 the song's
                                          * tempo is seq_t's, every pattern's */
    uint8_t swing;                       /* SEQ_SWING_MIN..MAX: the first of each pair of steps' share, in %: every
                                          * track's but those with their own (seq_track_t.swing) */
    uint8_t rsv;
    seq_track_t track[SEQ_TRACKS];
    uint32_t drum_magic;                 /* SEQ_MAGIC_DRUMS: the end of the drum tracks' record */
    uint16_t nlocks;                     /* (0.35) its parameter locks; a pattern saved before has none */
    uint16_t lock_rsv;
    seq_lock_t lock[SEQ_LOCKS];
} seq_pattern_t;
#define SEQ_PATTERN_V018 __builtin_offsetof(seq_pattern_t, nlocks)   /* a pattern as 0.18..0.34 saved it */

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

/* a block's events, in the order to apply them: by offset, a note's end before a note's start at the same one (and a
 * step's locks before its notes: SEQ_EV_LOCK, its step in note, 0.35) */
enum { SEQ_EV_OFF = 1, SEQ_EV_STOP, SEQ_EV_START, SEQ_EV_CLOCK, SEQ_EV_LOCK, SEQ_EV_ON,
       SEQ_EV_BEAT };                    /* (0.24) a quarter note begins, while beats is set: the metronome's click
                                          * (note 1: the bar's first) */
typedef struct {
    uint16_t at;                         /* sample offset in the block, a multiple of SEQ_GRID */
    uint8_t type, note, vel, track;      /* a note's track (0..SEQ_TRACKS - 1) */
} seq_event_t;

/* (0.34) a synth track's arpeggiator, its settings its sound's (app.c sets them every block). The notes held (the keys
 * and MIDI in, through seq_arp_note; the track's steps, each for its length) are played one after another, a note
 * every rate ticks of the song (24 a quarter note, as MIDI clock goes: on the step grid while the song plays, on the
 * clock in while one leads), each gate % of that long; stopped, on its own ticks from the first note held. HOLD keeps
 * the keys' notes once all are let go, until the next ones come */
#define SEQ_ARP_NOTES 8u
#define SEQ_ARP_ORDER (2u * SEQ_ARP_NOTES * 4u)   /* its order's notes, at most: 8 notes, 4 octaves, up and down */
enum { SEQ_ARP_OFF, SEQ_ARP_UP, SEQ_ARP_DOWN, SEQ_ARP_UPDOWN, SEQ_ARP_RANDOM, SEQ_ARP_PLAYED, SEQ_ARP_CHORD,
       SEQ_ARP_MODES };
typedef struct {
    volatile uint8_t mode;               /* SEQ_ARP_* (the app's) */
    volatile uint8_t rate;               /* ticks between its notes: 3 (a 32nd) .. 48 (a half) */
    volatile uint8_t octaves;            /* 1..4: its notes, then an octave up, ... */
    volatile uint8_t gate;               /* a note's length, % of rate: 5..100 */
    volatile uint8_t hold;               /* HOLD: 1 */
    volatile uint8_t n;                  /* notes held, in the order they came (the screen's) */
    volatile uint8_t note[SEQ_ARP_NOTES];
    uint8_t vel[SEQ_ARP_NOTES];
    int32_t rel_q8[SEQ_ARP_NOTES];       /* a step's note: when it is let go; SEQ_FAR (seq.c): a key's */
    uint8_t keys;                        /* keys (and MIDI in's notes) held now */
    uint8_t latched;                     /* HOLD: every key let go, their notes kept until the next */
    uint8_t pos;                         /* where it is in its order */
    uint8_t was;                         /* at the last block: 1 on, 2 HOLD (to see them go) */
    volatile uint8_t last;               /* the note it played last (the screen's) */
    volatile uint32_t played;            /* its notes played (the screen's, the tests) */
    int32_t free_q8;                     /* stopped: to its next tick (SEQ_FAR: not running); its ticks so far */
    uint32_t free_ticks;
    uint32_t rnd;
    uint8_t in_n;                        /* this block's steps' notes: held from at_q8, let go at rel_q8 */
    struct { uint8_t note, vel; int32_t at_q8, rel_q8; } in[SEQ_ARP_NOTES];
} seq_arp_t;

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
    volatile uint8_t skip[SEQ_TRACKS];   /* (0.24) a step + 1 a track does not play when it next comes (a note
                                          * recorded live just ahead of it: already heard); 0: none */
    volatile uint8_t beats;              /* (0.24) SEQ_EV_BEAT on every quarter note (the UI: the metronome on) */
    volatile uint8_t mute;               /* (0.26) the tracks muted, a bit each (the mixer): their steps start no notes,
                                          * the ones sounding end */
    volatile uint8_t count_in;           /* (0.25) steps of a count-in still to come before the first (set with
                                          * want = SEQ_WANT_PLAY): only the beats sound, the tracks wait */
    uint8_t start_due;                   /* the count-in over: MIDI start with the next step */
    int32_t pend_q8[SEQ_TRACKS];         /* (0.25) a track's step swung later, still to play: when (SEQ_FAR: none), */
    int32_t pend_gate[SEQ_TRACKS];       /* its notes' length, */
    uint8_t pend_step[SEQ_TRACKS];       /* which step */
    int32_t due_q8;                      /* samples x 256 from this block's start to the next step (SEQ_FAR: none) */
    int32_t clock_q8;                    /* to the next MIDI clock out (24 a quarter note, straight) */
    uint32_t ext_ticks;                  /* MIDI clocks in since the start: every 6th is a step */
    uint32_t ext_quiet;                  /* samples since the last clock in */
    uint32_t win_samples, win_ticks;     /* the clock in's tempo: samples over a beat's 24 ticks */
    int32_t ext_step_q8;                 /* a step's length by the clock in */
    struct { uint8_t note, track; int32_t off_q8; } sounding[SEQ_SOUNDING];
    uint32_t nsounding;
    seq_arp_t arp[SEQ_S_TRACKS];         /* (0.34) the synth tracks' arpeggiators */
    uint8_t ticking;                     /* (0.34) the song's ticks run: its first step played (no count-in left) */
    uint8_t tick_left;                   /* the song's ticks still to come in this step (6 a step), the next's */
    int32_t tick_q8;                     /* time and number */
    uint32_t tick_no;
    uint8_t ext_ticks_n;                 /* clocks in since the last block: the arps' ticks at its start */
    uint32_t ext_tick[4];
} seq_t;

void seq_defaults(seq_pattern_t *p);
void seq_init(seq_t *s);
/* audio interrupt: the events of the next n samples (into ev, at most max), sorted; returns how many */
uint32_t seq_block(seq_t *s, uint32_t n, seq_event_t *ev, uint32_t max);
void seq_midi_clock(seq_t *s);           /* audio interrupt, MIDI in: a clock (F8) */
void seq_midi_start(seq_t *s, int cont); /* audio interrupt, MIDI in: start (FA) or continue (FB); stop (FC) is
                                          * want = SEQ_WANT_STOP */
uint32_t seq_bpm(const seq_t *s);        /* the tempo playing: the song's, or the clock in's */
/* (0.25) a track's swing: its own, or the pattern's */
static inline uint32_t seq_track_swing(const seq_pattern_t *p, uint32_t track)
{
    return p->track[track].swing ? p->track[track].swing : p->swing;
}
/* (0.24) live recording, under plat_lock: the step of a track a note played now goes to, quantized: the one playing
 * while in its first half, else the next (*ahead = 1: it has not played yet); -1 when not playing */
int32_t seq_quantize(const seq_t *s, uint32_t track, int *ahead);
/* a pattern's length: its longest track's of those with a step on (SEQ_PAGE if none has) */
uint32_t seq_length(const seq_pattern_t *p);
int seq_has_steps(const seq_pattern_t *p);
/* a pattern copied in to play (src: 0, an empty one), from its first step (the audio interrupt at a pattern's end;
 * the main loop, under plat_lock, while stopped) */
void seq_load(seq_t *s, const seq_pattern_t *src);
int seq_valid(const seq_pattern_t *p);   /* a loaded record that can be played (its steps: as 0.18 saved it) */
int seq_locks_valid(const seq_pattern_t *p);   /* (0.35) its locks: sorted, each where a step can be */
/* (0.35) the main loop, under plat_lock: a step's lock of param, its value (-1: none); set (value -1: removed; -1
 * returned: the pool full); a step's locks gone (every step's of a track: step -1); the ones it has, a bit each,
 * their values into val (SEQ_LK_PARAMS) */
int seq_lock_get(const seq_pattern_t *p, uint32_t track, uint32_t step, uint32_t param);
int seq_lock_set(seq_pattern_t *p, uint32_t track, uint32_t step, uint32_t param, int32_t value);
void seq_lock_clear(seq_pattern_t *p, uint32_t track, int32_t step);
uint32_t seq_lock_mask(const seq_pattern_t *p, uint32_t track, uint32_t step, uint8_t *val);
/* 0.15's record, read into S1 of a pattern (the others empty); 0 if it is no such record */
int seq_from_v1(const seq_pattern_v1_t *v1, seq_pattern_t *p);
/* a pattern's two records, where they lie in it: OBJ_SEQ's (a seq_record_t) and OBJ_DRUMS' (a seq_drum_record_t) */
static inline const void *seq_record_of(const seq_pattern_t *p) { return p; }
static inline const void *seq_drum_record_of(const seq_pattern_t *p) { return &p->track[SEQ_S_TRACKS]; }
int seq_from_record(const seq_record_t *r, seq_pattern_t *p);          /* the synth tracks, tempo, swing; 0: refused */
int seq_from_drum_record(const seq_drum_record_t *d, seq_pattern_t *p);   /* the drum tracks; 0: refused */
/* (0.34) audio interrupt: a note from the keys or MIDI in for a synth track's arpeggiator (vel 0: let go), from the
 * next block's start; 1 if it took it (its arp on; a let-go: a note it holds), 0: to play as it is */
int seq_arp_note(seq_t *s, uint32_t track, uint8_t note, uint8_t vel);
/* (0.34) an arp's order: the notes held, low to high (AS PLAYED: as they came), each octave up as many as it has; DOWN
 * the other way, UP-DOWN up and back (the ends once): into out (SEQ_ARP_ORDER at most); how many */
uint32_t seq_arp_order(const seq_arp_t *a, uint8_t *out);
/* (0.34) a synth track's arp on (its mode not OFF) */
static inline int seq_arp_on(const seq_t *s, uint32_t track)
{
    return track < SEQ_S_TRACKS && s->arp[track].mode != SEQ_ARP_OFF && s->arp[track].mode < SEQ_ARP_MODES;
}
