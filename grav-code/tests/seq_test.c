/* The sequencer's timing, alone (seq/seq.c): where its events land in 256-sample blocks. Build and run:
 *   cc -std=c99 -O2 -I grav-code grav-code/tests/seq_test.c grav-code/seq/seq.c -o /tmp/seq_test && /tmp/seq_test
 * (tests/run_seq_test.py does it). */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "seq/seq.h"

#define BLOCK 256u
static int fails;

#define CHECK(c, ...) do { if (!(c)) { printf("FAIL %s:%d: ", __FILE__, __LINE__); printf(__VA_ARGS__); \
    printf("\n"); fails++; } } while (0)

typedef struct { uint32_t t; seq_event_t e; } timed_t;
static timed_t log_[4096];
static uint32_t nlog, now;               /* the log, and the samples run since the scenario began */

static void run(seq_t *s, uint32_t blocks)
{
    seq_event_t ev[SEQ_EVENTS];
    for (uint32_t b = 0; b < blocks; b++) {
        uint32_t n = seq_block(s, BLOCK, ev, SEQ_EVENTS);
        for (uint32_t i = 0; i < n && nlog < 4096; i++) {
            CHECK(ev[i].at < BLOCK && ev[i].at % SEQ_GRID == 0, "event at %u", ev[i].at);
            CHECK(i == 0 || ev[i - 1].at <= ev[i].at, "events out of order");
            log_[nlog].t = now + ev[i].at;
            log_[nlog].e = ev[i];
            nlog++;
        }
        now += BLOCK;
    }
}

static uint32_t grid(double samples)     /* where an event at that time lands: its block, then the grid */
{
    uint32_t t = (uint32_t)samples, block = t / BLOCK * BLOCK;
    return block + (t - block) / SEQ_GRID * SEQ_GRID;
}

static uint32_t find(uint8_t type, uint32_t nth)   /* the nth event of a type: its time (or ~0) */
{
    for (uint32_t i = 0; i < nlog; i++)
        if (log_[i].e.type == type && nth-- == 0)
            return log_[i].t;
    return ~0u;
}

static uint32_t count(uint8_t type)
{
    uint32_t c = 0;
    for (uint32_t i = 0; i < nlog; i++)
        c += log_[i].e.type == type;
    return c;
}

static void fresh(seq_t *s)
{
    seq_init(s);
    nlog = 0;
    now = 0;
    for (uint32_t i = 0; i < SEQ_PAGE; i++)
        s->pat.track[0].step[i].on = 1;
}

int main(void)
{
    static seq_t s;
    const double step = 44100.0 * 60.0 / 120.0 / 4.0;   /* 5512.5 samples at 120 BPM */

    /* straight: a step every 5512.5 samples, on the 24-sample grid; MIDI start, 6 clocks a step */
    fresh(&s);
    s.want = SEQ_WANT_PLAY;
    run(&s, 400);                        /* 102400 samples: 18.6 steps */
    CHECK(count(SEQ_EV_START) == 1, "start sent once");
    for (uint32_t k = 0; k < 16; k++)
        CHECK(find(SEQ_EV_ON, k) == grid(k * step), "straight step %u at %u, not %u", k, find(SEQ_EV_ON, k),
              grid(k * step));
    CHECK(count(SEQ_EV_ON) == 19, "19 steps in 102400 samples, got %u", count(SEQ_EV_ON));
    {
        uint32_t clocks = count(SEQ_EV_CLOCK);
        CHECK(clocks >= 111 && clocks <= 112, "6 clocks a step: %u", clocks);
    }
    /* a step's note: ends an eighth of a step before the next */
    CHECK(find(SEQ_EV_OFF, 0) == grid(step - step / 8), "gate end at %u", find(SEQ_EV_OFF, 0));

    /* swing 75: the second of each pair starts three quarters of the pair in */
    fresh(&s);
    s.pat.swing = 75;
    s.want = SEQ_WANT_PLAY;
    run(&s, 200);
    for (uint32_t k = 0; k < 8; k++) {
        double at = (k / 2) * 2 * step + ((k & 1) ? 1.5 * step : 0.0);
        CHECK(find(SEQ_EV_ON, k) == grid(at), "swing step %u at %u, not %u", k, find(SEQ_EV_ON, k), grid(at));
    }

    /* the length: steps past it do not play; a length of 3 loops 0, 1, 2 */
    fresh(&s);
    s.pat.track[0].length = 3;
    s.pat.track[0].step[1].on = 0;
    s.pat.track[0].step[1].note[0] = 61;
    s.pat.track[0].step[2].note[0] = 62;
    s.want = SEQ_WANT_PLAY;
    run(&s, 140);                        /* 6.5 steps: 0 (1 off) 2 0 (1) 2 0 */
    CHECK(count(SEQ_EV_ON) == 5, "length 3: 5 notes, got %u", count(SEQ_EV_ON));
    {
        const uint8_t want[5] = {60, 62, 60, 62, 60};
        uint32_t k = 0;
        for (uint32_t i = 0; i < nlog && k < 5; i++)
            if (log_[i].e.type == SEQ_EV_ON) {
                CHECK(log_[i].e.note == want[k], "length 3 note %u: %u", k, log_[i].e.note);
                k++;
            }
    }

    /* a chord, held two steps; the same note again ends the one sounding first */
    fresh(&s);
    s.pat.track[0].step[0].n = 3;
    s.pat.track[0].step[0].note[1] = 64;
    s.pat.track[0].step[0].note[2] = 67;
    s.pat.track[0].step[0].len = 2;
    s.pat.track[0].step[1].on = 0;
    s.want = SEQ_WANT_PLAY;
    run(&s, 60);                         /* 15360 samples: past step 2 (11025) */
    CHECK(find(SEQ_EV_ON, 2) == 0 && find(SEQ_EV_ON, 3) == grid(2 * step), "a chord at 0, the next note at step 2");
    CHECK(find(SEQ_EV_OFF, 0) == grid(2 * step - step / 8), "the chord ends after 2 steps: %u", find(SEQ_EV_OFF, 0));

    /* stop: every note sounding ends, MIDI stop goes out, nothing more plays */
    fresh(&s);
    s.pat.track[0].step[0].len = 8;
    s.want = SEQ_WANT_PLAY;
    run(&s, 10);
    s.want = SEQ_WANT_STOP;
    nlog = 0;
    run(&s, 100);
    CHECK(count(SEQ_EV_STOP) == 1 && count(SEQ_EV_OFF) == 1 && count(SEQ_EV_ON) == 0 && !s.playing,
          "stop: one off, one MIDI stop, no more notes");

    /* the clock in: started by MIDI start, a step every 6 clocks, its tempo measured; swing on the clock in */
    fresh(&s);
    s.pat.swing = 75;
    {
        const uint32_t tick = 1024;      /* samples a clock: 107.7 BPM */
        uint32_t t = 0, next = 0;
        seq_event_t ev[SEQ_EVENTS];
        seq_midi_start(&s, 0);
        for (uint32_t b = 0; b < 600; b++) {
            while (next < t + BLOCK) {   /* the clocks that came in before this block (at its start) */
                seq_midi_clock(&s);
                next += tick;
            }
            uint32_t n = seq_block(&s, BLOCK, ev, SEQ_EVENTS);
            for (uint32_t i = 0; i < n && nlog < 4096; i++) {
                log_[nlog].t = t + ev[i].at;
                log_[nlog].e = ev[i];
                nlog++;
            }
            t += BLOCK;
        }
        CHECK(s.ext, "following the clock in");
        CHECK(count(SEQ_EV_CLOCK) == 0 && count(SEQ_EV_START) == 0, "no clock out while following one in");
        CHECK(find(SEQ_EV_ON, 0) == 0, "the first step on the first clock: %u", find(SEQ_EV_ON, 0));
        CHECK(find(SEQ_EV_ON, 2) == grid(12 * tick), "step 2 on clock 12: %u, not %u", find(SEQ_EV_ON, 2),
              grid(12 * tick));
        {
            uint32_t bpm = seq_bpm(&s);
            CHECK(bpm >= 106 && bpm <= 109, "the clock in's tempo: %u BPM", bpm);
        }
        /* swung: the second of a pair later than its clock (by half a step at 75 %), late steps once measured */
        uint32_t at = find(SEQ_EV_ON, 21), on_clock = 21 * 6 * tick;
        CHECK(at > on_clock + 2 * tick && at < on_clock + 4 * tick, "swung on the clock in: step 21 at %u, clock %u",
              at, on_clock);
    }

    /* the clock in stops: half a second later the tempo is ours again, and the steps go on */
    {
        seq_event_t ev[SEQ_EVENTS];
        uint32_t ons = 0;
        for (uint32_t b = 0; b < 400; b++) {
            uint32_t n = seq_block(&s, BLOCK, ev, SEQ_EVENTS);
            for (uint32_t i = 0; i < n; i++)
                ons += ev[i].type == SEQ_EV_ON;
        }
        CHECK(!s.ext && s.playing && ons > 5, "on our own tempo again: ext %u, %u notes", s.ext, ons);
    }

    /* tracks: each its own length (polymeter), each event its track; S2 (length 3) and D2 (length 5, its steps 1 and
     * 3: a snare's note) beside S1 (length 4) */
    fresh(&s);
    for (uint32_t i = 0; i < 4; i++)
        s.pat.track[0].step[i].note[0] = (uint8_t)(60 + i);
    s.pat.track[0].length = 4;
    s.pat.track[1].length = 3;
    for (uint32_t i = 0; i < 3; i++) {
        s.pat.track[1].step[i].on = 1;
        s.pat.track[1].step[i].note[0] = (uint8_t)(40 + i);
    }
    s.pat.track[5].length = 5;
    s.pat.track[5].step[0].on = s.pat.track[5].step[2].on = 1;
    s.pat.track[5].step[0].note[0] = s.pat.track[5].step[2].note[0] = 38;
    s.want = SEQ_WANT_PLAY;
    run(&s, 260);                        /* 66560 samples: 12 steps */
    {
        uint32_t k0 = 0, k1 = 0, k5 = 0;
        for (uint32_t i = 0; i < nlog; i++) {
            if (log_[i].e.type != SEQ_EV_ON)
                continue;
            if (log_[i].e.track == 0) {
                CHECK(log_[i].e.note == 60 + k0 % 4, "S1 step %u: note %u", k0, log_[i].e.note);
                k0++;
            } else if (log_[i].e.track == 1) {
                CHECK(log_[i].e.note == 40 + k1 % 3, "S2 step %u: note %u", k1, log_[i].e.note);
                k1++;
            } else if (log_[i].e.track == 5) {
                CHECK(log_[i].e.note == 38, "D2: note %u", log_[i].e.note);
                k5++;
            } else
                CHECK(0, "a note on track %u", log_[i].e.track);
        }
        CHECK(k0 == 13 && k1 == 13, "both tracks every step: %u %u", k0, k1);
        CHECK(k5 == 6, "D2 on its steps 1 and 3 of 5 (steps 1 3 6 8 11 13): %u", k5);
        CHECK(s.step[0] == 0 && s.step[1] == 0 && s.step[5] == 2 && s.hits[0] == 13 && s.hits[1] == 13 &&
              s.hits[5] == 6, "after 13 steps: S1 at %d, S2 at %d, D2 at %d", s.step[0], s.step[1], s.step[5]);
    }

    /* 0.18, the patterns: one queued comes in at the pattern's end (its longest track with steps: S1's 4 here; D1's
     * 64, all off, don't count), once the one playing is saved (edits == saved); until then it plays on */
    {
        static seq_pattern_t next;
        seq_defaults(&next);
        CHECK(seq_length(&next) == SEQ_PAGE && !seq_has_steps(&next), "an empty pattern: 16 steps long");
        next.track[0].length = 2;
        next.track[0].step[0].on = next.track[0].step[1].on = 1;
        next.track[0].step[0].note[0] = next.track[0].step[1].note[0] = 70;
        fresh(&s);
        s.pat.track[0].length = 4;
        s.pat.track[4].length = 64;
        CHECK(seq_length(&s.pat) == 4, "the pattern's length: its longest track with steps, %u", seq_length(&s.pat));
        s.next = 3;
        s.next_src = &next;
        s.edits = 1;                     /* unsaved: it waits */
        s.next_ready = 1;
        s.want = SEQ_WANT_PLAY;
        run(&s, 140);                    /* 35840 samples: 7 steps */
        CHECK(s.pattern == 0 && s.next == 3 && count(SEQ_EV_ON) == 7, "unsaved: no switch at its end (%u, %u notes)",
              s.pattern, count(SEQ_EV_ON));
        s.saved = s.edits;               /* saved: in at the next end, step 9 */
        run(&s, 120);                    /* to 66560 samples: 13 steps */
        {
            uint32_t k = 0;
            for (uint32_t i = 0; i < nlog; i++)
                if (log_[i].e.type == SEQ_EV_ON) {
                    CHECK(log_[i].e.note == (k < 8 ? 60 : 70), "step %u: note %u", k, log_[i].e.note);
                    CHECK(k != 8 || log_[i].t == grid(8 * step), "the switch on step 9's time: %u", log_[i].t);
                    k++;
                }
            CHECK(k == 13, "13 steps: %u", k);
        }
        CHECK(s.pattern == 3 && s.next == SEQ_NO_PATTERN && !s.next_ready && s.pat.track[0].length == 2,
              "pattern 3 playing: %u", s.pattern);
        /* polymeter: the end when the longest track with steps comes round (S2's 6 beside S1's 4) */
        s.pat.track[1].length = 6;
        s.pat.track[1].step[2].on = 1;
        CHECK(seq_length(&s.pat) == 6, "S1 4, S2 6: 6 steps, %u", seq_length(&s.pat));
        seq_load(&s, 0);
        CHECK(!seq_has_steps(&s.pat) && s.step[0] == -1 && s.pat_steps == 0, "an empty pattern loaded");
        /* the tempo is the song's, whichever pattern plays */
        fresh(&s);
        s.bpm = 60;
        s.pat.bpm = 200;
        s.want = SEQ_WANT_PLAY;
        run(&s, 100);
        CHECK(find(SEQ_EV_ON, 1) == grid(2.0 * step), "the song's tempo, not the pattern's: %u", find(SEQ_EV_ON, 1));
    }

    /* a record saved, read back; 0.15's (one track) read into S1 */
    {
        seq_pattern_t p;
        static seq_pattern_v1_t v1;
        seq_defaults(&p);
        CHECK(seq_valid(&p), "the defaults are valid");
        p.track[2].length = 0;
        CHECK(!seq_valid(&p), "length 0 refused");
        v1.magic = SEQ_MAGIC_V1;
        v1.bpm = 97;
        v1.swing = 61;
        v1.length = 24;
        for (uint32_t i = 0; i < SEQ_STEPS; i++) {
            v1.step[i] = p.track[0].step[i];
            v1.step[i].on = i == 5;
        }
        CHECK(seq_from_v1(&v1, &p) && seq_valid(&p) && p.bpm == 97 && p.swing == 61 && p.track[0].length == 24 &&
              p.track[0].step[5].on && !p.track[1].step[5].on && p.track[1].length == 16, "0.15's pattern into S1");
        v1.magic = 0;
        CHECK(!seq_from_v1(&v1, &p), "not a 0.15 record: refused");
    }

    /* 0.17: the synth tracks (tempo, swing) and the drum tracks in two records (a store object: 3840 bytes at most) */
    {
        static seq_pattern_t p, q;
        static seq_record_t r;
        static seq_drum_record_t d;
        CHECK(sizeof r <= 3840u && sizeof d <= 3840u, "the records fit the store: %u %u", (unsigned)sizeof r,
              (unsigned)sizeof d);
        seq_defaults(&p);
        p.bpm = 133;
        p.swing = 58;
        p.track[1].length = 7;
        p.track[1].step[3].on = 1;
        p.track[5].length = 12;
        p.track[5].step[9].on = 1;
        p.track[5].step[9].note[0] = 38;
        memcpy(&r, seq_record_of(&p), sizeof r);   /* (as the app saves them: where they lie in the pattern) */
        memcpy(&d, seq_drum_record_of(&p), sizeof d);
        CHECK(r.magic == SEQ_MAGIC && r.bpm == 133 && r.track[1].length == 7 && d.magic == SEQ_MAGIC_DRUMS &&
              d.track[1].length == 12 && d.track[1].step[9].note[0] == 38, "the records in the pattern");
        seq_defaults(&q);
        CHECK(seq_from_record(&r, &q) && seq_from_drum_record(&d, &q) && !memcmp(&p, &q, sizeof p),
              "both records back into the pattern");
        d.track[2].length = 0;
        CHECK(!seq_from_drum_record(&d, &q) && q.track[6].length == 16, "a bad drum record refused, the drums kept");
        r.magic = 0;
        CHECK(!seq_from_record(&r, &q) && q.bpm == 133, "not a record: refused");
    }

    /* (0.24) live recording's quantizing: the step playing in its first half, the next in its second; stopped, none */
    {
        int ahead = 7;
        fresh(&s);
        CHECK(seq_quantize(&s, 0, &ahead) == -1 && ahead == 0, "stopped: no step");
        s.want = SEQ_WANT_PLAY;
        run(&s, 1);                      /* step 0 began at 0; 256 samples in */
        CHECK(seq_quantize(&s, 0, &ahead) == 0 && !ahead, "early in step 0: step 0");
        run(&s, 12);                     /* 3328 samples: past step 0's half (2756) */
        CHECK(seq_quantize(&s, 0, &ahead) == 1 && ahead, "late in step 0: step 1, ahead");
        s.pat.track[0].length = 1;       /* a track of one step: late, the same again */
        CHECK(seq_quantize(&s, 0, &ahead) == 0 && ahead, "length 1: step 0 again, ahead");
    }
    /* the skip: the next step (+ 1) not played as it comes, once (any other step comes first: none); the beats: a quarter note each 4 steps, the bar's
     * first marked, only while beats is set */
    fresh(&s);
    s.want = SEQ_WANT_PLAY;
    s.beats = 1;
    s.skip[0] = 3;                       /* (a start clears it) */
    run(&s, 1);
    CHECK(s.skip[0] == 0, "a start: no skip");
    s.skip[0] = 2;                       /* step 1, the next, set while playing */
    run(&s, 399);                        /* 18.6 steps in all */
    CHECK(count(SEQ_EV_ON) == 18 && s.skip[0] == 0, "step 1 skipped once: %u notes", count(SEQ_EV_ON));
    CHECK(find(SEQ_EV_ON, 1) == grid(2 * step), "the note after step 0's is step 2's: %u", find(SEQ_EV_ON, 1));
    CHECK(count(SEQ_EV_BEAT) == 5, "5 beats in 18.6 steps: %u", count(SEQ_EV_BEAT));
    for (uint32_t k = 0; k < 5; k++)
        CHECK(find(SEQ_EV_BEAT, k) == grid(4 * k * step), "beat %u at %u", k, find(SEQ_EV_BEAT, k));
    {
        uint32_t marked = 0;
        for (uint32_t i = 0; i < nlog; i++)
            if (log_[i].e.type == SEQ_EV_BEAT)
                marked += log_[i].e.note;
        CHECK(marked == 2, "the bars' first beats (0 and 16): %u", marked);
    }
    fresh(&s);
    s.want = SEQ_WANT_PLAY;
    run(&s, 100);
    CHECK(count(SEQ_EV_BEAT) == 0, "no beats unless asked");

    /* (0.25) a track's own swing: track 1 at 75 % on a straight pattern: its second of each pair three quarters of the
     * pair in, track 0's on the grid; its notes' end swung too; a bad swing refused */
    fresh(&s);
    for (uint32_t i = 0; i < SEQ_PAGE; i++) {
        s.pat.track[1].step[i].on = 1;
        s.pat.track[1].step[i].note[0] = 72;
    }
    s.pat.track[1].swing = 75;
    s.want = SEQ_WANT_PLAY;
    run(&s, 200);
    {
        uint32_t k0 = 0, k1 = 0;
        for (uint32_t i = 0; i < nlog; i++) {
            if (log_[i].e.type != SEQ_EV_ON)
                continue;
            if (log_[i].e.track == 0 && k0 < 8) {
                CHECK(log_[i].t == grid(k0 * step), "straight track: step %u at %u", k0, log_[i].t);
                k0++;
            } else if (log_[i].e.track == 1 && k1 < 8) {
                double at = (k1 / 2) * 2 * step + ((k1 & 1) ? 1.5 * step : 0.0);
                CHECK(log_[i].t == grid(at), "swung track: step %u at %u, not %u", k1, log_[i].t, grid(at));
                k1++;
            }
        }
        CHECK(k0 == 8 && k1 == 8, "both tracks played: %u %u", k0, k1);
        uint32_t offs = 0;                   /* track 1's first note (step 0) ends where step 1 starts, less an eighth */
        for (uint32_t i = 0; i < nlog && !offs; i++)
            if (log_[i].e.type == SEQ_EV_OFF && log_[i].e.track == 1)
                offs = log_[i].t;
        CHECK(offs == grid(1.5 * step - step / 8), "a swung track's gate: %u, not %u", offs, grid(1.5 * step - step / 8));
    }
    {
        static seq_pattern_t p;
        seq_defaults(&p);
        p.track[2].swing = 49;
        CHECK(!seq_valid(&p), "a track's swing below 50 refused");
        p.track[2].swing = 0;
        CHECK(seq_valid(&p) && seq_track_swing(&p, 2) == p.swing, "0: the pattern's swing");
    }

    /* (0.25) a count-in: a bar of beats (4, the first marked), no notes, no MIDI start; then MIDI start and the first
     * step on the next bar's first beat; quantizing waits for the count-in's last half step */
    fresh(&s);
    s.count_in = 16;
    s.want = SEQ_WANT_PLAY;
    run(&s, 1);
    {
        int ahead = 0;
        CHECK(seq_quantize(&s, 0, &ahead) == -1, "counting in: nothing recorded yet");
    }
    run(&s, 320);                        /* 82176 samples: in the count-in's 15th step */
    {
        int ahead = 0;
        CHECK(seq_quantize(&s, 0, &ahead) == -1, "counting in, a step before the last: nothing recorded");
    }
    run(&s, 10);                         /* 84736: early in its last step (82687..88200) */
    CHECK(count(SEQ_EV_ON) == 0 && count(SEQ_EV_START) == 0 && count(SEQ_EV_BEAT) == 4, "a count-in: 4 beats only (%u)",
          count(SEQ_EV_BEAT));
    {
        int ahead = 0;
        CHECK(seq_quantize(&s, 0, &ahead) == -1, "early in the count-in's last step: nothing");
        run(&s, 6);                      /* 86272: late in it */
        CHECK(seq_quantize(&s, 0, &ahead) == 0 && ahead, "late in the count-in's last step: the first step, ahead");
    }
    run(&s, 24);
    CHECK(find(SEQ_EV_START, 0) == grid(16 * step) && find(SEQ_EV_ON, 0) == grid(16 * step) &&
          find(SEQ_EV_BEAT, 3) == grid(12 * step), "the first step after a bar: %u", find(SEQ_EV_ON, 0));
    CHECK(s.count_in == 0, "the count-in over");

    if (fails) {
        printf("%d failures\n", fails);
        return 1;
    }
    /* (0.34) the arp: S1's chord step (C E G, 4 steps long) held for its length, UP over two octaves at a 16th: a note
     * on each step, on the step grid, C E G C5, each half a step long (GATE 50), none after the step lets go */
    {
        static const uint8_t up[4] = {60, 64, 67, 72};
        fresh(&s);
        for (uint32_t i = 0; i < SEQ_PAGE; i++)
            s.pat.track[0].step[i].on = 0;
        seq_step_t *c = &s.pat.track[0].step[0];
        c->on = 1, c->n = 3, c->note[0] = 60, c->note[1] = 64, c->note[2] = 67, c->len = 4;
        s.arp[0].mode = SEQ_ARP_UP, s.arp[0].rate = 6, s.arp[0].octaves = 2, s.arp[0].gate = 50;
        s.want = SEQ_WANT_PLAY;
        run(&s, 100);                    /* 4.6 steps */
        CHECK(count(SEQ_EV_ON) == 4, "arp: 4 notes over the chord's 4 steps, got %u", count(SEQ_EV_ON));
        for (uint32_t k = 0; k < 4; k++) {
            uint32_t at = find(SEQ_EV_ON, k), i;
            for (i = 0; i < nlog && !(log_[i].e.type == SEQ_EV_ON && log_[i].t == at); i++) {}
            CHECK(at == grid(k * step), "arp note %u at %u, not %u", k, at, grid(k * step));
            CHECK(i < nlog && log_[i].e.note == up[k] && log_[i].e.track == 0, "arp note %u: %u", k, log_[i].e.note);
            CHECK(find(SEQ_EV_OFF, k) == grid(k * step + step / 2), "arp gate %u ends at %u", k, find(SEQ_EV_OFF, k));
        }
        CHECK(s.arp[0].played == 4 && s.hits[0] == 4, "arp: played %u, hits %u", s.arp[0].played, s.hits[0]);
    }
    /* DOWN at a 32nd (3 ticks): G E C G E C ..., two a step; UP-DOWN: C E G E C E ... */
    {
        static const uint8_t want_down[6] = {67, 64, 60, 67, 64, 60}, want_ud[6] = {60, 64, 67, 64, 60, 64};
        for (uint32_t m = 0; m < 2; m++) {
            fresh(&s);
            for (uint32_t i = 0; i < SEQ_PAGE; i++)
                s.pat.track[0].step[i].on = 0;
            seq_step_t *c = &s.pat.track[0].step[0];
            c->on = 1, c->n = 3, c->note[0] = 60, c->note[1] = 64, c->note[2] = 67, c->len = 8;
            s.arp[0].mode = m ? SEQ_ARP_UPDOWN : SEQ_ARP_DOWN, s.arp[0].rate = 3, s.arp[0].octaves = 1;
            s.arp[0].gate = 50;
            s.want = SEQ_WANT_PLAY;
            run(&s, 70);                 /* 3.25 steps: 7 notes */
            uint32_t k = 0;
            for (uint32_t i = 0; i < nlog && k < 6; i++)
                if (log_[i].e.type == SEQ_EV_ON) {
                    CHECK(log_[i].t == grid(k * step / 2), "arp %s note %u at %u", m ? "up-down" : "down", k, log_[i].t);
                    CHECK(log_[i].e.note == (m ? want_ud : want_down)[k], "arp %s note %u: %u", m ? "up-down" : "down",
                          k, log_[i].e.note);
                    k++;
                }
            CHECK(k == 6, "arp %s: 6 notes", m ? "up-down" : "down");
        }
    }
    /* stopped: the keys' notes (seq_arp_note) on its own ticks from the first, a 16th apart at the song's tempo; let go,
     * nothing more; a let-go of a note it does not hold is not its */
    fresh(&s);
    for (uint32_t i = 0; i < SEQ_PAGE; i++)
        s.pat.track[0].step[i].on = 0;
    s.arp[1].mode = SEQ_ARP_UP, s.arp[1].rate = 6, s.arp[1].octaves = 1, s.arp[1].gate = 50;
    CHECK(seq_arp_note(&s, 1, 64, 90) == 1 && seq_arp_note(&s, 1, 60, 80) == 1, "arp: the keys' notes taken");
    CHECK(seq_arp_note(&s, 2, 60, 80) == 0, "no arp on S3: its note not taken");
    run(&s, 60);                         /* 2.8 steps: C at 0, E, C */
    CHECK(count(SEQ_EV_ON) == 3, "arp stopped: 3 notes, got %u", count(SEQ_EV_ON));
    CHECK(find(SEQ_EV_ON, 0) == 0 && find(SEQ_EV_ON, 1) == grid(step) && find(SEQ_EV_ON, 2) == grid(2 * step),
          "arp stopped: on its own ticks (%u %u %u)", find(SEQ_EV_ON, 0), find(SEQ_EV_ON, 1), find(SEQ_EV_ON, 2));
    CHECK(log_[0].e.note == 60 && log_[0].e.vel == 80 && log_[0].e.track == 1, "arp stopped: C first, its velocity");
    CHECK(seq_arp_note(&s, 1, 62, 0) == 0, "arp: a let-go of a note not held is not its");
    seq_arp_note(&s, 1, 60, 0);
    seq_arp_note(&s, 1, 64, 0);
    {
        const uint32_t before = count(SEQ_EV_ON);
        run(&s, 60);
        CHECK(count(SEQ_EV_ON) == before && s.nsounding == 0, "arp: let go, nothing more, nothing sounding");
    }
    /* HOLD: the notes kept once the keys are let go; the next key starts anew; CHORD: all its notes at once */
    s.arp[1].hold = 1;
    seq_arp_note(&s, 1, 60, 100);
    seq_arp_note(&s, 1, 67, 100);
    seq_arp_note(&s, 1, 60, 0);
    seq_arp_note(&s, 1, 67, 0);
    CHECK(s.arp[1].n == 2 && s.arp[1].latched, "arp HOLD: kept (%u)", s.arp[1].n);
    seq_arp_note(&s, 1, 72, 100);
    CHECK(s.arp[1].n == 1 && s.arp[1].note[0] == 72 && !s.arp[1].latched, "arp HOLD: the next key anew");
    s.arp[1].mode = SEQ_ARP_CHORD;
    seq_arp_note(&s, 1, 76, 100);
    nlog = 0;
    run(&s, 30);
    CHECK(count(SEQ_EV_ON) >= 2 && find(SEQ_EV_ON, 0) == find(SEQ_EV_ON, 1), "arp CHORD: its notes at once");
    /* turned off: its notes end at once, nothing more; HOLD off lets the kept notes go */
    s.arp[1].mode = SEQ_ARP_OFF;
    nlog = 0;
    run(&s, 30);
    CHECK(count(SEQ_EV_ON) == 0 && s.nsounding == 0 && s.arp[1].n == 0, "arp off: silent, nothing held");
    /* on the clock in: a note every 6 clocks */
    fresh(&s);
    for (uint32_t i = 0; i < SEQ_PAGE; i++)
        s.pat.track[0].step[i].on = 0;
    s.arp[0].mode = SEQ_ARP_UP, s.arp[0].rate = 6, s.arp[0].octaves = 1, s.arp[0].gate = 50;
    seq_arp_note(&s, 0, 60, 100);
    seq_arp_note(&s, 0, 64, 100);
    run(&s, 1);
    seq_midi_start(&s, 0);
    nlog = 0;
    for (uint32_t c = 0; c < 24; c++) {  /* a quarter note's clocks, one a block */
        seq_midi_clock(&s);
        run(&s, 1);
    }
    CHECK(count(SEQ_EV_ON) == 4, "arp on the clock in: 4 notes in 24 clocks, got %u", count(SEQ_EV_ON));

    /* (0.35) locks: set, read, kept sorted, a step's cleared, a track's cleared; the pool full */
    {
        static seq_pattern_t p;
        uint8_t val[SEQ_LK_PARAMS];
        seq_defaults(&p);
        CHECK(seq_lock_set(&p, 3, 10, SEQ_LK_OSC2, 40) == 0 && seq_lock_set(&p, 0, 1, SEQ_LK_VOL, 90) == 0 &&
              seq_lock_set(&p, 3, 10, SEQ_LK_OSC0, 5) == 0 && seq_lock_set(&p, 3, 10, SEQ_LK_OSC2, 41) == 0,
              "locks set");
        CHECK(p.nlocks == 3 && seq_locks_valid(&p), "locks kept sorted, one each: %u", p.nlocks);
        CHECK(seq_lock_get(&p, 3, 10, SEQ_LK_OSC2) == 41 && seq_lock_get(&p, 3, 11, SEQ_LK_OSC2) == -1, "lock read");
        CHECK(seq_lock_mask(&p, 3, 10, val) == (1u << SEQ_LK_OSC0 | 1u << SEQ_LK_OSC2) && val[SEQ_LK_OSC0] == 5,
              "a step's locks");
        seq_lock_set(&p, 3, 10, SEQ_LK_OSC0, -1);
        CHECK(seq_lock_mask(&p, 3, 10, 0) == 1u << SEQ_LK_OSC2, "a lock removed");
        seq_lock_set(&p, 3, 20, SEQ_LK_SEND, 0);
        seq_lock_clear(&p, 3, 10);
        CHECK(seq_lock_get(&p, 3, 10, SEQ_LK_OSC2) == -1 && seq_lock_get(&p, 3, 20, SEQ_LK_SEND) == 0, "a step's cleared");
        seq_lock_clear(&p, 3, -1);
        CHECK(p.nlocks == 1 && seq_lock_get(&p, 0, 1, SEQ_LK_VOL) == 90, "a track's cleared");
        uint32_t n = 0;
        for (uint32_t t = 0; t < SEQ_TRACKS; t++)
            for (uint32_t st = 0; st < SEQ_STEPS; st++)
                n += seq_lock_set(&p, t, st, SEQ_LK_OSC1, 50) == 0;
        CHECK(n == SEQ_LOCKS - 1u && p.nlocks == SEQ_LOCKS && seq_lock_set(&p, 7, 63, SEQ_LK_SEND, 1) == -1,
              "the pool full at %u", SEQ_LOCKS);
        p.nlocks = 600;
        CHECK(!seq_locks_valid(&p), "a pool past its size refused");
    }
    /* a step's locks reach the render before its notes (at its time); a step off with locks: its locks alone; a step
     * playing without any: a lock event all the same (the track's knobs again) */
    fresh(&s);
    for (uint32_t i = 0; i < SEQ_PAGE; i++)
        s.pat.track[0].step[i].on = 0;
    s.pat.track[0].step[0].on = 1;
    s.pat.track[0].step[2].on = 1;
    seq_lock_set(&s.pat, 0, 0, SEQ_LK_OSC0, 20);
    seq_lock_set(&s.pat, 0, 1, SEQ_LK_OSC0, 80);   /* (step 1 off: a lock without a note) */
    s.want = SEQ_WANT_PLAY;
    run(&s, 70);                         /* 3.25 steps */
    CHECK(count(SEQ_EV_LOCK) == 3 && count(SEQ_EV_ON) == 2, "locks: 3 lock events (%u), 2 notes (%u)",
          count(SEQ_EV_LOCK), count(SEQ_EV_ON));
    for (uint32_t k = 0; k < 3; k++)
        CHECK(find(SEQ_EV_LOCK, k) == grid(k * step), "lock event %u at %u", k, find(SEQ_EV_LOCK, k));
    {
        uint32_t i = 0;
        while (i < nlog && log_[i].e.type != SEQ_EV_LOCK)
            i++;
        CHECK(i + 1u < nlog && log_[i + 1u].e.type == SEQ_EV_ON && log_[i].t == log_[i + 1u].t && log_[i].e.note == 0,
              "a step's locks before its notes");
    }

    printf("PASS: sequencer timing (straight, swing, length, chords, stop, clock out and in, 8 tracks, records, patterns, live quantizing, skip, beats, a track's swing, count-in, the arp, locks)\n");
    return 0;
}
