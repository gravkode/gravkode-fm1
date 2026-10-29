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

/* ------------------------------------------------------------------ the parameter locks (0.35) --- */
_Static_assert(SEQ_PATTERN_V018 == 4140u, "the pattern before its locks, as 0.18..0.34 saved it");

int seq_locks_valid(const seq_pattern_t *p)
{
    if (p->nlocks > SEQ_LOCKS)
        return 0;
    for (uint32_t i = 0; i < p->nlocks; i++) {
        const uint32_t k = p->lock[i].key;
        if ((k & 15u) >= SEQ_LK_PARAMS || (k >> 10) >= SEQ_TRACKS || p->lock[i].value > 100u ||
            (i && k <= p->lock[i - 1u].key))
            return 0;
    }
    return 1;
}

static uint32_t lock_at(const seq_pattern_t *p, uint16_t key)   /* the first lock at key or after it */
{
    uint32_t lo = 0, hi = p->nlocks;
    while (lo < hi) {
        const uint32_t mid = (lo + hi) / 2u;
        if (p->lock[mid].key < key)
            lo = mid + 1u;
        else
            hi = mid;
    }
    return lo;
}

int seq_lock_get(const seq_pattern_t *p, uint32_t track, uint32_t step, uint32_t param)
{
    const uint16_t key = SEQ_LOCK_KEY(track, step, param);
    const uint32_t i = lock_at(p, key);
    return i < p->nlocks && p->lock[i].key == key ? (int)p->lock[i].value : -1;
}

int seq_lock_set(seq_pattern_t *p, uint32_t track, uint32_t step, uint32_t param, int32_t value)
{
    if (track >= SEQ_TRACKS || step >= SEQ_STEPS || param >= SEQ_LK_PARAMS)
        return -1;
    const uint16_t key = SEQ_LOCK_KEY(track, step, param);
    const uint32_t i = lock_at(p, key);
    const int here = i < p->nlocks && p->lock[i].key == key;
    if (value < 0) {                     /* removed */
        if (here) {
            memmove(&p->lock[i], &p->lock[i + 1u], (p->nlocks - i - 1u) * sizeof p->lock[0]);
            p->nlocks--;
        }
        return 0;
    }
    if (!here) {
        if (p->nlocks >= SEQ_LOCKS)
            return -1;
        memmove(&p->lock[i + 1u], &p->lock[i], (p->nlocks - i) * sizeof p->lock[0]);
        p->nlocks++;
        p->lock[i].key = key;
        p->lock[i].rsv = 0;
    }
    p->lock[i].value = (uint8_t)(value > 100 ? 100 : value);
    return 0;
}

void seq_lock_clear(seq_pattern_t *p, uint32_t track, int32_t step)
{
    const uint16_t lo = step < 0 ? SEQ_LOCK_KEY(track, 0, 0) : SEQ_LOCK_KEY(track, step, 0);
    const uint32_t hi = step < 0 ? SEQ_LOCK_KEY(track + 1u, 0, 0) : SEQ_LOCK_KEY(track, step + 1, 0);
    const uint32_t a = lock_at(p, lo);
    uint32_t b = a;
    while (b < p->nlocks && p->lock[b].key < hi)
        b++;
    memmove(&p->lock[a], &p->lock[b], (p->nlocks - b) * sizeof p->lock[0]);
    p->nlocks = (uint16_t)(p->nlocks - (b - a));
}

uint32_t seq_lock_mask(const seq_pattern_t *p, uint32_t track, uint32_t step, uint8_t *val)
{
    uint32_t mask = 0;
    for (uint32_t i = lock_at(p, SEQ_LOCK_KEY(track, step, 0));
         i < p->nlocks && p->lock[i].key >> 4 == (track << 6 | step); i++) {
        mask |= 1u << (p->lock[i].key & 15u);
        if (val)
            val[p->lock[i].key & 15u] = p->lock[i].value;
    }
    return mask;
}

/* the records lie in the pattern as saved: its head the synth tracks', its tail the drums' */
_Static_assert(sizeof(seq_record_t) == __builtin_offsetof(seq_pattern_t, track[SEQ_S_TRACKS]) &&
               __builtin_offsetof(seq_record_t, track) == __builtin_offsetof(seq_pattern_t, track) &&
               sizeof(seq_drum_record_t) ==
                   SEQ_PATTERN_V018 - __builtin_offsetof(seq_pattern_t, track[SEQ_S_TRACKS]) &&
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
    for (uint32_t k = 0; k < SEQ_S_TRACKS; k++) {   /* (0.34) the arps: none running */
        s->arp[k].free_q8 = SEQ_FAR;
        s->arp[k].last = 0xFFu;
        s->arp[k].rnd = 0x2545F491u + k;
    }
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
    if (!seq_locks_valid(&s->pat))       /* (0.35: one saved before its locks: the flash erased past it, none) */
        s->pat.nlocks = 0;
    steps_reset(s);
}

/* a track's step: its notes start at at_q8 (one already sounding on the track is ended first), and end gate later */
static void play_step(seq_t *s, uint32_t track, uint32_t step, int32_t at_q8, int32_t gate, seq_event_t *ev,
                      uint32_t *ne, uint32_t max)
{
    const seq_step_t *t = &s->pat.track[track].step[step];
    put(ev, ne, max, at_q8, SEQ_EV_LOCK, (uint8_t)step, 0, (uint8_t)track);   /* (0.35) its locks, or none: the
                                                                              * track's own knobs again */
    if (!t->on)
        return;
    if (seq_arp_on(s, track)) {          /* (0.34) its arp's: held for the step's length (its notes flash the track) */
        seq_arp_t *a = &s->arp[track];
        for (uint32_t k = 0; k < t->n && k < SEQ_NOTES && a->in_n < SEQ_ARP_NOTES; k++) {
            a->in[a->in_n].note = t->note[k];
            a->in[a->in_n].vel = t->vel;
            a->in[a->in_n].at_q8 = at_q8;
            a->in[a->in_n].rel_q8 = at_q8 + gate;
            a->in_n++;
        }
        return;
    }
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
    if (!s->ticking)                     /* (0.34) the song's ticks from its first step: the arps on them */
        for (uint32_t k = 0; k < SEQ_S_TRACKS; k++)
            s->arp[k].free_q8 = SEQ_FAR;
    s->ticking = 1;
    s->tick_no = count * SEQ_CLOCKS_PER_STEP;   /* (the clock in's: a tick each, seq_midi_clock) */
    s->tick_q8 = at_q8;
    s->tick_left = s->ext ? 0u : (uint8_t)SEQ_CLOCKS_PER_STEP;
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
        if ((!t->on && !seq_lock_mask(&s->pat, track, (uint32_t)step, 0)) || (s->mute & (1u << track)))
            continue;                    /* (muted: no note; 0.35: a step off with locks: its locks alone) */
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
    s->ticking = 0;
    s->tick_left = 0;
    s->ext_ticks_n = 0;
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
    s->ticking = 0;                      /* (0.34: the arps on their own ticks again, from the next block) */
    s->tick_left = 0;
    s->ext_ticks_n = 0;
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
        if (s->ext_ticks_n < 4u)         /* (0.34) a tick for the arps, at the next block's start */
            s->ext_tick[s->ext_ticks_n++] = s->ext_ticks;
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
    s->ticking = 0;
    s->tick_left = 0;
    s->ext_ticks_n = 0;
}

/* ------------------------------------------------------------------ the arpeggiator (0.34) --- */
static int arp_find(const seq_arp_t *a, uint8_t note)
{
    for (uint32_t i = 0; i < a->n; i++)
        if (a->note[i] == note)
            return (int)i;
    return -1;
}

static void arp_remove(seq_arp_t *a, uint32_t i)   /* (the others kept in the order they came) */
{
    for (; i + 1u < a->n; i++) {
        a->note[i] = a->note[i + 1u];
        a->vel[i] = a->vel[i + 1u];
        a->rel_q8[i] = a->rel_q8[i + 1u];
    }
    a->n--;
    if (!a->n)                           /* (all let go: the next ones from its order's start) */
        a->pos = 0;
}

/* a note held: a key's (rel_q8 SEQ_FAR) until let go, a step's until rel_q8; held already, its velocity anew and the
 * later let-go; full, the oldest goes */
static void arp_add(seq_arp_t *a, uint8_t note, uint8_t vel, int32_t rel_q8)
{
    const int i = arp_find(a, note);
    if (i >= 0) {
        a->vel[i] = vel;
        if (rel_q8 == SEQ_FAR || (a->rel_q8[i] != SEQ_FAR && rel_q8 > a->rel_q8[i]))
            a->rel_q8[i] = rel_q8;
        return;
    }
    if (a->n >= SEQ_ARP_NOTES)
        arp_remove(a, 0);
    a->note[a->n] = note;
    a->vel[a->n] = vel;
    a->rel_q8[a->n] = rel_q8;
    a->n++;
}

int seq_arp_note(seq_t *s, uint32_t track, uint8_t note, uint8_t vel)
{
    if (!seq_arp_on(s, track))
        return 0;
    seq_arp_t *a = &s->arp[track];
    const int i = arp_find(a, note);
    if (vel) {
        if (a->latched) {                /* HOLD: keys again after all were let go: the notes kept go */
            for (uint32_t k = 0; k < a->n;)
                if (a->rel_q8[k] == SEQ_FAR)
                    arp_remove(a, k);
                else
                    k++;
            a->latched = 0;
            a->keys = 0;
        }
        if (arp_find(a, note) < 0 || a->rel_q8[arp_find(a, note)] != SEQ_FAR)
            a->keys++;
        arp_add(a, note, vel, SEQ_FAR);
        return 1;
    }
    if (i < 0 || a->rel_q8[i] != SEQ_FAR)  /* not a key's: a step's, let go by its step; none: the synth's own */
        return i >= 0;
    if (a->keys)
        a->keys--;
    if (a->hold) {                       /* HOLD: kept; once every key is let go, until the next */
        if (!a->keys)
            a->latched = 1;
    } else
        arp_remove(a, (uint32_t)i);
    return 1;
}

/* its order: the notes held, low to high (AS PLAYED: as they came), then each an octave up, as many octaves as it
 * has; DOWN the other way; UP-DOWN up and back, the ends once */
uint32_t seq_arp_order(const seq_arp_t *a, uint8_t *out)
{
    uint8_t notes[SEQ_ARP_NOTES];
    const uint32_t n = a->n, octaves = a->octaves < 1u ? 1u : a->octaves > 4u ? 4u : a->octaves;
    for (uint32_t i = 0; i < n; i++) {
        uint8_t v = a->note[i];
        uint32_t j = i;
        if (a->mode != SEQ_ARP_PLAYED)   /* (sorted, as it goes) */
            for (; j > 0 && notes[j - 1u] > v; j--)
                notes[j] = notes[j - 1u];
        notes[j] = v;
    }
    uint32_t len = 0;
    for (uint32_t o = 0; o < octaves; o++)
        for (uint32_t i = 0; i < n; i++)
            out[len++] = (uint8_t)(notes[i] + 12u * o > 127u ? 127u : notes[i] + 12u * o);
    if (a->mode == SEQ_ARP_DOWN)
        for (uint32_t i = 0; i < len / 2u; i++) {
            const uint8_t t = out[i];
            out[i] = out[len - 1u - i];
            out[len - 1u - i] = t;
        }
    else if (a->mode == SEQ_ARP_UPDOWN && len > 2u) {
        const uint32_t up = len;
        for (uint32_t i = up - 2u; i >= 1u; i--)
            out[len++] = out[i];
    }
    return len;
}

static uint8_t arp_vel(const seq_arp_t *a, uint8_t note)   /* the velocity of the held note it comes from */
{
    for (uint32_t i = 0; i < a->n; i++)
        if (note >= a->note[i] && (note - a->note[i]) % 12u == 0u)
            return a->vel[i];
    return a->n ? a->vel[a->n - 1u] : 100u;
}

/* a note of its: one sounding on the track ended first; its end len_q8 later */
static void arp_play(seq_t *s, uint32_t track, uint8_t note, uint8_t vel, int32_t at_q8, int32_t len_q8,
                     seq_event_t *ev, uint32_t *ne, uint32_t max)
{
    for (uint32_t i = 0; i < s->nsounding; i++)
        if (s->sounding[i].note == note && s->sounding[i].track == track) {   /* (its end, or now if that is later) */
            put(ev, ne, max, s->sounding[i].off_q8 < at_q8 ? s->sounding[i].off_q8 : at_q8, SEQ_EV_OFF, note, 0,
                (uint8_t)track);
            s->sounding[i] = s->sounding[--s->nsounding];
            break;
        }
    if (s->nsounding >= SEQ_SOUNDING)
        return;
    put(ev, ne, max, at_q8, SEQ_EV_ON, note, vel, (uint8_t)track);
    s->sounding[s->nsounding].note = note;
    s->sounding[s->nsounding].track = (uint8_t)track;
    s->sounding[s->nsounding].off_q8 = at_q8 + (len_q8 > (int32_t)(SEQ_GRID << 8) ? len_q8 : (int32_t)(SEQ_GRID << 8));
    s->nsounding++;
    s->arp[track].last = note;
}

/* a tick of the song (or its own), number no: every rate ticks, its next note (CHORD: all its notes, an octave up
 * each time round) */
static void arp_tick(seq_t *s, uint32_t track, uint32_t no, int32_t at_q8, int32_t tick_q8, seq_event_t *ev,
                     uint32_t *ne, uint32_t max)
{
    seq_arp_t *a = &s->arp[track];
    const uint32_t rate = a->rate ? a->rate : SEQ_CLOCKS_PER_STEP;
    if (!a->n || no % rate || (s->mute & (1u << track)))
        return;
    /* (in 32 bits: a 64-bit multiply beside a load is the FM-1 core's erratum, felucca-bsp docs/CPU.md; rate x tick_q8
     * is under 46 M at 30 BPM, a hundredth of it times GATE too) */
    const int32_t len = (int32_t)rate * tick_q8 / 100 * (int32_t)(a->gate ? a->gate : 50u);
    if (a->mode == SEQ_ARP_CHORD) {
        const uint32_t octaves = a->octaves < 1u ? 1u : a->octaves > 4u ? 4u : a->octaves, o = a->pos % octaves;
        a->pos = (uint8_t)((o + 1u) % octaves);
        for (uint32_t i = 0; i < a->n; i++)
            arp_play(s, track, (uint8_t)(a->note[i] + 12u * o > 127u ? 127u : a->note[i] + 12u * o), a->vel[i], at_q8,
                     len, ev, ne, max);
    } else {
        uint8_t order[SEQ_ARP_ORDER];
        const uint32_t n = seq_arp_order(a, order);
        uint32_t i;
        if (a->mode == SEQ_ARP_RANDOM) {   /* (not the same twice running, while there is another) */
            a->rnd = a->rnd * 1664525u + 1013904223u;
            i = (a->rnd >> 16) % n;
            if (n > 1u && order[i] == a->last)
                i = (i + 1u) % n;
        } else {
            i = a->pos % n;
            a->pos = (uint8_t)((i + 1u) % n);
        }
        arp_play(s, track, order[i], arp_vel(a, order[i]), at_q8, len, ev, ne, max);
    }
    a->played++;
    s->hits[track]++;
}

/* the song's ticks of the step playing that come before limit_q8 (6 a step, from its start), at most 16 a block */
static void song_ticks(seq_t *s, int32_t limit_q8, int32_t *tk_at, uint32_t *tk_no, uint32_t *ntk)
{
    const int32_t tick = straight_q8(s) / (int32_t)SEQ_CLOCKS_PER_STEP;
    while (s->tick_left && s->tick_q8 < limit_q8 && *ntk < 16u) {
        tk_at[*ntk] = s->tick_q8;
        tk_no[(*ntk)++] = s->tick_no++;
        s->tick_q8 += tick;
        s->tick_left--;
    }
}

/* each arp's block: its steps' notes held and let go, its ticks (the song's, tk: at and number; stopped, its own), in
 * the order they fall (at the same time: let go, held, then the tick) */
static void arps_block(seq_t *s, int32_t span, const int32_t *tk_at, const uint32_t *tk_no, uint32_t ntk,
                       seq_event_t *ev, uint32_t *ne, uint32_t max)
{
    const int32_t tick_q8 = straight_q8(s) / (int32_t)SEQ_CLOCKS_PER_STEP;
    for (uint32_t track = 0; track < SEQ_S_TRACKS; track++) {
        seq_arp_t *a = &s->arp[track];
        const int on = seq_arp_on(s, track);
        if (!on) {                       /* off (or turned off): nothing held, its notes ended */
            if (a->was & 1u) {
                for (uint32_t i = 0; i < s->nsounding;)
                    if (s->sounding[i].track == track) {
                        put(ev, ne, max, 0, SEQ_EV_OFF, s->sounding[i].note, 0, (uint8_t)track);
                        s->sounding[i] = s->sounding[--s->nsounding];
                    } else
                        i++;
            }
            a->n = a->keys = a->latched = a->pos = 0;
            a->in_n = 0;
            a->free_q8 = SEQ_FAR;
            a->was = 0;
            continue;
        }
        if ((a->was & 2u) && !a->hold && a->latched) {   /* HOLD off: the notes kept go */
            for (uint32_t i = 0; i < a->n;)
                if (a->rel_q8[i] == SEQ_FAR)
                    arp_remove(a, i);
                else
                    i++;
            a->latched = 0;
        }
        a->was = (uint8_t)(1u | (a->hold ? 2u : 0u));
        if (!s->ticking && a->n && a->free_q8 == SEQ_FAR) {   /* stopped, a first note: its own ticks, from now */
            a->free_q8 = 0;
            a->free_ticks = 0;
        }
        uint32_t ki = 0, ii = 0;
        for (;;) {
            int32_t t_tick = SEQ_FAR, t_in = ii < a->in_n ? a->in[ii].at_q8 : SEQ_FAR, t_rel = SEQ_FAR;
            if (s->ticking)
                t_tick = ki < ntk ? tk_at[ki] : SEQ_FAR;
            else if (a->n || t_in < span)
                t_tick = a->free_q8;
            for (uint32_t i = 0; i < a->n; i++)
                if (a->rel_q8[i] < t_rel)
                    t_rel = a->rel_q8[i];
            const int32_t t = t_rel < t_in ? (t_rel < t_tick ? t_rel : t_tick) : (t_in < t_tick ? t_in : t_tick);
            if (t >= span)
                break;
            if (t_rel == t) {            /* a step's notes let go */
                for (uint32_t i = 0; i < a->n;)
                    if (a->rel_q8[i] <= t)
                        arp_remove(a, i);
                    else
                        i++;
            } else if (t_in == t) {      /* a step's notes held */
                if (!s->ticking && !a->n && a->free_q8 == SEQ_FAR) {
                    a->free_q8 = t;
                    a->free_ticks = 0;
                }
                arp_add(a, a->in[ii].note, a->in[ii].vel, a->in[ii].rel_q8);
                ii++;
            } else if (s->ticking) {     /* the song's tick */
                arp_tick(s, track, tk_no[ki], t, tick_q8, ev, ne, max);
                ki++;
            } else {                     /* its own tick: stopped once nothing is held */
                if (!a->n) {
                    a->free_q8 = SEQ_FAR;
                    continue;
                }
                arp_tick(s, track, a->free_ticks++, t, tick_q8, ev, ne, max);
                a->free_q8 += tick_q8;
            }
        }
        a->in_n = 0;
        for (uint32_t i = 0; i < a->n; i++)
            if (a->rel_q8[i] != SEQ_FAR)
                a->rel_q8[i] -= span;
        if (a->free_q8 != SEQ_FAR)
            a->free_q8 -= span;
    }
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
    int32_t tk_at[16];                   /* (0.34) the song's ticks in this block, for the arps */
    uint32_t tk_no[16], ntk = 0;
    if (s->playing) {
        while (s->due_q8 < span) {
            const int32_t at = s->due_q8;
            song_ticks(s, at, tk_at, tk_no, &ntk);   /* (the step before's) */
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
        song_ticks(s, span, tk_at, tk_no, &ntk);
        if (s->tick_left)
            s->tick_q8 -= span;
        if (s->ext && s->ticking)        /* (the clock in's ticks, at the block's start) */
            for (uint32_t i = 0; i < s->ext_ticks_n && ntk < 16u; i++) {
                for (uint32_t j = ntk++; j > 0; j--) {   /* (before the others: at 0) */
                    tk_at[j] = tk_at[j - 1u];
                    tk_no[j] = tk_no[j - 1u];
                }
                tk_at[0] = 0;
                tk_no[0] = s->ext_tick[s->ext_ticks_n - 1u - i];
            }
        s->ext_ticks_n = 0;
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
    arps_block(s, span, tk_at, tk_no, ntk, ev, &ne, max);   /* (0.34) */
    for (uint32_t i = 0; i < s->nsounding;) {   /* the notes that end in this block (stopped: the arps') */
        if (s->sounding[i].off_q8 < span) {
            put(ev, &ne, max, s->sounding[i].off_q8, SEQ_EV_OFF, s->sounding[i].note, 0, s->sounding[i].track);
            s->sounding[i] = s->sounding[--s->nsounding];
        } else {
            s->sounding[i].off_q8 -= span;
            i++;
        }
    }
    sort(ev, ne);
    return ne;
}
