#include "seq.h"
#include <string.h>

#define SEQ_RATE 44100u
#define SEQ_STEP_Q8_BPM (SEQ_RATE * 60u / 4u * 256u)   /* a 16th at 1 BPM, in samples x 256: 169344000 */
#define SEQ_FAR 0x7FFFFFFF
#define SEQ_EXT_TIMEOUT (SEQ_RATE / 2u)                /* no clock in for half a second: the tempo is ours again */
#define SEQ_CLOCKS_PER_STEP 6u                         /* MIDI clock: 24 a quarter note */

void seq_defaults(seq_pattern_t *p)
{
    memset(p, 0, sizeof *p);
    p->magic = SEQ_MAGIC;
    p->bpm = 120;
    p->swing = SEQ_SWING_MIN;
    p->length = SEQ_PAGE;
    for (uint32_t i = 0; i < SEQ_STEPS; i++) {
        p->step[i].n = 1;
        p->step[i].note[0] = 60;
        p->step[i].vel = 100;
        p->step[i].len = 1;
    }
}

int seq_valid(const seq_pattern_t *p)
{
    if (p->magic != SEQ_MAGIC || p->bpm < SEQ_BPM_MIN || p->bpm > SEQ_BPM_MAX || p->swing < SEQ_SWING_MIN ||
        p->swing > SEQ_SWING_MAX || p->length < 1u || p->length > SEQ_STEPS)
        return 0;
    for (uint32_t i = 0; i < SEQ_STEPS; i++) {
        const seq_step_t *t = &p->step[i];
        if (t->on > 1u || t->n < 1u || t->n > SEQ_NOTES || t->vel < 1u || t->vel > 127u || t->len < 1u ||
            t->len > SEQ_STEPS)
            return 0;
        for (uint32_t k = 0; k < t->n; k++)
            if (t->note[k] > 127u)
                return 0;
    }
    return 1;
}

void seq_init(seq_t *s)
{
    memset(s, 0, sizeof *s);
    seq_defaults(&s->pat);
    s->step = -1;
    s->due_q8 = SEQ_FAR;
    s->ext_step_q8 = (int32_t)(SEQ_STEP_Q8_BPM / 120u);
}

/* a straight step, by the pattern's tempo or the clock in's */
static int32_t straight_q8(const seq_t *s)
{
    return s->ext ? s->ext_step_q8 : (int32_t)(SEQ_STEP_Q8_BPM / s->pat.bpm);
}

/* the step played count-th since the start: the first of each pair takes swing % of the pair (at most 2 x 5644800 x
 * 75: within 32 bits) */
static int32_t step_q8(const seq_t *s, uint32_t count)
{
    const uint32_t st = (uint32_t)straight_q8(s);
    const uint32_t first = 2u * st * s->pat.swing / 100u;
    return (int32_t)((count & 1u) ? 2u * st - first : first);
}

uint32_t seq_bpm(const seq_t *s)
{
    return SEQ_STEP_Q8_BPM / (uint32_t)straight_q8(s);
}

static void put(seq_event_t *ev, uint32_t *ne, uint32_t max, int32_t at_q8, uint8_t type, uint8_t note, uint8_t vel)
{
    if (*ne >= max)
        return;
    uint32_t at = (uint32_t)(at_q8 < 0 ? 0 : at_q8) >> 8;
    ev[*ne].at = (uint16_t)(at / SEQ_GRID * SEQ_GRID);
    ev[*ne].type = type;
    ev[*ne].note = note;
    ev[*ne].vel = vel;
    (*ne)++;
}

static void all_off(seq_t *s, seq_event_t *ev, uint32_t *ne, uint32_t max)
{
    for (uint32_t i = 0; i < s->nsounding; i++)
        put(ev, ne, max, 0, SEQ_EV_OFF, s->sounding[i].note, 0);
    s->nsounding = 0;
}

/* the next step, at at_q8: its notes start (one already sounding is ended first), and end after its length */
static void fire(seq_t *s, int32_t at_q8, seq_event_t *ev, uint32_t *ne, uint32_t max)
{
    const uint32_t length = s->pat.length ? s->pat.length : 1u;
    const uint32_t count = s->count++;
    int32_t step = s->step + 1;
    if (step < 0 || (uint32_t)step >= length)
        step = 0;
    s->step = step;
    const seq_step_t *t = &s->pat.step[step];
    if (!t->on)
        return;
    int32_t gate = 0;
    for (uint32_t k = 0; k < t->len; k++)
        gate += step_q8(s, count + k);
    gate -= straight_q8(s) / 8;
    for (uint32_t k = 0; k < t->n && k < SEQ_NOTES; k++) {
        const uint8_t note = t->note[k];
        for (uint32_t i = 0; i < s->nsounding; i++)
            if (s->sounding[i].note == note) {          /* sounding still: ended, then started anew */
                put(ev, ne, max, at_q8, SEQ_EV_OFF, note, 0);
                s->sounding[i] = s->sounding[--s->nsounding];
                break;
            }
        if (s->nsounding >= SEQ_SOUNDING)
            continue;
        put(ev, ne, max, at_q8, SEQ_EV_ON, note, t->vel);
        s->sounding[s->nsounding].note = note;
        s->sounding[s->nsounding].off_q8 = at_q8 + gate;
        s->nsounding++;
    }
}

static void start(seq_t *s, int ext, int cont, seq_event_t *ev, uint32_t *ne, uint32_t max)
{
    s->playing = 1;
    if (!cont) {
        s->step = -1;
        s->count = 0;
    }
    if (ext) {                           /* the first step on the next clock in */
        s->ext_ticks = 0;
        s->due_q8 = SEQ_FAR;
    } else {
        s->due_q8 = 0;
        s->clock_q8 = 0;
        put(ev, ne, max, 0, SEQ_EV_START, 0, 0);
    }
}

static void stop(seq_t *s, seq_event_t *ev, uint32_t *ne, uint32_t max)
{
    all_off(s, ev, ne, max);
    if (!s->ext)
        put(ev, ne, max, 0, SEQ_EV_STOP, 0, 0);
    s->playing = 0;
    s->step = -1;
    s->due_q8 = SEQ_FAR;
}

void seq_midi_clock(seq_t *s)
{
    if (!s->ext) {                       /* a clock in again: its tempo from this beat on */
        s->ext = 1;
        s->ext_step_q8 = straight_q8(s);
        s->win_samples = s->win_ticks = 0;
        if (s->playing) {                /* playing on our own clock: from here on the clock in's */
            s->ext_ticks = 0;
            s->due_q8 = SEQ_FAR;
        }
    }
    s->ext_quiet = 0;
    if (++s->win_ticks == 24u) {         /* a beat (4 steps): the tempo, smoothed */
        const int32_t measured = (int32_t)(s->win_samples * 64u);   /* x 256 / 4 */
        s->ext_step_q8 += (measured - s->ext_step_q8) / 2;
        s->win_samples = s->win_ticks = 0;
    }
    if (s->playing) {
        if (s->ext_ticks % SEQ_CLOCKS_PER_STEP == 0) {   /* a step: on the tick, the second of a pair swung later */
            const int32_t st = s->ext_step_q8;
            s->due_q8 = (s->count & 1u) ? st * (int32_t)(2u * s->pat.swing - 100u) / 100 : 0;
        }
        s->ext_ticks++;
    }
}

void seq_midi_start(seq_t *s, int cont)
{
    /* at once, not at the next block: the first clock may come in with it (the first step on it) */
    s->all_off = 1;
    s->ext = 1;
    s->ext_quiet = 0;
    s->playing = 1;
    if (!cont) {
        s->step = -1;
        s->count = 0;
    }
    s->ext_ticks = 0;
    s->due_q8 = SEQ_FAR;
}

static void sort(seq_event_t *ev, uint32_t ne)
{
    for (uint32_t i = 1; i < ne; i++) {
        seq_event_t e = ev[i];
        uint32_t j = i;
        while (j > 0 && (ev[j - 1].at > e.at || (ev[j - 1].at == e.at && ev[j - 1].type > e.type))) {
            ev[j] = ev[j - 1];
            j--;
        }
        ev[j] = e;
    }
}

uint32_t seq_block(seq_t *s, uint32_t n, seq_event_t *ev, uint32_t max)
{
    const int32_t span = (int32_t)(n << 8);
    uint32_t ne = 0;
    const uint8_t want = s->want;
    if (s->all_off) {                    /* (MIDI start: what was sounding ends) */
        s->all_off = 0;
        all_off(s, ev, &ne, max);
    }
    if (want) {
        s->want = SEQ_WANT_NONE;
        if (want == SEQ_WANT_STOP) {
            if (s->playing)
                stop(s, ev, &ne, max);
        } else if (!s->playing) {
            start(s, s->ext, 0, ev, &ne, max);
        }
    }
    if (s->ext) {
        s->ext_quiet += n;
        s->win_samples += n;
        if (s->ext_quiet > SEQ_EXT_TIMEOUT) {   /* the clock in stopped: on our own tempo, from the next step */
            s->ext = 0;
            if (s->playing && s->due_q8 == SEQ_FAR) {
                s->due_q8 = step_q8(s, s->count);
                s->clock_q8 = 0;
            }
        }
    }
    if (s->playing) {
        while (s->due_q8 < span) {
            const int32_t at = s->due_q8;
            const uint32_t count = s->count;
            fire(s, at, ev, &ne, max);
            if (s->ext) {
                s->due_q8 = SEQ_FAR;     /* the next on its clock in */
                break;
            }
            s->due_q8 = at + step_q8(s, count);
        }
        for (uint32_t i = 0; i < s->nsounding;) {   /* the notes that end in this block */
            if (s->sounding[i].off_q8 < span) {
                put(ev, &ne, max, s->sounding[i].off_q8, SEQ_EV_OFF, s->sounding[i].note, 0);
                s->sounding[i] = s->sounding[--s->nsounding];
            } else {
                s->sounding[i].off_q8 -= span;
                i++;
            }
        }
        if (!s->ext) {
            const int32_t tick = straight_q8(s) / (int32_t)SEQ_CLOCKS_PER_STEP;
            while (s->clock_q8 < span) {
                put(ev, &ne, max, s->clock_q8, SEQ_EV_CLOCK, 0, 0);
                s->clock_q8 += tick;
            }
            s->clock_q8 -= span;
        }
        if (s->due_q8 != SEQ_FAR)
            s->due_q8 -= span;
    }
    sort(ev, ne);
    return ne;
}
