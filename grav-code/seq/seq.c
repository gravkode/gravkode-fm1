#include "seq.h"
#include <string.h>

#define SEQ_RATE 44100u
#define SEQ_STEP_Q8_BPM (SEQ_RATE * 60u / 4u * 256u)   /* a 16th at 1 BPM, in samples x 256: 169344000 */
#define SEQ_FAR 0x7FFFFFFF
#define SEQ_EXT_TIMEOUT (SEQ_RATE / 2u)                /* no clock in for half a second: the tempo is ours again */
#define SEQ_CLOCKS_PER_STEP 6u                         /* MIDI clock: 24 a quarter note */

static void step_defaults(seq_step_t *t)
{
    memset(t, 0, sizeof *t);
    t->n = 1;
    t->note[0] = 60;
    t->vel = 100;
    t->len = 1;
}

void seq_defaults(seq_pattern_t *p)
{
    memset(p, 0, sizeof *p);
    p->magic = SEQ_MAGIC;
    p->drum_magic = SEQ_MAGIC_DRUMS;
    p->bpm = 120;
    p->swing = SEQ_SWING_MIN;
    for (uint32_t k = 0; k < SEQ_TRACKS; k++) {
        p->track[k].length = SEQ_PAGE;
        for (uint32_t i = 0; i < SEQ_STEPS; i++)
            step_defaults(&p->track[k].step[i]);
    }
}

static int steps_valid(const seq_step_t *step, uint32_t length)
{
    if (length < 1u || length > SEQ_STEPS)
        return 0;
    for (uint32_t i = 0; i < SEQ_STEPS; i++) {
        const seq_step_t *t = &step[i];
        if (t->on > 1u || t->n < 1u || t->n > SEQ_NOTES || t->vel < 1u || t->vel > 127u || t->len < 1u ||
            t->len > SEQ_STEPS)
            return 0;
        for (uint32_t k = 0; k < t->n; k++)
            if (t->note[k] > 127u)
                return 0;
    }
    return 1;
}

static int swing_valid(uint32_t swing)   /* a track's: 0 (the pattern's) or its own */
{
    return swing == 0u || (swing >= SEQ_SWING_MIN && swing <= SEQ_SWING_MAX);
}

int seq_valid(const seq_pattern_t *p)
{
    if (p->magic != SEQ_MAGIC || p->drum_magic != SEQ_MAGIC_DRUMS || p->bpm < SEQ_BPM_MIN || p->bpm > SEQ_BPM_MAX ||
        p->swing < SEQ_SWING_MIN || p->swing > SEQ_SWING_MAX)
        return 0;
    for (uint32_t k = 0; k < SEQ_TRACKS; k++)
        if (!steps_valid(p->track[k].step, p->track[k].length) || !swing_valid(p->track[k].swing))
            return 0;
    return 1;
}

/* the records lie in the pattern as saved: its head the synth tracks', its tail the drums' */
_Static_assert(sizeof(seq_record_t) == __builtin_offsetof(seq_pattern_t, track[SEQ_S_TRACKS]) &&
               __builtin_offsetof(seq_record_t, track) == __builtin_offsetof(seq_pattern_t, track) &&
               sizeof(seq_drum_record_t) ==
                   sizeof(seq_pattern_t) - __builtin_offsetof(seq_pattern_t, track[SEQ_S_TRACKS]) &&
               __builtin_offsetof(seq_drum_record_t, magic) == __builtin_offsetof(seq_pattern_t, drum_magic) -
                                                                   __builtin_offsetof(seq_pattern_t, track[SEQ_S_TRACKS]),
               "a pattern's records, where they lie in it");
_Static_assert(sizeof(seq_record_t) <= 3840u && sizeof(seq_drum_record_t) <= 3840u, "each fits a store object");

int seq_from_record(const seq_record_t *r, seq_pattern_t *p)
{
    if (r->magic != SEQ_MAGIC || r->bpm < SEQ_BPM_MIN || r->bpm > SEQ_BPM_MAX || r->swing < SEQ_SWING_MIN ||
        r->swing > SEQ_SWING_MAX)
        return 0;
    for (uint32_t k = 0; k < SEQ_S_TRACKS; k++)
        if (!steps_valid(r->track[k].step, r->track[k].length) || !swing_valid(r->track[k].swing))
            return 0;
    p->bpm = r->bpm;
    p->swing = r->swing;
    memcpy(p->track, r->track, sizeof r->track);
    return 1;
}

int seq_from_drum_record(const seq_drum_record_t *d, seq_pattern_t *p)
{
    if (d->magic != SEQ_MAGIC_DRUMS)
        return 0;
    for (uint32_t k = 0; k < SEQ_TRACKS - SEQ_S_TRACKS; k++)
        if (!steps_valid(d->track[k].step, d->track[k].length) || !swing_valid(d->track[k].swing))
            return 0;
    memcpy(&p->track[SEQ_S_TRACKS], d->track, sizeof d->track);
    return 1;
}

int seq_from_v1(const seq_pattern_v1_t *v1, seq_pattern_t *p)
{
    if (v1->magic != SEQ_MAGIC_V1 || v1->bpm < SEQ_BPM_MIN || v1->bpm > SEQ_BPM_MAX ||
        v1->swing < SEQ_SWING_MIN || v1->swing > SEQ_SWING_MAX || !steps_valid(v1->step, v1->length))
        return 0;
    seq_defaults(p);
    p->bpm = v1->bpm;
    p->swing = v1->swing;
    p->track[0].length = v1->length;
    memcpy(p->track[0].step, v1->step, sizeof p->track[0].step);
    return 1;
}

void seq_init(seq_t *s)
{
    memset(s, 0, sizeof *s);
    seq_defaults(&s->pat);
    s->bpm = 120;
    s->next = SEQ_NO_PATTERN;
    for (uint32_t k = 0; k < SEQ_TRACKS; k++) {
        s->step[k] = -1;
        s->pend_q8[k] = SEQ_FAR;
    }
    s->due_q8 = SEQ_FAR;
    s->ext_step_q8 = (int32_t)(SEQ_STEP_Q8_BPM / 120u);
}

/* a straight step, by the song's tempo or the clock in's */
static int32_t straight_q8(const seq_t *s)
{
    return s->ext ? s->ext_step_q8 : (int32_t)(SEQ_STEP_Q8_BPM / s->bpm);
}

/* (0.25) the steps come on a straight grid; a track's swing puts the second of each pair later: by (2 x swing - 100)
 * % of a step (its first takes swing % of the pair), less than half a step (5644800 x 50: within 32 bits) */
static int32_t swing_q8(const seq_t *s, uint32_t track, uint32_t count)
{
    if (!(count & 1u))
        return 0;
    return straight_q8(s) * (int32_t)(2u * seq_track_swing(&s->pat, track) - 100u) / 100;
}

uint32_t seq_bpm(const seq_t *s)
{
    return SEQ_STEP_Q8_BPM / (uint32_t)straight_q8(s);
}

int32_t seq_quantize(const seq_t *s, uint32_t track, int *ahead)
{
    *ahead = 0;
    if (!s->playing || track >= SEQ_TRACKS)
        return -1;
    const int32_t step = s->step[track];
    const uint32_t length = s->pat.track[track].length ? s->pat.track[track].length : 1u;
    if (s->count_in)                     /* counting in: not yet */
        return -1;
    if (step < 0 && !s->start_due) {     /* started, its first step not yet played: that one, ahead */
        *ahead = 1;
        return 0;
    }
    int late;                            /* past the step's first half */
    if (s->ext)                          /* by the clock in: 6 ticks a step, the step's on the first */
        late = s->ext_ticks && (s->ext_ticks - 1u) % SEQ_CLOCKS_PER_STEP >= SEQ_CLOCKS_PER_STEP / 2u;
    else                                 /* by the time to the next step (a straight one) */
        late = s->due_q8 != SEQ_FAR && s->due_q8 * 2 < straight_q8(s);
    if (step < 0)                        /* the count-in's last step: late in it, the first step's */
        return late ? (*ahead = 1, 0) : -1;
    if (!late)
        return step;
    *ahead = 1;
    return (int32_t)(((uint32_t)step + 1u) % length);
}

static void put(seq_event_t *ev, uint32_t *ne, uint32_t max, int32_t at_q8, uint8_t type, uint8_t note, uint8_t vel,
                uint8_t track)
{
    if (*ne >= max)
        return;
    uint32_t at = (uint32_t)(at_q8 < 0 ? 0 : at_q8) >> 8;
    ev[*ne].at = (uint16_t)(at / SEQ_GRID * SEQ_GRID);
    ev[*ne].type = type;
    ev[*ne].note = note;
    ev[*ne].vel = vel;
    ev[*ne].track = track;
    (*ne)++;
}

static void all_off(seq_t *s, seq_event_t *ev, uint32_t *ne, uint32_t max)
{
    for (uint32_t i = 0; i < s->nsounding; i++)
        put(ev, ne, max, 0, SEQ_EV_OFF, s->sounding[i].note, 0, s->sounding[i].track);
    s->nsounding = 0;
}

static void steps_reset(seq_t *s)
{
    for (uint32_t k = 0; k < SEQ_TRACKS; k++) {
        s->step[k] = -1;
        s->skip[k] = 0;
        s->pend_q8[k] = SEQ_FAR;
    }
    s->pat_steps = 0;
}

static int track_has_steps(const seq_track_t *tr)
{
    for (uint32_t i = 0; i < tr->length && i < SEQ_STEPS; i++)
        if (tr->step[i].on)
            return 1;
    return 0;
}

int seq_has_steps(const seq_pattern_t *p)
{
    for (uint32_t k = 0; k < SEQ_TRACKS; k++)
        if (track_has_steps(&p->track[k]))
            return 1;
    return 0;
}

uint32_t seq_length(const seq_pattern_t *p)
{
    uint32_t n = 0;
    for (uint32_t k = 0; k < SEQ_TRACKS; k++)
        if (p->track[k].length > n && track_has_steps(&p->track[k]))
            n = p->track[k].length;
    return n ? n : SEQ_PAGE;
}

void seq_load(seq_t *s, const seq_pattern_t *src)
{
    if (src)
        memcpy(&s->pat, src, sizeof s->pat);
    else
        seq_defaults(&s->pat);
    steps_reset(s);
}

/* a track's step: its notes start at at_q8 (one already sounding on the track is ended first), and end gate later */
static void play_step(seq_t *s, uint32_t track, uint32_t step, int32_t at_q8, int32_t gate, seq_event_t *ev,
                      uint32_t *ne, uint32_t max)
{
    const seq_step_t *t = &s->pat.track[track].step[step];
    if (!t->on)
        return;
    s->hits[track]++;
    for (uint32_t k = 0; k < t->n && k < SEQ_NOTES; k++) {
        const uint8_t note = t->note[k];
        for (uint32_t i = 0; i < s->nsounding; i++)
            if (s->sounding[i].note == note && s->sounding[i].track == track) {   /* sounding: ended, anew */
                put(ev, ne, max, at_q8, SEQ_EV_OFF, note, 0, (uint8_t)track);
                s->sounding[i] = s->sounding[--s->nsounding];
                break;
            }
        if (s->nsounding >= SEQ_SOUNDING)
            continue;
        put(ev, ne, max, at_q8, SEQ_EV_ON, note, t->vel, (uint8_t)track);
        s->sounding[s->nsounding].note = note;
        s->sounding[s->nsounding].track = (uint8_t)track;
        s->sounding[s->nsounding].off_q8 = at_q8 + gate;
        s->nsounding++;
    }
}

/* the steps swung later that come before limit_q8 */
static void flush_pending(seq_t *s, int32_t limit_q8, seq_event_t *ev, uint32_t *ne, uint32_t max)
{
    for (uint32_t track = 0; track < SEQ_TRACKS; track++)
        if (s->pend_q8[track] < limit_q8) {
            if (!(s->mute & (1u << track)))
                play_step(s, track, s->pend_step[track], s->pend_q8[track], s->pend_gate[track], ev, ne, max);
            s->pend_q8[track] = SEQ_FAR;
        }
}

/* the next step of every track, at at_q8 (each round its own length): its notes start then, or (the second of a pair)
 * as late as the track's swing puts them, and end an eighth of a step before the end of their length's last step.
 * Counting in, only the beats */
static void fire(seq_t *s, int32_t at_q8, seq_event_t *ev, uint32_t *ne, uint32_t max)
{
    const uint32_t count = s->count++;
    const int32_t st = straight_q8(s);
    if (s->count_in) {                   /* a count-in: a beat on each quarter note, the tracks waiting */
        if (count % 4u == 0u)
            put(ev, ne, max, at_q8, SEQ_EV_BEAT, count % 16u == 0u, 0, 0);
        if (--s->count_in == 0u) {
            s->count = 0;                /* (the first step next: on the beat, unswung) */
            s->start_due = 1;
        }
        return;
    }
    if (s->start_due) {                  /* the count-in over: MIDI start with the first step */
        s->start_due = 0;
        put(ev, ne, max, at_q8, SEQ_EV_START, 0, 0, 0);
    }
    if (s->pat_steps >= seq_length(&s->pat)) {   /* the pattern's end: the next one in, if it is ready */
        s->pat_steps = 0;
        if (s->next_ready && s->next != SEQ_NO_PATTERN && s->edits == s->saved) {
            seq_load(s, s->next_src);
            s->pattern = s->next;
            s->next = SEQ_NO_PATTERN;
            s->next_ready = 0;
        }
    }
    s->pat_steps++;
    if (s->beats && count % 4u == 0u)    /* a quarter note: the metronome's click (the bar's first: its accent) */
        put(ev, ne, max, at_q8, SEQ_EV_BEAT, count % 16u == 0u, 0, 0);
    for (uint32_t track = 0; track < SEQ_TRACKS; track++) {
        const seq_track_t *tr = &s->pat.track[track];
        const uint32_t length = tr->length ? tr->length : 1u;
        int32_t step = s->step[track] + 1;
        if (step < 0 || (uint32_t)step >= length)
            step = 0;
        s->step[track] = step;
        const seq_step_t *t = &tr->step[step];
        if (s->skip[track]) {            /* a note recorded live just ahead of this step: heard already, once */
            const int here = s->skip[track] == (uint8_t)(step + 1);
            s->skip[track] = 0;
            if (here)
                continue;
        }
        if (!t->on || (s->mute & (1u << track)))   /* (muted: no note) */
            continue;
        const int32_t late = swing_q8(s, track, count);
        const int32_t gate = (int32_t)t->len * st + swing_q8(s, track, count + t->len) - late - st / 8;
        if (late > 0) {                  /* swung: when its time comes (before the next step) */
            s->pend_q8[track] = at_q8 + late;
            s->pend_gate[track] = gate;
            s->pend_step[track] = (uint8_t)step;
        } else
            play_step(s, track, (uint32_t)step, at_q8, gate, ev, ne, max);
    }
}

static void start(seq_t *s, int ext, int cont, seq_event_t *ev, uint32_t *ne, uint32_t max)
{
    s->playing = 1;
    if (!cont) {
        steps_reset(s);
        s->count = 0;
    }
    if (ext) {                           /* the first step on the next clock in */
        s->ext_ticks = 0;
        s->due_q8 = SEQ_FAR;
    } else {
        s->due_q8 = 0;
        s->clock_q8 = 0;
        if (!s->count_in)                /* (counting in: MIDI start with the first step) */
            put(ev, ne, max, 0, SEQ_EV_START, 0, 0, 0);
    }
}

static void stop(seq_t *s, seq_event_t *ev, uint32_t *ne, uint32_t max)
{
    all_off(s, ev, ne, max);
    if (!s->ext)
        put(ev, ne, max, 0, SEQ_EV_STOP, 0, 0, 0);
    s->playing = 0;
    s->count_in = 0;
    s->start_due = 0;
    steps_reset(s);
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
        if (s->ext_ticks % SEQ_CLOCKS_PER_STEP == 0)   /* a step: on the tick (its tracks' swing delays their notes) */
            s->due_q8 = 0;
        s->ext_ticks++;
    }
}

void seq_midi_start(seq_t *s, int cont)
{
    /* at once, not at the next block: the first clock may come in with it (the first step on it) */
    s->all_off = 1;
    s->ext = 1;
    s->count_in = 0;                     /* (the clock in's start: no count-in) */
    s->start_due = 0;
    s->ext_quiet = 0;
    s->playing = 1;
    if (!cont) {
        steps_reset(s);
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
    if (s->mute)                         /* (0.26) a track muted: its notes sounding end now */
        for (uint32_t i = 0; i < s->nsounding;) {
            if (s->mute & (1u << s->sounding[i].track)) {
                put(ev, &ne, max, 0, SEQ_EV_OFF, s->sounding[i].note, 0, s->sounding[i].track);
                s->sounding[i] = s->sounding[--s->nsounding];
            } else
                i++;
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
                s->due_q8 = straight_q8(s);
                s->clock_q8 = 0;
            }
        }
    }
    if (s->playing) {
        while (s->due_q8 < span) {
            const int32_t at = s->due_q8;
            flush_pending(s, at + 1, ev, &ne, max);   /* (a swung step comes before the next) */
            fire(s, at, ev, &ne, max);
            if (s->ext) {
                s->due_q8 = SEQ_FAR;     /* the next on its clock in */
                break;
            }
            s->due_q8 = at + straight_q8(s);
        }
        flush_pending(s, span, ev, &ne, max);   /* the swung steps due in this block; the others nearer */
        for (uint32_t track = 0; track < SEQ_TRACKS; track++)
            if (s->pend_q8[track] != SEQ_FAR)
                s->pend_q8[track] -= span;
        for (uint32_t i = 0; i < s->nsounding;) {   /* the notes that end in this block */
            if (s->sounding[i].off_q8 < span) {
                put(ev, &ne, max, s->sounding[i].off_q8, SEQ_EV_OFF, s->sounding[i].note, 0, s->sounding[i].track);
                s->sounding[i] = s->sounding[--s->nsounding];
            } else {
                s->sounding[i].off_q8 -= span;
                i++;
            }
        }
        if (!s->ext) {
            const int32_t tick = straight_q8(s) / (int32_t)SEQ_CLOCKS_PER_STEP;
            while (s->clock_q8 < span) {
                put(ev, &ne, max, s->clock_q8, SEQ_EV_CLOCK, 0, 0, 0);
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
