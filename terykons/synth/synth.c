#include "synth.h"

void synth_init(synth_t *s)
{
    static const uint8_t ONE_TRACK[SYNTH_S_TRACKS] = {1, 0, 0, 0};
    s->note = -1;
    s->voices = 0;
    s->clock = 0;
    s->steals = s->shed_count = 0;
    for (uint32_t i = 0; i < SYNTH_VOICES_MAX; i++) {
        synth_voice_t *v = &s->voice[i];
        v->note = -1;
        v->velocity = 1.0f;
        v->gate = 0;
        v->shed = 0;
        v->strikes = 0;
        v->age = 0;
        envelope_init(&v->env);
        s->voice_track[i] = SYNTH_NO_TRACK;
    }
    for (uint32_t t = 0; t < SYNTH_TRACKS; t++) {
        s->track[t].first = s->track[t].count = 0;
        s->track[t].held_n = 0;
    }
    for (uint32_t d = 0; d < SYNTH_D_TRACKS; d++) {   /* a drum track: its one voice, always */
        s->track[SYNTH_S_TRACKS + d].first = (uint8_t)(SYNTH_POLY_VOICES + d);
        s->track[SYNTH_S_TRACKS + d].count = 1;
        s->voice_track[SYNTH_POLY_VOICES + d] = (uint8_t)(SYNTH_S_TRACKS + d);
    }
    synth_set_tracks(s, ONE_TRACK);
    lfo_init(&s->mod_lfo, 44100.0f);
}

/* takes note out of a track's held list; 1 if it was there */
static int held_remove(synth_track_t *k, int32_t note)
{
    for (uint32_t i = 0; i < k->held_n; i++) {
        if (k->held[i] != (uint8_t)note)
            continue;
        for (; i + 1 < k->held_n; i++) {
            k->held[i] = k->held[i + 1];
            k->held_vel[i] = k->held_vel[i + 1];
        }
        k->held_n--;
        return 1;
    }
    return 0;
}

static void held_push(synth_track_t *k, int32_t note, uint32_t velocity)
{
    held_remove(k, note);                    /* (the same note again: it moves to the top) */
    if (k->held_n == SYNTH_HELD_MAX)
        held_remove(k, k->held[0]);
    k->held[k->held_n] = (uint8_t)note;
    k->held_vel[k->held_n] = (uint8_t)velocity;
    k->held_n++;
}

static void voice_start(synth_t *s, synth_voice_t *v, int32_t note, uint32_t velocity)
{
    if (v->note >= 0 && v->note != note && !v->shed && (v->gate || v->env.stage != ENV_IDLE))
        s->steals++;
    v->note = note;
    v->velocity = (float)velocity / 127.0f;
    v->gate = 1;
    v->shed = 0;
    v->strikes++;
    v->age = ++s->clock;
    envelope_note_on(&v->env);
}

/* POLY: the track's voice for a new note: the one already on it, else a free one (quiet), else the one let go
 * longest ago, else the oldest held */
static synth_voice_t *voice_for(synth_t *s, const synth_track_t *k, int32_t note)
{
    synth_voice_t *free_v = 0, *released = 0, *oldest = 0;
    for (uint32_t i = k->first; i < (uint32_t)k->first + k->count; i++) {
        synth_voice_t *v = &s->voice[i];
        if (v->note == note && (v->gate || v->env.stage != ENV_IDLE))
            return v;
        if (!v->gate && (v->env.stage == ENV_IDLE || v->shed)) {
            if (!free_v || v->age < free_v->age)
                free_v = v;
        } else if (!v->gate) {
            if (!released || v->age < released->age)
                released = v;
        }
        if (!oldest || v->age < oldest->age)
            oldest = v;
    }
    return free_v ? free_v : released ? released : oldest;
}

void synth_track_note_on(synth_t *s, uint32_t track, int32_t note, uint32_t velocity)
{
    if (track >= SYNTH_TRACKS || note < 0 || note > 127)
        return;
    synth_track_t *k = &s->track[track];
    if (!k->count)
        return;                              /* a track without voices is silent */
    if (velocity > 127u)
        velocity = 127u;
    held_push(k, note, velocity);
    s->note = note;
    voice_start(s, k->count > 1 ? voice_for(s, k, note) : &s->voice[k->first], note, velocity);
}

void synth_track_note_off(synth_t *s, uint32_t track, int32_t note)
{
    if (track >= SYNTH_TRACKS)
        return;
    synth_track_t *k = &s->track[track];
    if (!held_remove(k, note))
        return;
    s->note = k->held_n ? k->held[k->held_n - 1] : -1;
    if (k->count > 1) {                      /* POLY: its voice lets go */
        for (uint32_t i = k->first; i < (uint32_t)k->first + k->count; i++) {
            synth_voice_t *v = &s->voice[i];
            if (v->gate && v->note == note) {
                v->gate = 0;
                envelope_note_off(&v->env);
            }
        }
        return;
    }
    if (!k->count)
        return;
    synth_voice_t *v = &s->voice[k->first];  /* MONO */
    if (!v->gate || v->note != note)
        return;                              /* held under the one sounding */
    if (k->held_n) {                         /* back to the note held before, as it is (no new attack) */
        v->note = k->held[k->held_n - 1];
        v->velocity = (float)k->held_vel[k->held_n - 1] / 127.0f;
        return;
    }
    v->gate = 0;
    envelope_note_off(&v->env);
}

void synth_note_on(synth_t *s, int32_t note, uint32_t velocity) { synth_track_note_on(s, 0, note, velocity); }
void synth_note_off(synth_t *s, int32_t note) { synth_track_note_off(s, 0, note); }

void synth_panic(synth_t *s)
{
    for (uint32_t t = 0; t < SYNTH_TRACKS; t++)
        s->track[t].held_n = 0;
    s->note = -1;
    for (uint32_t i = 0; i < SYNTH_VOICES_MAX; i++) {
        s->voice[i].gate = 0;
        envelope_note_off(&s->voice[i].env);
    }
}

void synth_set_tracks(synth_t *s, const uint8_t counts[SYNTH_S_TRACKS])
{
    uint32_t total = 0, changed = 0;
    for (uint32_t t = 0; t < SYNTH_S_TRACKS; t++)
        total += counts[t];
    if (total > SYNTH_POLY_VOICES)
        return;
    for (uint32_t t = 0; t < SYNTH_S_TRACKS; t++)
        changed |= s->track[t].count != counts[t];
    if (!changed && s->voices == total)
        return;
    synth_panic(s);
    uint32_t first = 0;
    for (uint32_t t = 0; t < SYNTH_S_TRACKS; t++) {
        s->track[t].first = (uint8_t)first;
        s->track[t].count = counts[t];
        for (uint32_t i = 0; i < counts[t]; i++)
            s->voice_track[first + i] = (uint8_t)t;
        first += counts[t];
    }
    for (uint32_t i = first; i < SYNTH_POLY_VOICES; i++)   /* (the drums' voices stay theirs) */
        s->voice_track[i] = SYNTH_NO_TRACK;
    s->voices = total;
}

void synth_set_voices(synth_t *s, uint32_t voices)
{
    uint8_t counts[SYNTH_S_TRACKS] = {0, 0, 0, 0};
    if (voices < 1)
        voices = 1;
    if (voices > SYNTH_POLY_VOICES)
        voices = SYNTH_POLY_VOICES;
    counts[0] = (uint8_t)voices;
    synth_set_tracks(s, counts);
}

void synth_envelopes(synth_t *s, float attack, float decay, float sustain, float release, float sample_rate)
{
    for (uint32_t i = 0; i < SYNTH_VOICES_MAX; i++)
        envelope_set(&s->voice[i].env, attack, decay, sustain, release, sample_rate);
}

void synth_track_envelopes(synth_t *s, uint32_t track, float attack, float decay, float sustain, float release,
                           float sample_rate)
{
    for (uint32_t i = 0; i < SYNTH_VOICES_MAX; i++)
        if (s->voice_track[i] == track)
            envelope_set(&s->voice[i].env, attack, decay, sustain, release, sample_rate);
}

void synth_shed(synth_t *s)
{
    synth_voice_t *oldest = 0;
    uint32_t sounding = 0;
    for (uint32_t i = 0; i < SYNTH_VOICES_MAX; i++) {
        synth_voice_t *v = &s->voice[i];
        if (s->voice_track[i] == SYNTH_NO_TRACK || v->shed || (!v->gate && v->env.stage == ENV_IDLE))
            continue;                        /* quiet already */
        sounding++;
        const int drum = i >= SYNTH_POLY_VOICES, oldest_drum = oldest && oldest >= &s->voice[SYNTH_POLY_VOICES];
        if (!oldest || (oldest_drum && !drum) ||
            (drum == oldest_drum && ((!v->gate && oldest->gate) || (v->gate == oldest->gate && v->age < oldest->age))))
            oldest = v;                      /* a synth track's first; one let go first, then the oldest */
    }
    if (!oldest || sounding < 2)
        return;                              /* the last one sounding stays: cutting it would only lose the note */
    oldest->shed = 1;
    s->shed_count++;
    oldest->gate = 0;                        /* the render fades it out (dsp/grav_engine.c), then its envelope rests */
}

int synth_quiet(const synth_t *s)
{
    for (uint32_t i = 0; i < SYNTH_VOICES_MAX; i++) {
        const synth_voice_t *v = &s->voice[i];
        if (s->voice_track[i] != SYNTH_NO_TRACK && !v->shed && (v->gate || v->env.stage != ENV_IDLE))
            return 0;
    }
    return 1;
}

uint32_t synth_voices_held(const synth_t *s)
{
    uint32_t n = 0;
    for (uint32_t i = 0; i < SYNTH_VOICES_MAX; i++)
        if (s->voice_track[i] != SYNTH_NO_TRACK)
            n += s->voice[i].gate;
    return n;
}
