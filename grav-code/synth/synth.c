#include "synth.h"

void synth_init(synth_t *s)
{
    s->note = -1;
    s->voices = 1;
    s->held_n = 0;
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
    }
    lfo_init(&s->mod_lfo, 44100.0f);
}

/* takes note out of the held list; 1 if it was there */
static int held_remove(synth_t *s, int32_t note)
{
    for (uint32_t i = 0; i < s->held_n; i++) {
        if (s->held[i] != (uint8_t)note)
            continue;
        for (; i + 1 < s->held_n; i++) {
            s->held[i] = s->held[i + 1];
            s->held_vel[i] = s->held_vel[i + 1];
        }
        s->held_n--;
        return 1;
    }
    return 0;
}

static void held_push(synth_t *s, int32_t note, uint32_t velocity)
{
    held_remove(s, note);                    /* (the same note again: it moves to the top) */
    if (s->held_n == SYNTH_HELD_MAX)
        held_remove(s, s->held[0]);
    s->held[s->held_n] = (uint8_t)note;
    s->held_vel[s->held_n] = (uint8_t)velocity;
    s->held_n++;
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

/* POLY: the voice for a new note: the one already on it, else a free one (quiet), else the one let go longest ago,
 * else the oldest held */
static synth_voice_t *voice_for(synth_t *s, int32_t note)
{
    synth_voice_t *free_v = 0, *released = 0, *oldest = 0;
    for (uint32_t i = 0; i < s->voices; i++) {
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

void synth_note_on(synth_t *s, int32_t note, uint32_t velocity)
{
    if (note < 0 || note > 127)
        return;
    if (velocity > 127u)
        velocity = 127u;
    held_push(s, note, velocity);
    s->note = note;
    voice_start(s, s->voices > 1 ? voice_for(s, note) : &s->voice[0], note, velocity);
}

void synth_note_off(synth_t *s, int32_t note)
{
    if (!held_remove(s, note))
        return;
    s->note = s->held_n ? s->held[s->held_n - 1] : -1;
    if (s->voices > 1) {                     /* POLY: its voice lets go */
        for (uint32_t i = 0; i < s->voices; i++) {
            synth_voice_t *v = &s->voice[i];
            if (v->gate && v->note == note) {
                v->gate = 0;
                envelope_note_off(&v->env);
            }
        }
        return;
    }
    synth_voice_t *v = &s->voice[0];         /* MONO */
    if (!v->gate || v->note != note)
        return;                              /* held under the one sounding */
    if (s->held_n) {                         /* back to the note held before, as it is (no new attack) */
        v->note = s->held[s->held_n - 1];
        v->velocity = (float)s->held_vel[s->held_n - 1] / 127.0f;
        return;
    }
    v->gate = 0;
    envelope_note_off(&v->env);
}

void synth_panic(synth_t *s)
{
    s->held_n = 0;
    s->note = -1;
    for (uint32_t i = 0; i < SYNTH_VOICES_MAX; i++) {
        s->voice[i].gate = 0;
        envelope_note_off(&s->voice[i].env);
    }
}

void synth_set_voices(synth_t *s, uint32_t voices)
{
    if (voices < 1)
        voices = 1;
    if (voices > SYNTH_VOICES_MAX)
        voices = SYNTH_VOICES_MAX;
    if (voices == s->voices)
        return;
    synth_panic(s);
    s->voices = voices;
}

void synth_envelopes(synth_t *s, float attack, float decay, float sustain, float release, float sample_rate)
{
    for (uint32_t i = 0; i < SYNTH_VOICES_MAX; i++)
        envelope_set(&s->voice[i].env, attack, decay, sustain, release, sample_rate);
}

void synth_shed(synth_t *s)
{
    synth_voice_t *oldest = 0;
    if (s->voices == 1)
        return;                              /* MONO keeps its one voice */
    for (uint32_t i = 0; i < s->voices; i++) {
        synth_voice_t *v = &s->voice[i];
        if (v->shed || (!v->gate && v->env.stage == ENV_IDLE))
            continue;                        /* quiet already */
        if (!oldest || (!v->gate && oldest->gate) || (v->gate == oldest->gate && v->age < oldest->age))
            oldest = v;                      /* one let go first, then the oldest */
    }
    if (!oldest)
        return;
    oldest->shed = 1;
    s->shed_count++;
    oldest->gate = 0;
    envelope_init(&oldest->env);             /* silent at once */
}

uint32_t synth_voices_held(const synth_t *s)
{
    uint32_t n = 0;
    for (uint32_t i = 0; i < s->voices; i++)
        n += s->voice[i].gate;
    return n;
}
