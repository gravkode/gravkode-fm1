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
        s->pat.step[i].on = 1;
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
    s.pat.length = 3;
    s.pat.step[1].on = 0;
    s.pat.step[1].note[0] = 61;
    s.pat.step[2].note[0] = 62;
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
    s.pat.step[0].n = 3;
    s.pat.step[0].note[1] = 64;
    s.pat.step[0].note[2] = 67;
    s.pat.step[0].len = 2;
    s.pat.step[1].on = 0;
    s.want = SEQ_WANT_PLAY;
    run(&s, 60);                         /* 15360 samples: past step 2 (11025) */
    CHECK(find(SEQ_EV_ON, 2) == 0 && find(SEQ_EV_ON, 3) == grid(2 * step), "a chord at 0, the next note at step 2");
    CHECK(find(SEQ_EV_OFF, 0) == grid(2 * step - step / 8), "the chord ends after 2 steps: %u", find(SEQ_EV_OFF, 0));

    /* stop: every note sounding ends, MIDI stop goes out, nothing more plays */
    fresh(&s);
    s.pat.step[0].len = 8;
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

    /* a record saved, read back */
    {
        seq_pattern_t p;
        seq_defaults(&p);
        CHECK(seq_valid(&p), "the defaults are valid");
        p.length = 0;
        CHECK(!seq_valid(&p), "length 0 refused");
    }

    if (fails) {
        printf("%d failures\n", fails);
        return 1;
    }
    printf("PASS: sequencer timing (straight, swing, length, chords, stop, clock out and in)\n");
    return 0;
}
