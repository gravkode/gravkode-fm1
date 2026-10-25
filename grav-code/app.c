/* SPDX-License-Identifier: MPL-2.0
 * Copyright (C) 2026 Charles Vestal */
/* GRAV CODE: up to four voices on Plaits' engines (dsp/), with an envelope and an LFO (synth/), through Beads (the
 * effect). The keys play them with velocity and go out over USB MIDI (MIDI in, USB or TRS, plays them too); PRESET
 * picks the engine, K1-K3 the page's parameters (HOME: OSC, LFO: MOD, ENV: ENV, GLO: GLO, FX: the effect's three
 * pages), EDIT freezes the effect, SEL seeds it, OCT- / OCT+ the octave, MASTER the volume, SAVE stores it all. No
 * radio: Bluetooth and WiFi are left out of the build. Part of the unity build (device/unity.c, host/plat_host.c),
 * after the platform: gfx.c and announce.c are in scope. */
#include "fastmath.h"
#include "dsp/grav_engine.h"
#include "dsp/grav_plaits.h"
#include "synth/synth.h"
#include "synth/event_queue.h"
#include "seq/seq.h"

_Static_assert(APP_BLOCK <= 256u, "dual-core voice buffers fit APP_BLOCK");

/* the effect's knobs (FX: GRAIN, SHAPE, MIX; QUALITY apart), 0..100 */
enum { FX_DENSITY, FX_TIME, FX_SIZE, FX_SHAPE, FX_PITCH, FX_FEEDBACK, FX_DRY_WET, FX_REVERB, FX_N };
static const uint8_t FX_DEFAULT[FX_N] = {75, 30, 65, 50, 50, 30, 0, 0};   /* (DRY/WET and REVERB 0: off) */

#define SET_MAGIC 0x47525639u
#define PREV_SET_MAGIC 0x54504C32u
#define OLD_SET_MAGIC 0x54504C31u            /* "TPL1": the settings record; another magic loads defaults */
typedef struct {
    uint32_t magic;
    int8_t octave;                       /* -2 .. +2 */
    uint8_t engine_id;                   /* GRAV_ENGINE_* (dsp/grav_plaits.h): one of Plaits' 24 */
    uint8_t rsv_ble;                     /* was Bluetooth switched off (REC held), when there was Bluetooth: kept,
                                          * unread, for the record's layout (saved settings still load) */
    uint8_t rsv[1];
    uint8_t osc[3];
    uint8_t mod[3];
    uint8_t env[3];
    uint8_t voices;                      /* 1 MONO, 2..4 POLY (GLO); 0, in a record saved before 0.12: the default */
    uint8_t velocity_off;                /* the FM-1's keys play at a fixed velocity (GLO); MIDI in keeps its own */
    uint8_t reserved[1];
    uint8_t fx[FX_N];                    /* 0.13: the effect's knobs; in a record saved before (20 bytes): defaults */
    uint8_t fx_quality;                  /* GRAV_FX_* (dsp/grav_beads.h) */
    uint8_t fx_reserved[3];
    /* 0.16: the synth tracks' sounds (S1..S4): engine, OSC's three knobs, voices (0..4, together 4 at most). engine_id
     * and osc[] above keep S1's, for the record's older readers */
    uint8_t trk_engine[SYNTH_S_TRACKS];
    uint8_t trk_osc[SYNTH_S_TRACKS][3];
    uint8_t trk_voices[SYNTH_S_TRACKS];
    uint8_t trk_reserved[4];
    /* 0.17: the drum tracks' sounds (D1..D4): a drum engine (BASS DRUM, SNARE, HI-HAT), OSC's three knobs */
    uint8_t drum_engine[SYNTH_D_TRACKS];
    uint8_t drum_osc[SYNTH_D_TRACKS][3];
} settings_t;
#define SET_SIZE_012 20                  /* the record as 0.12 saved it: up to reserved[] */
#define SET_SIZE_013 32                  /* as 0.13..0.15 saved it: up to fx_reserved[] (one sound) */
#define SET_SIZE_016 56                  /* as 0.16 saved it: up to trk_reserved[] (no drums) */
_Static_assert(sizeof(settings_t) == 72 && SET_SIZE_012 == __builtin_offsetof(settings_t, fx) &&
               SET_SIZE_013 == __builtin_offsetof(settings_t, trk_engine) &&
               SET_SIZE_016 == __builtin_offsetof(settings_t, drum_engine), "the settings record's layout");
/* a fresh FM-1's tracks: a lead, a bass, chords, a pluck; each one voice (MONO), so each plays at once */
static const uint8_t TRACK_ENGINE[SYNTH_S_TRACKS] = {GRAV_ENGINE_WAVESHAPING, GRAV_ENGINE_VA_VCF, GRAV_ENGINE_CHORDS,
                                                     GRAV_ENGINE_STRING};
/* and its kit: a kick, a snare, a closed and an open hi-hat (TIMBRE, MORPH: the decay, HARMONICS), each drum's note
 * (the one a new step plays) */
static const uint8_t DRUM_ENGINE[SYNTH_D_TRACKS] = {GRAV_ENGINE_BASS_DRUM, GRAV_ENGINE_SNARE_DRUM, GRAV_ENGINE_HI_HAT,
                                                    GRAV_ENGINE_HI_HAT};
static const uint8_t DRUM_OSC[SYNTH_D_TRACKS][3] = {{40, 55, 40}, {50, 50, 50}, {70, 25, 50}, {70, 70, 50}};
static const uint8_t DRUM_NOTE[SYNTH_D_TRACKS] = {36, 55, 60, 60};
#define VOICES_DEFAULT SYNTH_POLY_VOICES
#define FIXED_VELOCITY 100u              /* the keys' velocity with VELOCITY off */
static settings_t set;
static uint8_t set_dirty;                /* changed since the last save: SAVE is lit */


/* Parameter pages: UI state only, independent of DSP. */
enum {
    PAGE_OSC = 0,
    PAGE_MOD,
    PAGE_ENV,
    PAGE_GLO,                            /* the firmware and its global settings: VOICES, VELOCITY */
    PAGE_FX_GRAIN,                       /* the effect (FX, again for the next): DENSITY, TIME, SIZE */
    PAGE_FX_SHAPE,                       /* SHAPE, PITCH, FEEDBACK */
    PAGE_FX_MIX,                         /* DRY/WET, REVERB, QUALITY */
    PAGE_DEBUG,                          /* GLO again: dual-core diagnostics */
    PAGE_SEQ,                            /* HOME: the sequence (HOME again: OSC, the sound) */
    PAGE_N
};
#define IS_FX_PAGE(p) ((p) >= PAGE_FX_GRAIN && (p) <= PAGE_FX_MIX)

static uint8_t active_page = PAGE_SEQ;
static uint8_t fx_page = PAGE_FX_GRAIN;  /* the FX page shown last: FX goes back to it */
static uint8_t home_page = PAGE_SEQ;     /* HOME's (SEQ or OSC) and GLO's (GLO or DEBUG) shown last */
static uint8_t glo_page = PAGE_GLO;
static const char *const PAGE_NAME[PAGE_N] = {"OSC", "MOD", "ENV", "GLO", "FX GRAIN", "FX SHAPE", "FX MIX", "DEBUG",
                                              "SEQ"};

/* the controls by their names in plat.h, in its order */
static const char *const BTN_NAME[NB] = {"FX", "SEL", "ENV", "LFO", "EDIT", "GLO", "HOME", "SAVE", "ARP", "SEQ",
                                         "PLAY", "REC", "OCT-", "OCT+"};
static const char *const ENC_NAME[NE] = {"SELECT", "ALGO", "PRESET", "K1", "K2", "K3", "K4"};

/* ----------------------------------------------------------------- voices --- */
/* the voices (MONO or POLY, synth/synth.h); the main loop and MIDI in set them, the audio interrupt plays them */
static synth_t synth;
static synth_event_queue_t synth_queue;
static volatile uint32_t synth_queue_overflows;
static volatile uint32_t synth_emergency_panic;
static uint32_t synth_emergency_panic_seen;
static volatile uint32_t synth_audio_mute;
static volatile uint32_t synth_ota_mute;

static void queue_key_event(synth_event_type_t type, int32_t note, uint32_t velocity, uint32_t track)
{
    synth_event_t event = {type, note, velocity, track};
    if (!synth_event_queue_push(&synth_queue, &event)) {
        synth_queue_overflows++;
        synth_emergency_panic++;
    }
}
static grav_engine_t grav_engine;

/* the sequencer (seq/seq.h): its pattern edited here, played in the audio interrupt */
static seq_t seq;
#define TRK (seq.pat.track[track_sel])  /* the selected track's steps and length */
static uint8_t seq_midi_sent[SYNTH_TRACKS][16];   /* each track's notes whose note-on went out: note-offs owed */
static uint32_t seq_midi_owed;

/* the effect (Beads): its knobs (FX_*), QUALITY, and EDIT's and SEL's states; the audio interrupt reads them */
static volatile int32_t fx_params[FX_N];
static volatile int32_t fx_quality;      /* GRAV_FX_* */
static volatile uint8_t fx_freeze;       /* EDIT: the recording held */
static volatile uint8_t fx_seed;         /* SEL held */
static volatile uint8_t fx_seed_auto = 1;   /* grains on their own (DENSITY's); SEL pressed: by hand, held 2 s: back */
#define SEED_AUTO_MS 2000u
static const char *const QUALITY_NAME[GRAV_FX_QUALITIES] = {"DIGITAL", "VINTAGE", "CLEAN TAPE", "DIRTY TAPE"};

/* Plaits' three knobs (OSC), each track's: 0..100% */
static volatile int32_t osc_params[SYNTH_TRACKS][3];
static volatile uint8_t track_sel;       /* the track selected (SELECT): the keys, MIDI in and the pages play it */
static const char *const TRACK_NAME[SYNTH_TRACKS] = {"S1", "S2", "S3", "S4", "D1", "D2", "D3", "D4"};

static uint8_t *track_engine(uint32_t t)   /* a track's engine, where the settings keep it */
{
    return t < SYNTH_S_TRACKS ? &set.trk_engine[t] : &set.drum_engine[(t - SYNTH_S_TRACKS) % SYNTH_D_TRACKS];
}

/* Parameters for future modulation and envelope DSP. */
static volatile int32_t mod_params[3] = {50, 0, 0};
static const char *const LFO_DEST_NAME[3] = {"TIMBRE", "MORPH", "PITCH"};
/* Keep the saved 0..100 representation compatible with 0.14.0 and earlier. */
static uint32_t lfo_destination(int32_t value) { return (uint32_t)clamp(value / 34, 0, 2); }
static volatile int32_t env_params[3] = {10, 50, 30};

static const char *const PARAM_NAME[PAGE_N][3] = {
    {"TIMBRE", "MORPH", "HARMONICS"},
    {"LFO RATE", "LFO DEPTH", "LFO DEST"},
    {"ATTACK", "DECAY", "RELEASE"},
    {"VOICES", "VELOCITY", "VERSION"},
    {"DENSITY", "TIME", "SIZE"},
    {"SHAPE", "PITCH", "FEEDBACK"},
    {"DRY/WET", "REVERB", "QUALITY"},
    {"", "", ""},
    {"TEMPO", "SWING", "LENGTH"}
};


static int32_t clamp_percent(int32_t value)
{
    if (value < 0) return 0;
    if (value > 100) return 100;
    return value;
}


static volatile uint32_t midi_last, midi_last_port, midi_count;   /* the last message in (shown), and how many */
static void midi_in(void)                    /* audio interrupt: the packets that came in */
{
    uint32_t pkt;
    int port;
    while (plat_midi_in(&pkt, &port)) {
        uint32_t st = (pkt >> 8) & 0xFFu, d1 = (pkt >> 16) & 0x7Fu, d2 = pkt >> 24;
        if (st != 0xF8u && st != 0xFEu) {    /* (clock and active sensing would flood the screen) */
            midi_last = pkt;
            midi_last_port = (uint32_t)port;
            midi_count++;
        }
        if ((st & 0xF0u) == 0x90u && d2) {      /* (any channel: the track selected) */
            synth_audio_mute = 0u;
            synth_track_note_on(&synth, track_sel, (int32_t)d1, d2);
        }
        else if ((st & 0xF0u) == 0x80u || (st & 0xF0u) == 0x90u)
            synth_track_note_off(&synth, track_sel, (int32_t)d1);
        else if (st == 0xF8u)                /* clock, start, continue, stop: the sequencer follows them */
            seq_midi_clock(&seq);
        else if (st == 0xFAu || st == 0xFBu)
            seq_midi_start(&seq, st == 0xFBu);
        else if (st == 0xFCu)
            seq.want = SEQ_WANT_STOP;
    }
}

/* a sequencer event, in the audio interrupt: to its track's voices, and out over USB MIDI on the track's channel
 * (S1..S4: 1..4) while there is room, keeping room for the note-offs owed */
static void seq_apply(const seq_event_t *e)
{
    const uint32_t bit = 1u << (e->note & 7u), at = e->note >> 3, track = e->track % SYNTH_TRACKS;
    switch (e->type) {
    case SEQ_EV_ON:
        synth_audio_mute = 0u;
        synth_track_note_on(&synth, track, e->note, e->vel);
        if (!(seq_midi_sent[track][at] & bit) && plat_midi_out_room(TO_USB) > seq_midi_owed + 2u) {
            plat_midi_send(0x09u | (0x90u | track) << 8 | (uint32_t)e->note << 16 | (uint32_t)e->vel << 24, TO_USB);
            seq_midi_sent[track][at] |= (uint8_t)bit;
            seq_midi_owed++;
        }
        break;
    case SEQ_EV_OFF:
        synth_track_note_off(&synth, track, e->note);
        if (seq_midi_sent[track][at] & bit) {
            plat_midi_send(0x08u | (0x80u | track) << 8 | (uint32_t)e->note << 16, TO_USB);
            seq_midi_sent[track][at] &= (uint8_t)~bit;
            seq_midi_owed--;
        }
        break;
    case SEQ_EV_CLOCK:
    case SEQ_EV_START:
    case SEQ_EV_STOP:
        if (plat_midi_out_room(TO_USB) > seq_midi_owed) {
            const uint32_t st = e->type == SEQ_EV_CLOCK ? 0xF8u : e->type == SEQ_EV_START ? 0xFAu : 0xFCu;
            plat_midi_send(0x0Fu | st << 8, TO_USB);
        }
        break;
    default:
        break;
    }
}

static void render_span(int32_t *lr, uint32_t n, uint32_t master_q12, const grav_beads_params_t *fx);

void app_render(int32_t *lr, uint32_t n, uint32_t master_q12)
{
    synth_event_t event;

    float lfo_rate = 0.05f + (float)mod_params[0] * 0.1995f;
    float lfo_depth = (float)mod_params[1] * 0.01f;
    lfo_set(&synth.mod_lfo, lfo_rate, lfo_depth);

    synth_envelopes(
        &synth,
        (float)env_params[0] * 0.02f,
        (float)env_params[1] * 0.02f,
        0.7f,
        (float)env_params[2] * 0.04f,
        44100.0f
    );

    while (synth_event_queue_pop(&synth_queue, &event)) {
        switch (event.type) {
        case SYNTH_EVENT_NOTE_ON:
            synth_audio_mute = 0u;
            synth_track_note_on(&synth, event.track, event.note, event.velocity);
            break;
        case SYNTH_EVENT_NOTE_OFF:
            synth_track_note_off(&synth, event.track, event.note);
            break;
        case SYNTH_EVENT_PANIC:
            synth_audio_mute = 1u;
            synth_panic(&synth);
            break;
        case SYNTH_EVENT_VOICES: {           /* GLO: the tracks' voices (every voice let go) */
            uint8_t counts[SYNTH_S_TRACKS];
            for (uint32_t t = 0; t < SYNTH_S_TRACKS; t++)
                counts[t] = (uint8_t)((uint32_t)event.note >> (4u * t) & 15u);
            synth_set_tracks(&synth, counts);
            break;
        }
        default:
            break;
        }
    }

    uint32_t emergency = synth_emergency_panic;
    if (emergency != synth_emergency_panic_seen) {
        synth_emergency_panic_seen = emergency;
        synth_audio_mute = 1u;
        synth_panic(&synth);
    }

    midi_in();

    grav_beads_params_t fx = {
        (float)fx_params[FX_DENSITY] * 0.01f, (float)fx_params[FX_TIME] * 0.01f,
        (float)fx_params[FX_SIZE] * 0.01f, (float)fx_params[FX_SHAPE] * 0.01f,
        (float)fx_params[FX_PITCH] * 0.01f,
        (float)fx_params[FX_FEEDBACK] * 0.01f, (float)fx_params[FX_DRY_WET] * 0.01f,
        (float)fx_params[FX_REVERB] * 0.01f,
        (uint8_t)fx_quality, fx_freeze, fx_seed, fx_seed_auto
    };

    /* the sequencer's events in this block: the sound rendered in spans between them, each applied where it falls
     * (on a 24-sample grid), not at the block's start */
    seq_event_t ev[SEQ_EVENTS];
    const uint32_t ne = seq_block(&seq, n, ev, SEQ_EVENTS);
    uint32_t pos = 0, k = 0;
    while (pos < n) {
        while (k < ne && ev[k].at <= pos)
            seq_apply(&ev[k++]);
        const uint32_t end = k < ne && ev[k].at < n ? ev[k].at : n;
        render_span(lr + 2u * pos, end - pos, master_q12, &fx);
        pos = end;
    }
    while (k < ne)
        seq_apply(&ev[k++]);

    if (synth_audio_mute || synth_ota_mute) {
        for (uint32_t i = 0; i < 2u * n; ++i)
            lr[i] = 0;
    }
}

/* n samples of the voices (their LFO, the effect, MASTER), from where the last span left them */
static void render_span(int32_t *lr, uint32_t n, uint32_t master_q12, const grav_beads_params_t *fx)
{
    grav_sound_t sound[SYNTH_TRACKS];
    float lfo_value = lfo_process(&synth.mod_lfo);
    uint32_t lfo_dest = lfo_destination(mod_params[2]);
    float pitch_offset = (lfo_dest == 2) ? lfo_value * 2.0f : 0.0f;

    for (uint32_t t = 0; t < SYNTH_TRACKS; t++) {   /* each track's sound, the one LFO on the synth tracks */
        const int drum = SYNTH_IS_DRUM(t);
        float timbre = (float)osc_params[t][0] * 0.01f;
        float morph = (float)osc_params[t][1] * 0.01f;
        if (lfo_dest == 0 && !drum)
            timbre += lfo_value * 0.5f;
        else if (lfo_dest == 1 && !drum)
            morph += lfo_value * 0.5f;
        if (timbre < 0.0f) timbre = 0.0f;
        if (timbre > 1.0f) timbre = 1.0f;
        if (morph < 0.0f) morph = 0.0f;
        if (morph > 1.0f) morph = 1.0f;
        sound[t].engine = *track_engine(t);
        sound[t].pitch = drum ? 0.0f : pitch_offset;
        sound[t].timbre = timbre;
        sound[t].morph = morph;
        sound[t].harmonics = (float)osc_params[t][2] / 100.0f;
    }

    grav_engine_render_tracks(&grav_engine, lr, n, &synth, master_q12, sound, 0.0f, fx);

    lfo_advance(&synth.mod_lfo, n);
}

/* the render took over 90 % of its time (device/main_fm1.c, APP_OVERLOAD): the oldest voice sounding falls silent
 * rather than the sound breaking up; in the audio interrupt, after the render */
void app_overload(void)
{
    synth_shed(&synth);
    grav_debug.shed = synth.shed_count;
}

void app_ota_mute(int active)
{
    synth_ota_mute = active ? 1u : 0u;
}

void app_panic(void)
{
    synth_audio_mute = 1u;
    queue_key_event(SYNTH_EVENT_PANIC, 0, 0, 0);
}

/* --------------------------------------------------------------- settings --- */
static void drum_defaults(settings_t *s)   /* the drum tracks' kit */
{
    for (uint32_t d = 0; d < SYNTH_D_TRACKS; d++) {
        s->drum_engine[d] = DRUM_ENGINE[d];
        for (uint32_t i = 0; i < 3; i++)
            s->drum_osc[d][i] = DRUM_OSC[d][i];
    }
}

static void track_defaults(settings_t *s)   /* the tracks' sounds: S1 the record's one, the others their own */
{
    for (uint32_t t = 0; t < SYNTH_S_TRACKS; t++) {
        s->trk_engine[t] = t ? TRACK_ENGINE[t] : s->engine_id;
        s->trk_osc[t][0] = t ? 65 : s->osc[0];
        s->trk_osc[t][1] = t ? 50 : s->osc[1];
        s->trk_osc[t][2] = t ? 75 : s->osc[2];
        s->trk_voices[t] = 1;
        s->trk_reserved[t] = 0;
    }
}

static void fx_defaults(settings_t *s)   /* the effect's knobs at rest, off */
{
    for (int i = 0; i < FX_N; ++i)
        s->fx[i] = FX_DEFAULT[i];
    s->fx_quality = GRAV_FX_DIGITAL;
    for (int i = 0; i < 3; ++i)
        s->fx_reserved[i] = 0;
}

static void settings_defaults(void)
{
    set.magic = SET_MAGIC;
    set.octave = 0;
    set.engine_id = 0;
    set.rsv_ble = 0;
    set.rsv[0] = 0;
    set.osc[0] = 65; set.osc[1] = 50; set.osc[2] = 75;
    set.mod[0] = 50; set.mod[1] = 0; set.mod[2] = 0;
    set.env[0] = 10; set.env[1] = 50; set.env[2] = 30;
    set.voices = VOICES_DEFAULT;
    set.velocity_off = 0;
    set.reserved[0] = 0;
    fx_defaults(&set);
    track_defaults(&set);
    drum_defaults(&set);
}

/* the knobs' parameters as they are now, into the settings record (SAVE writes the record) */
static void settings_capture(void)
{
    for (uint32_t t = 0; t < SYNTH_S_TRACKS; t++)
        for (uint32_t i = 0; i < 3; i++)
            set.trk_osc[t][i] = (uint8_t)osc_params[t][i];
    for (uint32_t d = 0; d < SYNTH_D_TRACKS; d++)
        for (uint32_t i = 0; i < 3; i++)
            set.drum_osc[d][i] = (uint8_t)osc_params[SYNTH_S_TRACKS + d][i];
    set.engine_id = set.trk_engine[0];       /* (S1's, where older readers look) */
    for (uint32_t i = 0; i < 3; i++)
        set.osc[i] = set.trk_osc[0][i];
    for (int i = 0; i < 3; ++i) {
        set.mod[i] = (uint8_t)mod_params[i];
        set.env[i] = (uint8_t)env_params[i];
    }
    for (int i = 0; i < FX_N; ++i)
        set.fx[i] = (uint8_t)fx_params[i];
    set.fx_quality = (uint8_t)fx_quality;
}

void app_init(void)
{
    synth_init(&synth);
    synth_event_queue_init(&synth_queue);
    grav_engine_init(&grav_engine);
    settings_t loaded_settings = {0};
    int loaded = plat_store_load(OBJ_SETTINGS, &loaded_settings, sizeof loaded_settings);

    settings_defaults();
    set_dirty = 0;

    if ((loaded == 8 || loaded == SET_SIZE_012 || loaded == SET_SIZE_013 || loaded == SET_SIZE_016 ||
         loaded == (int)sizeof loaded_settings) && loaded_settings.octave >= -2 && loaded_settings.octave <= 2) {

        if ((loaded == (int)sizeof loaded_settings || loaded == SET_SIZE_016 || loaded == SET_SIZE_013 ||
             loaded == SET_SIZE_012) &&
            loaded_settings.magic == SET_MAGIC &&
            loaded_settings.engine_id < GRAV_ENGINE_COUNT) {
            int valid = 1;
            uint32_t voices = 0;
            if (loaded == SET_SIZE_012)      /* (saved before 0.13, before the effect: its knobs at rest) */
                fx_defaults(&loaded_settings);
            if (loaded <= SET_SIZE_013)      /* (saved before 0.16, one sound: S1's, the other tracks their own) */
                track_defaults(&loaded_settings);
            if (loaded <= SET_SIZE_016)      /* (saved before 0.17: the drums' kit as it comes) */
                drum_defaults(&loaded_settings);
            for (uint32_t d = 0; d < SYNTH_D_TRACKS; d++) {
                if (!grav_engine_is_drum(loaded_settings.drum_engine[d]))
                    valid = 0;
                for (uint32_t i = 0; i < 3; i++)
                    if (loaded_settings.drum_osc[d][i] > 100u)
                        valid = 0;
            }
            for (uint32_t t = 0; t < SYNTH_S_TRACKS; t++) {
                voices += loaded_settings.trk_voices[t];
                if (loaded_settings.trk_engine[t] >= GRAV_ENGINE_COUNT || loaded_settings.trk_voices[t] > 4u)
                    valid = 0;
                for (uint32_t i = 0; i < 3; i++)
                    if (loaded_settings.trk_osc[t][i] > 100u)
                        valid = 0;
            }
            if (voices > SYNTH_POLY_VOICES)
                valid = 0;
            for (int i = 0; i < 3; ++i) {
                if (loaded_settings.osc[i] > 100u ||
                    loaded_settings.mod[i] > 100u ||
                    loaded_settings.env[i] > 100u)
                    valid = 0;
            }
            for (int i = 0; i < FX_N; ++i)
                if (loaded_settings.fx[i] > 100u)
                    valid = 0;
            if (loaded_settings.voices > SYNTH_POLY_VOICES || loaded_settings.velocity_off > 1u ||
                loaded_settings.fx_quality >= GRAV_FX_QUALITIES)
                valid = 0;
            if (valid) {
                set = loaded_settings;
                if (set.voices == 0)         /* (saved before 0.12, the byte reserved: the default) */
                    set.voices = VOICES_DEFAULT;
            }
        } else if (loaded == 8 &&
                   loaded_settings.magic == PREV_SET_MAGIC &&
                   loaded_settings.engine_id <= 2u) {
            set.octave = loaded_settings.octave;
            set.engine_id = set.trk_engine[0] = loaded_settings.engine_id;
            set_dirty = 1;
        } else if (loaded == 8 &&
                   loaded_settings.magic == OLD_SET_MAGIC &&
                   loaded_settings.engine_id <= 2u) {
            set.octave = loaded_settings.octave;
            set.engine_id = set.trk_engine[0] = loaded_settings.engine_id == 2u ? 1u : 0u;
            set_dirty = 1;
        }
    }

    for (uint32_t t = 0; t < SYNTH_TRACKS; t++)
        for (uint32_t i = 0; i < 3; i++)
            osc_params[t][i] = t < SYNTH_S_TRACKS ? set.trk_osc[t][i] : set.drum_osc[t - SYNTH_S_TRACKS][i];

    for (int i = 0; i < 3; ++i) {
        mod_params[i] = set.mod[i];
        env_params[i] = set.env[i];
    }
    for (int i = 0; i < FX_N; ++i)
        fx_params[i] = set.fx[i];
    fx_quality = set.fx_quality;
    synth_set_tracks(&synth, set.trk_voices);   /* (before the audio starts: no queue needed) */

    seq_init(&seq);                          /* the pattern saved (0.15's: its one track S1's), or an empty one */
    {
        union { seq_record_t r; seq_pattern_v1_t v1; seq_drum_record_t d; } rec;
        const int n = plat_store_load(OBJ_SEQ, &rec, sizeof rec);
        if (n == (int)sizeof rec.r)
            seq_from_record(&rec.r, &seq.pat);
        else if (n == (int)sizeof rec.v1)
            seq_from_v1(&rec.v1, &seq.pat);
        if (plat_store_load(OBJ_DRUMS, &rec, sizeof rec) == (int)sizeof rec.d)
            seq_from_drum_record(&rec.d, &seq.pat);
    }
#if FM1B_CPU2
    plat_cpu2_set(1);                     /* boot only, before audio/timers start */
#endif
}

/* --------------------------------------------------------------------- UI --- */
static struct {
    char msg[2][24];                     /* a short message over the header */
    uint32_t msg_until;
    uint32_t keys, btn;                  /* held at the last input */
    uint8_t vel[NKEYS], prs[NKEYS];      /* each key's last velocity, and its pressure while held */
    uint8_t note[NKEYS];                 /* the note each held key plays: the octave it was pressed in, so OCTAVE
                                          * while it is held changes the next notes, never this one's note-off */
    int32_t last_key;                    /* the key pressed last: its numbers in the header */
    uint32_t seed_at;                    /* when SEL went down */
    int32_t turns[NE];                   /* detents since boot, per encoder */
    uint32_t turned_at[NE];              /* when each was last turned: it shows lit for a moment */
    uint32_t cpu, xruns, cpu_at;         /* the render's load (% of its time) and late renders, as last shown */
    uint8_t redraw;
    uint32_t debug_at, debug_xruns_base;
    /* the sequencer's (seq) */
    uint8_t step_mode;                   /* SEQ: the white keys are the steps of view_page */
    uint8_t view_page;                   /* the page of 16 steps shown and edited */
    int8_t step_held;                    /* the step a white key holds (K1..K3 edit it); -1: none */
    uint8_t step_was_on, step_edited;    /* (a step on, let go untouched, goes off) */
    uint8_t seq_down, seq_used;          /* SEQ held; and a white key pressed meanwhile (the length: no mode change) */
    uint8_t rec, rec_pos;                /* REC (stopped): the next chord goes to rec_pos */
    uint32_t key_step;                   /* the keys pressed as steps (let go as steps) */
    uint8_t chord_open, chord_n, chord_vel, chord[SEQ_NOTES];   /* the notes held together, as they came */
    uint8_t last_n, last_vel, last_chord[SEQ_NOTES];             /* the last chord: a new step's */
    uint32_t beat_count, beat_at;        /* PLAY's green: the last beat, and the bar's first */
    uint8_t beat_bar;
    int32_t shown_step;                  /* the step the screen shows playing, and whether */
    uint8_t shown_playing, redraw_grid;
    uint8_t key_track[NKEYS];            /* the track each held key plays (its note-off goes there) */
    uint32_t tracks_until;               /* SELECT turned: the tracks' list shown until then */
    uint32_t shown_hits[SEQ_TRACKS], hit_at[SEQ_TRACKS];   /* each track's last note begun: its light flashes */
} ui;

static void text_cat(char *d, const char *s)
{
    while (*d)
        d++;
    while (*s)
        *d++ = *s++;
    *d = 0;
}

static void int_text(int32_t v, char *b)     /* signed, no padding */
{
    char t[12];
    int n = 0;
    uint32_t u = (uint32_t)(v < 0 ? -v : v);
    do
        t[n++] = (char)('0' + u % 10u);
    while ((u /= 10u) && n < 11);
    if (v < 0)
        *b++ = '-';
    while (n)
        *b++ = t[--n];
    *b = 0;
}

void app_say(const char *a, const char *b)
{
    uint32_t i;
    char t[48];
    for (i = 0; i < 23u && a && a[i]; i++)
        ui.msg[0][i] = a[i];
    ui.msg[0][i] = 0;
    for (i = 0; i < 23u && b && b[i]; i++)
        ui.msg[1][i] = b[i];
    ui.msg[1][i] = 0;
    ui.msg_until = plat_ms() + 1300u;
    ui.redraw = 1;
    if (ann_on()) {                          /* and to a screen reader (announce.c, fm1-reader) */
        t[0] = 0;
        text_cat(t, ui.msg[0]);
        if (ui.msg[1][0]) {
            text_cat(t, " ");
            text_cat(t, ui.msg[1]);
        }
        ann_say(t, 0, ANN_NOW);
    }
}

static void octave_text(char *t)
{
    t[0] = set.octave < 0 ? '-' : '+';
    t[1] = (char)('0' + (set.octave < 0 ? -set.octave : set.octave));
    t[2] = 0;
}

/* a screen reader that has just started listening (or asks) hears the whole screen */
static void speak(void)
{
    char t[96], o[4];
    if (ann_on() && ann_where()) {
        octave_text(o);
        t[0] = 0;
        text_cat(t, APP_NAME ". OCTAVE ");
        text_cat(t, o);
        text_cat(t, ". WAVE ");
        text_cat(t, grav_engine_name(*track_engine(track_sel)));
        text_cat(t, ". TRACK ");
        text_cat(t, TRACK_NAME[track_sel]);
        if (midi_count) {                    /* the last MIDI message in, as the screen shows it */
            static const char *const PORT[NPORTS] = {"USB", "BLE", "TRS"};
            static const char HEX[] = "0123456789ABCDEF";
            uint32_t m = midi_last, b;
            char h[10];
            for (b = 0; b < 3u; b++) {
                uint32_t v = (m >> (8u + 8u * b)) & 0xFFu;
                h[3 * b] = HEX[v >> 4];
                h[3 * b + 1] = HEX[v & 15u];
                h[3 * b + 2] = b < 2u ? ' ' : 0;
            }
            text_cat(t, ". MIDI IN ");
            text_cat(t, midi_last_port < NPORTS ? PORT[midi_last_port] : "");
            text_cat(t, " ");
            text_cat(t, h);
        }
        ann_say(t, 0, ANN_NOW);
    }
    ann_tick(plat_ms());
}

static void note_name(int32_t note, char *b)
{
    static const char *const N[] = {"C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B"};
    char o[12];
    b[0] = 0;
    text_cat(b, N[note % 12]);
    int_text(note / 12 - 1, o);
    text_cat(b, o);
}

static void octave_step(int32_t d)
{
    int32_t o = clamp(set.octave + d, -2, 2);
    char t[4];
    if (o == set.octave)
        return;
    set.octave = (int8_t)o;                  /* held keys keep their notes (ui.note) until they are let go */
    set_dirty = 1;
    octave_text(t);
    app_say("OCTAVE", t);
}

#ifndef FELUCCA_ID
#define FELUCCA_ID "HOST"                /* (the host simulator: no package identity) */
#endif

static const char *voices_text(uint32_t voices)   /* GLO VOICES: OFF (none), MONO, or how many */
{
    static const char *const T[] = {"OFF", "MONO", "2", "3", "4"};
    return T[voices <= SYNTH_POLY_VOICES ? voices : 1];
}

static const char *track_voices_text(uint32_t t)   /* a track's: a synth track's VOICES, a drum's own */
{
    return SYNTH_IS_DRUM(t) ? "DRUM" : voices_text(set.trk_voices[t]);
}

/* the selected track's voices, by e: more take the free ones, then one from the track with the most (the last of
 * them first); fewer are left free. Every voice let go, as the layout changes */
static void voices_turn(int32_t e)
{
    uint8_t c[SYNTH_S_TRACKS];
    const uint32_t t = track_sel;
    uint32_t total = 0;
    char a[16];
    if (SYNTH_IS_DRUM(t)) {                  /* a drum track: its one voice, always */
        a[0] = 0;
        text_cat(a, TRACK_NAME[t]);
        text_cat(a, " VOICES");
        app_say(a, "DRUM");
        return;
    }
    for (uint32_t k = 0; k < SYNTH_S_TRACKS; k++)
        total += c[k] = set.trk_voices[k];
    int32_t want = clamp((int32_t)c[t] + e, 0, (int32_t)SYNTH_POLY_VOICES);
    while ((int32_t)c[t] < want) {
        if (total < SYNTH_POLY_VOICES) {
            total++;
        } else {
            uint32_t from = SYNTH_S_TRACKS;
            for (uint32_t k = SYNTH_S_TRACKS; k-- > 0;)
                if (k != t && c[k] && (from == SYNTH_S_TRACKS || c[k] > c[from]))
                    from = k;
            if (from == SYNTH_S_TRACKS)
                break;
            c[from]--;
        }
        c[t]++;
    }
    if ((int32_t)c[t] > want)
        c[t] = (uint8_t)want;
    if (memcmp(c, set.trk_voices, sizeof c)) {
        uint32_t packed = 0;
        memcpy(set.trk_voices, c, sizeof c);
        set_dirty = 1;
        for (uint32_t k = 0; k < SYNTH_S_TRACKS; k++)
            packed |= (uint32_t)c[k] << (4u * k);
        queue_key_event(SYNTH_EVENT_VOICES, (int32_t)packed, 0, 0);   /* (the audio interrupt's to change) */
    }
    a[0] = 0;
    text_cat(a, TRACK_NAME[t]);
    text_cat(a, " VOICES");
    app_say(a, voices_text(c[t]));
}

/* GLO's knobs: K1 VOICES (MONO, 2, 3, 4), K2 VELOCITY (right: on, the keys' own; left: off, fixed), K3 the version */
static void glo_turn(uint32_t index, int32_t e)
{
    if (index == 0u) {
        voices_turn(e);
    } else if (index == 1u) {
        uint8_t off = e < 0;
        if (off != set.velocity_off) {
            set.velocity_off = off;
            set_dirty = 1;
        }
        app_say("VELOCITY", set.velocity_off ? "OFF" : "ON");
    } else {
        app_say("VERSION", APP_VERSION);
    }
}

/* a page's knob: K1..K3 (index 0..2) on OSC, MOD, ENV and the FX pages (not GLO's, nor MIX's QUALITY) */
static volatile int32_t *param_of(uint32_t page, uint32_t index)
{
    if (page == PAGE_OSC)
        return &osc_params[track_sel][index];
    if (page == PAGE_MOD)
        return &mod_params[index];
    if (page == PAGE_ENV)
        return &env_params[index];
    return &fx_params[(page - PAGE_FX_GRAIN) * 3u + index];
}

/* drawn from the middle: DENSITY (no grains there: clocked left, random right) and PITCH (0 there) */
static int param_centred(uint32_t page, uint32_t index)
{
    return (page == PAGE_FX_GRAIN && index == 0u) || (page == PAGE_FX_SHAPE && index == 1u);
}

/* a knob's value as the screen and a screen reader give it: PITCH in semitones (Beads' law), the rest as it is */
static void param_text(uint32_t page, uint32_t index, int32_t value, char *t)
{
    if (page == PAGE_MOD && index == 2u) {
        t[0] = 0;
        text_cat(t, LFO_DEST_NAME[lfo_destination(value)]);
    } else if (page == PAGE_FX_SHAPE && index == 1u) {
        float st = grav_beads_pitch((float)value * 0.01f);
        int32_t s = (int32_t)(st + (st < 0.0f ? -0.5f : 0.5f));
        if (s > 0) {
            t[0] = '+';
            int_text(s, t + 1);
        } else
            int_text(s, t);
    } else
        int_text(value, t);
}

/* MIX's K3: QUALITY, from DIGITAL to DIRTY TAPE (Beads' media) */
static void quality_turn(int32_t e)
{
    int32_t q = clamp(fx_quality + e, 0, GRAV_FX_QUALITIES - 1);
    if (q != fx_quality) {
        fx_quality = q;
        set_dirty = 1;
    }
    app_say("QUALITY", QUALITY_NAME[fx_quality]);
}

static int fx_wanted(void)               /* the effect on: DRY/WET or REVERB up, or frozen */
{
    return fx_params[FX_DRY_WET] > 0 || fx_params[FX_REVERB] > 0 || fx_freeze;
}

/* ------------------------------------------------------------ sequencer UI --- */
static const uint8_t WHITE_KEYS[16] = PLAT_WHITE_KEYS;
static const uint8_t BLACK_KEYS[11] = PLAT_BLACK_KEYS;

static int key_index(const uint8_t *keys, uint32_t n, uint32_t k)   /* a key's place among the white or black ones */
{
    for (uint32_t i = 0; i < n; i++)
        if (keys[i] == k)
            return (int)i;
    return -1;
}

static void step_write(uint32_t s, const seq_step_t *t)   /* a step changed (the audio interrupt reads them) */
{
    uint32_t f = plat_lock();
    TRK.step[s] = *t;
    plat_unlock(f);
    set_dirty = 1;
    ui.redraw = 1;
}

static void chord_text(const seq_step_t *t, char *b)   /* "C4", or "C4+2" for a chord of three */
{
    char n[8];
    note_name(t->note[0], b);
    if (t->n > 1u) {
        text_cat(b, "+");
        int_text((int32_t)t->n - 1, n);
        text_cat(b, n);
    }
}

static void step_say(uint32_t s)
{
    char a[16], b[16];
    a[0] = 0;
    text_cat(a, "STEP ");
    int_text((int32_t)s + 1, b);
    text_cat(a, b);
    if (TRK.step[s].on)
        chord_text(&TRK.step[s], b);
    else
        b[0] = 0, text_cat(b, "OFF");
    app_say(a, b);
}

/* a white key in step mode: its step on (the last chord played) when pressed, off when let go if it was on and its
 * knobs were not turned; with SEQ held, the last step */
static void step_key(uint32_t i, int down)
{
    const uint32_t s = ui.view_page * SEQ_PAGE + i;
    char t[8];
    if (down) {
        if (ui.seq_down) {
            TRK.length = (uint8_t)(s + 1u);
            ui.seq_used = 1;
            set_dirty = 1;
            int_text((int32_t)s + 1, t);
            app_say("LENGTH", t);
            return;
        }
        ui.step_held = (int8_t)s;
        ui.step_was_on = TRK.step[s].on;
        ui.step_edited = 0;
        if (!ui.step_was_on) {
            seq_step_t st = TRK.step[s];
            st.on = 1;
            st.n = SYNTH_IS_DRUM(track_sel) ? 1 : ui.last_n;   /* a drum: its note; a synth track: the last chord */
            for (uint32_t k = 0; k < SEQ_NOTES; k++)
                st.note[k] = SYNTH_IS_DRUM(track_sel) ? DRUM_NOTE[track_sel - SYNTH_S_TRACKS] : ui.last_chord[k];
            st.vel = ui.last_vel;
            st.len = 1;
            step_write(s, &st);
        }
        step_say(s);
    } else if (ui.step_held == (int8_t)s) {
        if (ui.step_was_on && !ui.step_edited) {
            seq_step_t st = TRK.step[s];
            st.on = 0;
            step_write(s, &st);
            step_say(s);
        }
        ui.step_held = -1;
    }
}

/* the held step's knobs: K1 its notes (a semitone a detent), K2 its velocity, K3 its length in steps */
static void step_turn(uint32_t index, int32_t e)
{
    const uint32_t s = (uint32_t)ui.step_held;
    seq_step_t st = TRK.step[s];
    char t[16];
    if (index == 0u) {
        for (uint32_t k = 0; k < st.n; k++)
            st.note[k] = (uint8_t)clamp((int32_t)st.note[k] + e, 0, 127);
        chord_text(&st, t);
        app_say("NOTE", t);
    } else if (index == 1u) {
        st.vel = (uint8_t)clamp((int32_t)st.vel + e, 1, 127);
        int_text(st.vel, t);
        app_say("VELOCITY", t);
    } else {
        st.len = (uint8_t)clamp((int32_t)st.len + e, 1, (int32_t)SEQ_STEPS);
        int_text(st.len, t);
        app_say("LENGTH", t);
    }
    ui.step_edited = 1;
    step_write(s, &st);
}

/* SEQ's knobs: K1 TEMPO, K2 SWING, K3 LENGTH */
static void seq_turn(uint32_t index, int32_t e)
{
    char t[12];
    if (index == 0u) {
        seq.pat.bpm = (uint16_t)clamp((int32_t)seq.pat.bpm + e, SEQ_BPM_MIN, SEQ_BPM_MAX);
        int_text(seq.pat.bpm, t);
        app_say("TEMPO", t);
    } else if (index == 1u) {
        seq.pat.swing = (uint8_t)clamp((int32_t)seq.pat.swing + e, SEQ_SWING_MIN, SEQ_SWING_MAX);
        int_text(seq.pat.swing, t);
        text_cat(t, "%");
        app_say("SWING", t);
    } else {
        TRK.length = (uint8_t)clamp((int32_t)TRK.length + e, 1, (int32_t)SEQ_STEPS);
        int_text(TRK.length, t);
        app_say("LENGTH", t);
    }
    set_dirty = 1;
}

/* SELECT, or a black key in step mode: the track the keys, MIDI in, PRESET, OSC, VOICES and the steps are for */
static void track_select(uint32_t t)
{
    char b[16];
    if (t >= SYNTH_TRACKS)
        return;
    if (t != track_sel) {
        track_sel = (uint8_t)t;
        ui.step_held = -1;
    }
    ui.tracks_until = plat_ms() + 1500u;
    ui.redraw = 1;
    b[0] = 0;
    text_cat(b, grav_engine_name(*track_engine(t)));
    app_say(TRACK_NAME[t], b);
}

/* the keys let go of a chord: it is the next new step's, and with REC (stopped), it is written at rec_pos */
static void chord_done(void)
{
    char a[16], b[8];
    ui.last_n = ui.chord_n;
    for (uint32_t k = 0; k < SEQ_NOTES; k++)
        ui.last_chord[k] = k < ui.chord_n ? ui.chord[k] : ui.chord[0];
    ui.last_vel = ui.chord_vel;
    if (!ui.rec)
        return;
    seq_step_t st = TRK.step[ui.rec_pos];
    st.on = 1;
    st.n = ui.last_n;
    for (uint32_t k = 0; k < SEQ_NOTES; k++)
        st.note[k] = ui.last_chord[k];
    st.vel = ui.last_vel;
    st.len = 1;
    step_write(ui.rec_pos, &st);
    ui.rec_pos = (uint8_t)((ui.rec_pos + 1u) % TRK.length);
    ui.view_page = (uint8_t)(ui.rec_pos / SEQ_PAGE);
    a[0] = 0;
    text_cat(a, "REC STEP ");
    int_text(ui.rec_pos + 1, b);
    text_cat(a, b);
    app_say(a, "");
}

static uint32_t page_button(uint32_t page)   /* the button that opens a page */
{
    return page == PAGE_SEQ || page == PAGE_OSC ? B_HOME : page == PAGE_MOD ? B_LFO : page == PAGE_ENV ? B_ENV :
           page == PAGE_GLO || page == PAGE_DEBUG ? B_GLO : B_FX;
}

/* its blink's period: the first of a button's pages slow, the next faster (FX: GRAIN, SHAPE, MIX) */
static uint32_t page_period(uint32_t page)
{
    return page == PAGE_OSC || page == PAGE_DEBUG || page == PAGE_FX_SHAPE ? 500u : page == PAGE_FX_MIX ? 250u :
           1000u;
}

/* the lights: the buttons (the open page's blinking, PLAY green on the beat, red recording) and, in step mode, the
 * keys (the steps of the page shown) */
static void lights(uint32_t btn, uint32_t keys)
{
    const uint32_t now = plat_ms(), play = 1u << B_PLAY;
    for (uint32_t t = 0; t < SEQ_TRACKS; t++)    /* a track's note begun: its lights flash */
        if (seq.hits[t] != ui.shown_hits[t]) {
            ui.shown_hits[t] = seq.hits[t];
            ui.hit_at[t] = now;
        }
    uint32_t lit = (btn & ~play) | (set_dirty ? 1u << B_SAVE : 0u) | (fx_freeze ? 1u << B_EDIT : 0u) |
                   (ui.step_mode ? 1u << B_SEQ : 0u);
    uint32_t mid = 0, lit_keys = keys, mid_keys = 0, dim_keys = 0;
    if (grav_beads_on()) {
        if (!IS_FX_PAGE(active_page))
            lit |= 1u << B_FX;
        if (fx_seed_auto)
            lit |= 1u << B_SEL;
    }
    {
        const uint32_t b = 1u << page_button(active_page), period = page_period(active_page);
        if (now % period < period / 2u)
            lit |= b;
        else {
            lit &= ~b;
            mid |= b;
        }
    }
    if (ui.rec) {                            /* step recording: PLAY red, REC blinking */
        lit |= play;
        if ((now / 125u) & 1u)
            lit |= 1u << B_REC;
        else
            mid |= 1u << B_REC;
    } else if (seq.playing) {                /* PLAY green on each beat, longer on the bar's first */
        const uint32_t count = seq.count;
        if (count != ui.beat_count) {
            ui.beat_count = count;
            if (count && (count - 1u) % 4u == 0u) {
                ui.beat_at = now;
                ui.beat_bar = (count - 1u) % 16u == 0u;
            }
        }
        if (now - ui.beat_at < (ui.beat_bar ? 120u : 60u))
            lit |= LED_PLAY_GREEN;
    }
    if (ui.step_mode) {                      /* a step: on lit, off within the length mid, past it dim; the one
                                              * playing the other way round; the held one lit */
        const uint32_t length = TRK.length;
        lit_keys = mid_keys = 0;
        for (uint32_t i = 0; i < SEQ_PAGE; i++) {
            const uint32_t s = ui.view_page * SEQ_PAGE + i, bit = 1u << WHITE_KEYS[i];
            const int on = TRK.step[s].on, inside = s < length;
            const int playing = seq.playing && seq.step[track_sel] == (int32_t)s;
            if (ui.step_held == (int8_t)s || (playing ? !on : on && inside))
                lit_keys |= bit;
            else if (inside && !playing)
                mid_keys |= bit;
            else if (!inside)
                dim_keys |= bit;
        }
        for (uint32_t t = 0; t < SYNTH_TRACKS; t++) {   /* black 1..8: the tracks (selected lit, a note flashes) */
            const uint32_t bit = 1u << BLACK_KEYS[t];
            if (t == track_sel || now - ui.hit_at[t] < 80u)
                lit_keys |= bit;
            else if (SYNTH_IS_DRUM(t) || set.trk_voices[t])
                mid_keys |= bit;
            else
                dim_keys |= bit;
        }
    }
    {
        uint32_t dim = ((1u << NB) - 1u) & ~lit & ~mid;
        if (!(lit & LED_PLAY_GREEN) && !(lit & play))
            dim = (dim & ~play) | LED_PLAY_GREEN;   /* PLAY glows green, as the other buttons glow */
        else
            dim &= ~play;
        plat_lights(lit, lit_keys, dim, dim_keys, 2);
        plat_lights_mid(mid, mid_keys);
    }
}

static void input(void)
{
    uint32_t keys, btn = plat_buttons(), k;
    int32_t e;
    touch_poll();                            /* the sensed keys' presses, velocities and pressure */
    keys = touch_keys();
    for (k = 0; k < NKEYS; k++) {            /* key 0 is F3 (MIDI 53) */
        uint32_t bit = 1u << k, v;
        int32_t note = 53 + (int32_t)k + 12 * set.octave;
        if ((keys & bit) && !(ui.keys & bit)) {
            if (ui.step_mode) {              /* step mode: a white key is a step, black 1..8 the tracks */
                int w = key_index(WHITE_KEYS, 16, k), b = key_index(BLACK_KEYS, 11, k);
                ui.key_step |= bit;
                if (w >= 0)
                    step_key((uint32_t)w, 1);
                else if (b >= 0 && b < (int)SYNTH_TRACKS)
                    track_select((uint32_t)b);
                continue;
            }
            v = set.velocity_off ? FIXED_VELOCITY : touch_vel(k);   /* GLO VELOCITY: the keys' own, or fixed */
            ui.vel[k] = (uint8_t)v;
            ui.prs[k] = 0;
            ui.note[k] = (uint8_t)note;
            ui.last_key = (int32_t)k;
            ui.key_track[k] = track_sel;       /* (its note-off to the same track, whatever is selected then) */
            queue_key_event(SYNTH_EVENT_NOTE_ON, note, v, track_sel);
            plat_midi_send(0x09u | (0x90u | track_sel) << 8 | (uint32_t)note << 16 | v << 24, TO_USB);   /* its channel */
            if (!ui.chord_open) {            /* a chord: the notes held together, in the order they came */
                ui.chord_open = 1;
                ui.chord_n = 0;
                ui.chord_vel = (uint8_t)v;
            }
            if (ui.chord_n < SEQ_NOTES)
                ui.chord[ui.chord_n++] = (uint8_t)clamp(note, 0, 127);
        } else if (!(keys & bit) && (ui.keys & bit)) {
            if (ui.key_step & bit) {         /* (pressed as a step: let go as one, whatever the mode now) */
                int w = key_index(WHITE_KEYS, 16, k);
                ui.key_step &= ~bit;
                if (w >= 0)
                    step_key((uint32_t)w, 0);
                continue;
            }
            note = ui.note[k];
            ui.prs[k] = 0;
            queue_key_event(SYNTH_EVENT_NOTE_OFF, note, 0, ui.key_track[k]);
            plat_midi_send(0x08u | (0x80u | ui.key_track[k]) << 8 | (uint32_t)note << 16, TO_USB);
        } else if ((keys & bit) && !(ui.key_step & bit) && touch_pressure(k) != ui.prs[k]) {   /* poly aftertouch */
            note = ui.note[k];
            ui.prs[k] = touch_pressure(k);
            ui.redraw = 1;
            plat_midi_send(0x0Au | (0xA0u | ui.key_track[k]) << 8 | (uint32_t)note << 16 | (uint32_t)ui.prs[k] << 24,
                           TO_USB);
        }
    }
    if (ui.chord_open && !(keys & ~ui.key_step)) {   /* every note let go: the chord is done */
        ui.chord_open = 0;
        chord_done();
    }
    for (k = 0; k < NB; k++) {               /* a button pressed: its name, and what it does here */
        uint32_t bit = 1u << k;
        if (!(btn & bit) || (ui.btn & bit))
            continue;
        if (k == B_HOME || k == B_LFO || k == B_ENV || k == B_GLO || k == B_FX) {
            if (k == B_FX && IS_FX_PAGE(active_page))   /* FX again: the next of its pages */
                fx_page = active_page == PAGE_FX_MIX ? PAGE_FX_GRAIN : (uint8_t)(active_page + 1u);
            if (k == B_HOME)                 /* HOME: SEQ, again OSC (the sound), and back */
                home_page = active_page == PAGE_SEQ ? PAGE_OSC : active_page == PAGE_OSC ? PAGE_SEQ : home_page;
            if (k == B_GLO)                  /* GLO: GLO, again DEBUG, and back */
                glo_page = active_page == PAGE_GLO ? PAGE_DEBUG : active_page == PAGE_DEBUG ? PAGE_GLO : glo_page;
            active_page = k == B_HOME ? home_page :
                          k == B_LFO  ? PAGE_MOD :
                          k == B_ENV  ? PAGE_ENV :
                          k == B_GLO  ? glo_page : fx_page;
            app_say("PAGE", PAGE_NAME[active_page]);
            ui.redraw = 1;
        } else if (k == B_PLAY) {            /* the sequencer: start, stop */
            if (seq.playing) {
                seq.want = SEQ_WANT_STOP;
                app_say("STOP", "");
            } else {
                ui.rec = 0;
                seq.want = SEQ_WANT_PLAY;
                app_say("PLAY", seq.ext ? "MIDI CLOCK" : "");
            }
        } else if (k == B_REC) {             /* stopped: step recording, the keys' notes and chords one step each */
            if (seq.playing) {
                app_say("REC", "STOP FIRST");
            } else {
                char a[16], b[8];
                ui.rec = !ui.rec;
                if (ui.rec) {
                    ui.step_mode = 0;
                    ui.step_held = -1;
                    active_page = home_page = PAGE_SEQ;
                    ui.rec_pos = (uint8_t)(ui.view_page * SEQ_PAGE < TRK.length ? ui.view_page * SEQ_PAGE : 0u);
                    a[0] = 0;
                    text_cat(a, "REC STEP ");
                    int_text(ui.rec_pos + 1, b);
                    text_cat(a, b);
                    app_say(a, "");
                } else
                    app_say("REC", "OFF");
            }
        } else if (k == B_SEQ) {             /* step mode on or off when let go; held, a white key is the length */
            ui.seq_down = 1;
            ui.seq_used = 0;
        } else if (k == B_EDIT) {            /* FREEZE: the effect's recording held, or let go */
            fx_freeze = !fx_freeze;
            app_say("FREEZE", fx_freeze ? "ON" : "OFF");
        } else if (k == B_SEL) {             /* SEED: a grain now, and grains by hand from here on */
            ui.seed_at = plat_ms();
            fx_seed = 1;
            app_say("SEED", fx_seed_auto ? "MANUAL" : "");
            fx_seed_auto = 0;
        } else if ((k == B_OCTDN || k == B_OCTUP) && ui.step_mode) {   /* step mode: the pages */
            char t[4];
            ui.view_page = (uint8_t)clamp((int32_t)ui.view_page + (k == B_OCTUP ? 1 : -1), 0, SEQ_PAGES - 1);
            int_text(ui.view_page + 1, t);
            app_say("PAGE", t);
        } else if (k == B_OCTDN)
            octave_step(-1);
        else if (k == B_OCTUP)
            octave_step(1);
        else if (k == B_SAVE) {
            settings_capture();
            if (plat_store_save(OBJ_SETTINGS, &set, sizeof set) == 0 &&   /* (the pattern's records as they lie) */
                plat_store_save(OBJ_SEQ, seq_record_of(&seq.pat), sizeof(seq_record_t)) == 0 &&
                plat_store_save(OBJ_DRUMS, seq_drum_record_of(&seq.pat), sizeof(seq_drum_record_t)) == 0) {
                set_dirty = 0;
                app_say("SAVED", "");
            } else {
                app_say("SAVE", "FAILED");
            }
        } else {
            app_say("BUTTON", BTN_NAME[k]);
        }
    }
    if (ui.seq_down && !(btn & 1u << B_SEQ)) {   /* SEQ let go: step mode on or off, unless it set the length */
        ui.seq_down = 0;
        if (!ui.seq_used) {
            ui.step_mode = !ui.step_mode;
            ui.step_held = -1;
            if (ui.step_mode) {
                ui.rec = 0;
                active_page = home_page = PAGE_SEQ;
            }
            app_say("STEP", ui.step_mode ? "ON" : "OFF");
        }
    }
    if (!(btn & 1u << B_SEL))                /* SEL let go; held 2 s: the grains on their own again */
        fx_seed = 0;
    else if (!fx_seed_auto && plat_ms() - ui.seed_at >= SEED_AUTO_MS) {
        fx_seed_auto = 1;
        app_say("SEED", "AUTO");
    }
    for (k = 0; k < NE; k++) {               /* an encoder turned: its count; PRESET also changes the wave */
        char t[12];
        if ((e = plat_enc((int)k)) == 0)
            continue;
        ui.turns[k] += e;
        ui.turned_at[k] = plat_ms();
        ui.redraw = 1;
        if ((k == EN_K1 || k == EN_K2 || k == EN_K3) && ui.step_held >= 0) {
            step_turn((uint32_t)(k - EN_K1), e);
        } else if ((k == EN_K1 || k == EN_K2 || k == EN_K3 || k == EN_K4) && active_page == PAGE_DEBUG) {
            if (k == EN_K1) {
                grav_dual_core = e > 0;
                app_say("CORES", grav_dual_core ? "2" : "1");
            } else if (k == EN_K2) {
                grav_debug_reset = 1;
                plat_audio_debug_reset();
                ui.debug_xruns_base = plat_xruns();
                app_say("DEBUG", "RESET");
            }
        } else if ((k == EN_K1 || k == EN_K2 || k == EN_K3) && active_page == PAGE_SEQ) {
            seq_turn((uint32_t)(k - EN_K1), e);
        } else if (k == EN_SELECT) {         /* the track (S1..S4), on every page; recording: the step */
            if (ui.rec) {
                ui.rec_pos = (uint8_t)(((int32_t)ui.rec_pos + e % (int32_t)TRK.length + (int32_t)TRK.length) %
                                       (int32_t)TRK.length);
                ui.view_page = (uint8_t)(ui.rec_pos / SEQ_PAGE);
                int_text(ui.rec_pos + 1, t);
                app_say("REC STEP", t);
            } else
                track_select((uint32_t)clamp((int32_t)track_sel + e, 0, SYNTH_TRACKS - 1));
        } else if ((k == EN_K1 || k == EN_K2 || k == EN_K3) && active_page == PAGE_GLO) {
            glo_turn((uint32_t)(k - EN_K1), e);
        } else if (k == EN_K3 && active_page == PAGE_FX_MIX) {
            quality_turn(e);
        } else if (k == EN_K3 && active_page == PAGE_MOD) {
            uint32_t destination = (uint32_t)clamp((int32_t)lfo_destination(mod_params[2]) + e, 0, 2);
            int32_t value = (int32_t)destination * 34;
            if (value != mod_params[2]) {
                mod_params[2] = value;
                set_dirty = 1;
            }
            app_say("LFO DEST", LFO_DEST_NAME[destination]);
        } else if (k == EN_K1 || k == EN_K2 || k == EN_K3) {
            uint32_t index = (uint32_t)(k - EN_K1);
            volatile int32_t *value = param_of(active_page, index);
            int32_t previous = *value;
            *value = clamp_percent(previous + e);
            if (*value != previous)
                set_dirty = 1;
            param_text(active_page, index, *value, t);
            app_say(PARAM_NAME[active_page][index], t);
        } else if (k == EN_PRESET) {
            uint8_t *engine = track_engine(track_sel);   /* Plaits' order; a drum track: its three drums */
            *engine = (uint8_t)(SYNTH_IS_DRUM(track_sel) ? grav_drum_step(*engine, e) : grav_engine_step(*engine, e));
            set_dirty = 1;
            app_say("WAVE", grav_engine_name(*track_engine(track_sel)));
        } else {
            int_text(ui.turns[k], t);
            app_say(ENC_NAME[k], t);
        }
    }
    if (keys != ui.keys || btn != ui.btn)
        ui.redraw = 1;
    ui.keys = keys;
    ui.btn = btn;
    lights(btn, keys);
}

void app_input_only(void) { input(); }

static void cell(int32_t x, int32_t y, int32_t w, const char *s, int on)   /* a labelled box, lit when on */
{
    cv_rect(x, y, w, 18, on ? C_HI : C_LINE);
    cv_text(x + (w - text_w(&FONT_XS, s)) / 2, y + 2, &FONT_XS, s, on ? C_BLACK : C_GRAY);
}

static void key_cell(int32_t x, int32_t y, int32_t w, int32_t h, uint32_t k, uint16_t off)
{
    int32_t v, p;
    cv_rect(x, y, w, h, off);
    if (!(ui.keys >> k & 1u))
        return;
    v = (ui.vel[k] * h + 126) / 127;
    p = (ui.prs[k] * h + 126) / 127;
    cv_rect(x, y + h - v, w, v, C_HI);
    if (p)
        cv_rect(x + 2, y + h - p, w - 4, p, C_WHITE);
}

/* OSC, MOD, ENV and the FX pages: each knob's name, value and bar; QUALITY by its name, one of four */
static void draw_knobs(void)
{
    char t[12];
    for (uint32_t i = 0; i < 3u; i++) {
        int32_t x = 8 + (int32_t)i * 78;
        cv_text(x, 42, &FONT_XS, PARAM_NAME[active_page][i], C_GRAY);
        if (active_page == PAGE_MOD && i == 2u) {
            uint32_t destination = lfo_destination(mod_params[2]);
            cv_text(x, 66, &FONT_S, LFO_DEST_NAME[destination], C_WHITE);
            for (uint32_t d = 0; d < 3; ++d)
                cv_rect(x + (int32_t)d * 24, 100, 20, 3, d == destination ? C_HI : C_LINE);
            continue;
        }
        if (active_page == PAGE_FX_MIX && i == 2u) {
            cv_text(x, 66, &FONT_S, QUALITY_NAME[fx_quality], C_WHITE);
            for (int32_t q = 0; q < GRAV_FX_QUALITIES; q++)
                cv_rect(x + q * 18, 100, 14, 3, q == fx_quality ? C_HI : C_LINE);
            continue;
        }
        int32_t value = *param_of(active_page, i);
        param_text(active_page, i, value, t);
        cv_text(x, 62, &FONT_M, t, C_WHITE);
        cv_rect(x, 100, 68, 3, C_LINE);
        if (param_centred(active_page, i)) { /* from the middle, its mark there */
            int32_t at = value * 68 / 100;
            int32_t a = at < 34 ? at : 34, b = at < 34 ? 34 : at;
            if (b > a)
                cv_rect(x + a, 100, b - a, 3, C_HI);
            cv_rect(x + 33, 97, 2, 9, C_GRAY);
        } else if (value > 0)
            cv_rect(x, 100, value * 68 / 100, 3, C_HI);
    }
    if (IS_FX_PAGE(active_page)) {           /* the effect's state: SEED's way and on or off */
        cv_text(8, 116, &FONT_XS, "SEED", C_GRAY);
        cv_text(40, 116, &FONT_XS, fx_seed_auto ? "AUTO" : "MANUAL", C_WHITE);
        cv_text(164, 116, &FONT_XS, "FX", C_GRAY);
        cv_text(184, 116, &FONT_XS, fx_wanted() ? "ON" : "OFF", fx_wanted() ? C_HI : C_WHITE);
    }
}

/* SEQ's steps: the 16 of the page shown, from y0 (52 high): a step on fills to its velocity, a tick at its note's
 * height among the pattern's notes; the one playing on a light ground; the one held or recorded at marked; past the
 * length a dot. Under them the four pages (the one shown amber, the one playing white) and the step playing */
#define GRID_H 52
static void draw_grid(int32_t y0)
{
    char t[12];
    const uint32_t length = TRK.length, page = ui.view_page;
    const int32_t playing = seq.playing ? seq.step[track_sel] : -1;
    int32_t lo = 127, hi = 0;
    for (uint32_t s = 0; s < length; s++)    /* the notes' range, for the ticks' heights */
        if (TRK.step[s].on) {
            if (TRK.step[s].note[0] < lo) lo = TRK.step[s].note[0];
            if (TRK.step[s].note[0] > hi) hi = TRK.step[s].note[0];
        }
    for (uint32_t i = 0; i < SEQ_PAGE; i++) {
        const uint32_t s = page * SEQ_PAGE + i;
        const seq_step_t *st = &TRK.step[s];
        const int32_t x = 8 + (int32_t)i * 14, y = y0 + 2, h = 40;
        if (s >= length) {                   /* past the length */
            cv_rect(x + 5, y + h - 3, 3, 3, C_DIM);
            continue;
        }
        if (playing == (int32_t)s)
            cv_rect(x - 1, y - 1, 14, h + 2, C_LINE);
        if (ui.step_held == (int8_t)s || (ui.rec && ui.rec_pos == s))
            cv_rect(x - 1, y - 2, 14, 2, ui.rec ? RGB(240, 55, 45) : C_WHITE);
        if (st->on) {
            const int32_t v = 6 + (int32_t)st->vel * (h - 6) / 127;
            cv_rect(x, y + h - v, 12, v, playing == (int32_t)s ? C_WHITE : C_GRAY);
            const int32_t at = hi > lo ? (int32_t)(st->note[0] - lo) * (h - 4) / (hi - lo) : (h - 4) / 2;
            cv_rect(x, y + h - 3 - at, 12, 2, C_HI);
            if (st->n > 1u)                  /* a chord: a dot for each note over the first */
                for (uint32_t k = 1; k < st->n; k++)
                    cv_rect(x + 1 + (int32_t)(k - 1) * 4, y + 1, 2, 2, C_HI);
        } else {
            cv_rect(x, y + h - 2, 12, 2, C_LINE);
        }
    }
    for (uint32_t p = 0; p < SEQ_PAGES; p++) {
        const int32_t x = 8 + (int32_t)p * 22;
        const uint16_t c = p == page ? C_HI : playing >= 0 && (uint32_t)playing / SEQ_PAGE == p ? C_WHITE :
                           p * SEQ_PAGE < length ? C_LINE : C_DIM;
        cv_rect(x, y0 + 46, 18, 4, c);
    }
    {                                        /* the tracks (S1..S4, then D1..D4): the selected framed, a note flashes */
        const uint32_t now = plat_ms();
        for (uint32_t t = 0; t < SEQ_TRACKS; t++) {
            const int32_t x = 100 + (int32_t)t * 10 + (SYNTH_IS_DRUM(t) ? 4 : 0);
            if (t == track_sel)
                cv_rect(x - 1, y0 + 42, 9, 10, C_WHITE);
            cv_rect(x, y0 + 43, 7, 8, now - ui.hit_at[t] < 100u ? C_HI :
                    SYNTH_IS_DRUM(t) || set.trk_voices[t] ? C_LINE : C_DIM);
        }
    }
    if (playing >= 0) {                      /* the step playing, of the length */
        int_text(playing + 1, t);
        text_cat(t, "/");
        char l[4];
        int_text((int32_t)length, l);
        text_cat(t, l);
        cv_text(190, y0 + 41, &FONT_XS, t, C_WHITE);
    }
}

/* SEQ: the steps; under them TEMPO, SWING, LENGTH (a step held: its NOTE, VELOCITY, LENGTH); the state below */
static void draw_seq(void)
{
    static const char *const HELD[3] = {"NOTE", "VELOCITY", "LENGTH"};
    char t[16];
    draw_grid(24);
    for (uint32_t i = 0; i < 3u; i++) {
        const int32_t x = 8 + (int32_t)i * 78;
        int32_t fill;
        if (ui.step_held >= 0) {
            const seq_step_t *st = &TRK.step[(uint32_t)ui.step_held];
            cv_text(x, 80, &FONT_XS, HELD[i], C_GRAY);
            if (i == 0u)
                chord_text(st, t);
            else
                int_text(i == 1u ? st->vel : st->len, t);
            fill = i == 0u ? st->note[0] * 68 / 127 : i == 1u ? st->vel * 68 / 127 : st->len * 68 / (int32_t)SEQ_STEPS;
        } else {
            cv_text(x, 80, &FONT_XS, PARAM_NAME[PAGE_SEQ][i], C_GRAY);
            int_text(i == 0u ? (int32_t)seq_bpm(&seq) : i == 1u ? seq.pat.swing : TRK.length, t);
            if (i == 1u)
                text_cat(t, "%");
            fill = i == 0u ? ((int32_t)seq_bpm(&seq) - (int32_t)SEQ_BPM_MIN) * 68 / (int32_t)(SEQ_BPM_MAX - SEQ_BPM_MIN) :
                   i == 1u ? (seq.pat.swing - (int32_t)SEQ_SWING_MIN) * 68 / (int32_t)(SEQ_SWING_MAX - SEQ_SWING_MIN) :
                   TRK.length * 68 / (int32_t)SEQ_STEPS;
        }
        cv_text(x, 94, &FONT_M, t, C_WHITE);
        cv_rect(x, 120, 68, 3, C_LINE);
        if (fill > 0)
            cv_rect(x, 120, fill, 3, C_HI);
    }
    cv_text(8, 128, &FONT_XS, seq.playing ? "PLAY" : "STOP", seq.playing ? C_HI : C_GRAY);
    if (seq.ext)
        cv_text(44, 128, &FONT_XS, "MIDI CLOCK", C_WHITE);
    if (ui.rec) {
        int_text(ui.rec_pos + 1, t);
        cv_text(140, 128, &FONT_XS, "REC", RGB(240, 55, 45));
        cv_text(170, 128, &FONT_XS, t, C_WHITE);
    } else if (ui.step_mode)
        cv_text(140, 128, &FONT_XS, "STEP", C_HI);
}

/* SELECT turned: the four tracks for a moment, over the page; the selected one lit */
static void draw_tracks(void)
{
    cv_rect(4, 20, 232, 8 * 14 + 6, C_LINE);
    cv_rect(5, 21, 230, 8 * 14 + 4, C_BLACK);
    for (uint32_t t = 0; t < SYNTH_TRACKS; t++) {
        const int32_t y = 23 + (int32_t)t * 14;
        const int sel = t == track_sel;
        if (sel)
            cv_rect(7, y, 226, 14, C_HI);
        cv_text(12, y + 1, &FONT_XS, TRACK_NAME[t], sel ? C_BLACK : C_WHITE);
        cv_text(42, y + 1, &FONT_XS, grav_engine_name(*track_engine(t)), sel ? C_BLACK : C_HI);
        cv_text(170, y + 1, &FONT_XS, track_voices_text(t), sel ? C_BLACK : C_GRAY);
    }
}

/* only the steps (the step playing moved): their strip, not the whole screen */
static void draw_seq_grid(void)
{
    cv_begin(240, GRID_H, C_BLACK);
    draw_grid(0);
    cv_blit(0, 36 + 24);
}

/* GLO: VOICES and VELOCITY as the other pages' knobs are drawn, the version beside them, the identity below */
static void draw_glo(void)
{
    for (uint32_t i = 0; i < 3u; i++) {
        int32_t x = 8 + (int32_t)i * 78;
        cv_text(x, 42, &FONT_XS, PARAM_NAME[PAGE_GLO][i], C_GRAY);
        if (i == 2u) {                       /* (the small font: a version has dots) */
            cv_text(x, 66, &FONT_S, APP_VERSION, C_WHITE);
            continue;
        }
        const uint32_t voices = SYNTH_IS_DRUM(track_sel) ? 1u : set.trk_voices[track_sel];
        int32_t fill = i == 0u ? (int32_t)voices * 68 / (int32_t)SYNTH_POLY_VOICES : set.velocity_off ? 0 : 68;
        cv_text(x, 62, &FONT_M, i == 0u ? track_voices_text(track_sel) : set.velocity_off ? "OFF" : "ON", C_WHITE);
        cv_rect(x, 100, 68, 3, C_LINE);
        if (fill > 0)
            cv_rect(x, 100, fill, 3, C_HI);
    }
    cv_text(8, 116, &FONT_XS, "ID", C_GRAY);
    cv_text(26, 116, &FONT_XS, FELUCCA_ID, C_WHITE);
    cv_text(140, 116, &FONT_XS, "24 ENGINES", C_GRAY);
}

/* DEBUG times are microseconds; AVG/PEAK are the whole render's deadline usage.
 * CPU0 cannot run the UI until the audio ISR has joined CPU1, so this short lock
 * obtains a completed block without displaying half-updated worker results. */
static void debug_number(int32_t x, int32_t y, const char *label, uint32_t value, int percent)
{
    char t[16];
    int_text((int32_t)value, t);
    if (percent) text_cat(t, "%");
    cv_text(x, y, &FONT_T, label, C_GRAY);
    cv_text(x + 46, y, &FONT_T, t, C_WHITE);
}

static void draw_debug(void)
{
    grav_debug_t d;
    plat_audio_debug_t audio;
    uint32_t f = plat_lock();
    d = grav_debug;
    plat_unlock(f);
    plat_audio_debug(&audio);
    const uint32_t budget = audio.budget_us ? audio.budget_us : APP_BLOCK * 1000000u / 44100u;
    int on = 0, error = 0;
    uint32_t bad = 0, stack = 0;
#if FM1B_CPU2
    on = plat_cpu2_on();
    error = plat_cpu2_gave_up();
    bad = plat_cpu2_bad();
    stack = plat_cpu2_stack();
#endif
    debug_number(8, 24, "CORES", grav_dual_core && on ? 2 : 1, 0);
    cv_text(126, 24, &FONT_T, "CPU1", C_GRAY);
    cv_text(172, 24, &FONT_T, on ? (grav_dual_core ? "ON" : "IDLE") : "OFF", on ? C_HI : RGB(240,55,45));
    debug_number(8, 36, "AVG", ui.cpu, 1);
    debug_number(126, 36, "PEAK", audio.peak_us * 100u / budget, 1);
    debug_number(8, 48, "SYN0", d.voice_us[0], 0);
    debug_number(126, 48, "SYN1", d.voice_us[1], 0);
    debug_number(8, 60, d.fx_core ? "FX1" : "FX0", d.fx_us, 0);   /* the effect's time, on CPU1 or CPU0 */
    debug_number(126, 60, "WAIT", d.wait_us, 0);
    debug_number(8, 72, "XRUN", plat_xruns() - ui.debug_xruns_base, 0);
    debug_number(126, 72, "SHED", d.shed, 0);
    debug_number(8, 84, "STEAL", d.steals, 0);
    debug_number(126, 84, "OVER", audio.overloads, 0);
    debug_number(8, 96, "VOICES", d.active, 0);
    debug_number(126, 96, "JOBS", d.jobs, 0);
    debug_number(8, 108, "MASK0", d.masks[0], 0);
    debug_number(126, 108, "MASK1", d.masks[1], 0);
    debug_number(8, 120, error ? "ERROR" : "BAD", error ? (uint32_t)error : bad, 0);
    debug_number(126, 120, "STACK", stack, 0);
    cv_text(8, 132, &FONT_T, "K1 CORES   K2 RESET   us", C_HI);
}

static void draw(void)
{
    char t[24];
    uint32_t i;
    uint32_t pct = plat_master() * 100u / 4096u;
    int batt = plat_battery();
    static const uint8_t WK[16] = PLAT_WHITE_KEYS;
    static const uint8_t BK[11] = PLAT_BLACK_KEYS;

    /* Header */
    cv_begin(240, 32, C_BLACK);
    cv_text(6, 6, &FONT_B, "GRAV CODE", C_WHITE);
    cv_text(136, 10, &FONT_XS, PAGE_NAME[active_page], C_HI);




    /* Battery: rounded 26x14 body, terminal and level/USB symbol. */
    {
        const int32_t bx = 202, by = 9;
        uint16_t color = batt >= 3 ? RGB(0, 255, 0) :
                         batt == 2 ? RGB(255, 200, 0) :
                                     RGB(240, 55, 45);

        /* Rounded outline (pixel corners). */
        cv_rect(bx + 2, by, 22, 1, C_GRAY);
        cv_rect(bx + 2, by + 13, 22, 1, C_GRAY);
        cv_rect(bx, by + 2, 1, 10, C_GRAY);
        cv_rect(bx + 25, by + 2, 1, 10, C_GRAY);
        cv_rect(bx + 1, by + 1, 1, 1, C_GRAY);
        cv_rect(bx + 24, by + 1, 1, 1, C_GRAY);
        cv_rect(bx + 1, by + 12, 1, 1, C_GRAY);
        cv_rect(bx + 24, by + 12, 1, 1, C_GRAY);
        cv_rect(bx + 26, by + 5, 3, 4, C_GRAY);

        if (batt == 4) {
            /* Lightning bolt: USB power. */
            cv_line(bx + 14, by + 2, bx + 10, by + 7, RGB(0, 255, 0));
            cv_line(bx + 10, by + 7, bx + 15, by + 7, RGB(0, 255, 0));
            cv_line(bx + 15, by + 7, bx + 11, by + 12, RGB(0, 255, 0));
        } else if (batt >= 0 && batt <= 3) {
            /* Three discrete charge levels; no invented percentages. */
            if (batt > 0)
                cv_rect(bx + 3, by + 3, batt * 7, 8, color);
        } else {
            /* Battery level unavailable: show empty outline. */
        }
    }
    cv_blit(0, 0);

    /* Synth engine and parameter page */
    cv_begin(240, 144, C_BLACK);

    cv_text(8, 4, &FONT_S, TRACK_NAME[track_sel], C_WHITE);   /* the track selected, its engine */
    cv_text(30, 4, &FONT_S, grav_engine_name(*track_engine(track_sel)), C_HI);
    if (fx_freeze)                           /* the effect frozen: on every page (and EDIT lit) */
        cell(172, 2, 60, "FREEZE", 1);

    if (active_page == PAGE_GLO)
        draw_glo();
    else if (active_page == PAGE_DEBUG)
        draw_debug();
    else if (active_page == PAGE_SEQ)
        draw_seq();
    else
        draw_knobs();
    if (ui.tracks_until)
        draw_tracks();

    cv_blit(0, 36);
    /* the keys, black above white: a held key fills to its velocity (amber), its pressure over that (white) */
    cv_begin(240, 60, C_BLACK);
    for (i = 0; i < 16u; i++)
        key_cell(4 + (int32_t)i * 14, 14, 13, 14, WK[i], C_DIM);
    for (i = 0; i < 11u; i++) {
        static const uint8_t AT[11] = {0, 1, 2, 4, 5, 7, 8, 9, 11, 12, 14};   /* the white key each one follows */
        key_cell(4 + (int32_t)AT[i] * 14 + 8, 0, 11, 12, BK[i], C_LINE);
    }
    octave_text(t);
    cv_text(4, 36, &FONT_XS, "OCT", C_GRAY);
    cv_text(30, 36, &FONT_XS, t, C_HI);
    cv_text(64, 36, &FONT_XS, "PRESET", C_GRAY);
    cv_text(112, 36, &FONT_XS, grav_engine_name(*track_engine(track_sel)), C_HI);
    if (active_page == PAGE_DEBUG) {
        debug_number(4, 48, "QUEUE", synth_queue_overflows, 0);
    } else if (midi_count) {                        /* the last MIDI message in, and where from */
        static const char *const PORT[NPORTS] = {"USB", "BLE", "TRS"};
        static const char HEX[] = "0123456789ABCDEF";
        uint32_t m = midi_last, b;
        char h[12];
        for (b = 0; b < 3u; b++) {
            uint32_t v = (m >> (8u + 8u * b)) & 0xFFu;
            h[3 * b] = HEX[v >> 4];
            h[3 * b + 1] = HEX[v & 15u];
            h[3 * b + 2] = b < 2u ? ' ' : 0;
        }
        cv_text(4, 48, &FONT_XS, "MIDI IN", C_GRAY);
        cv_text(52, 48, &FONT_XS, midi_last_port < NPORTS ? PORT[midi_last_port] : "", C_HI);
        cv_text(82, 48, &FONT_XS, h, C_WHITE);
    }
    cv_text(178, 36, &FONT_XS, "VOL", C_GRAY);
    int_text((int32_t)pct, t);
    text_cat(t, "%");
    cv_text(204, 36, &FONT_XS, t, C_HI);
    cv_text(178, 48, &FONT_XS, "CPU", C_GRAY);   /* the render's load; red once a render ran late (heard: a crackle) */
    int_text((int32_t)ui.cpu, t);
    text_cat(t, "%");
    cv_text(204, 48, &FONT_XS, t, ui.xruns ? RGB(240, 55, 45) : C_HI);
    cv_blit(0, 180);
}

void app_ui_init(void)
{
    ui.step_held = -1;                       /* the sequencer's: nothing held, a new step plays C4 at 100 */
    ui.last_n = 1;
    for (uint32_t k = 0; k < SEQ_NOTES; k++)
        ui.last_chord[k] = 60;
    ui.last_vel = 100;
    ui.shown_step = -2;
    touch_on(1);                             /* velocity and pressure: the keys' readings on */
    lcd_fill(0, 0, 240, 240, C_BLACK);
    ui.redraw = 1;
}

void app_frame(void)
{
    static uint32_t last_pct, lit_until, last_midi;
    static int last_batt = -2;
    uint32_t i, now = plat_ms(), pct = plat_master() * 100u / 4096u;
    int batt = plat_battery();

    if (batt != last_batt) {
        last_batt = batt;
        ui.redraw = 1;
    }
    input();
    speak();
    for (i = 0; i < NE; i++)                 /* an encoder's light goes out */
        if (ui.turned_at[i] && now - ui.turned_at[i] < 400u && now > lit_until)
            lit_until = ui.turned_at[i] + 300u;
    if (lit_until && now >= lit_until) {
        lit_until = 0;
        ui.redraw = 1;
    }
    if (now - ui.cpu_at >= 500u) {           /* the load, twice a second */
        ui.cpu_at = now;
        if (plat_cpu_pct() != ui.cpu || plat_xruns() != ui.xruns) {
            ui.cpu = plat_cpu_pct();
            ui.xruns = plat_xruns();
            ui.redraw = 1;
        }
    }
    if (active_page == PAGE_DEBUG && now - ui.debug_at >= 250u) {
        ui.debug_at = now;
        ui.redraw = 1;
    }
    if (midi_count != last_midi) {
        last_midi = midi_count;
        ui.redraw = 1;
    }
    if (pct != last_pct || (ui.msg_until && now >= ui.msg_until)) {
        last_pct = pct;
        if (now >= ui.msg_until)
            ui.msg_until = 0;
        ui.redraw = 1;
    }
    if (ui.tracks_until && now >= ui.tracks_until) {   /* the tracks' list goes */
        ui.tracks_until = 0;
        ui.redraw = 1;
    }
    {                                        /* the tracks' lights on SEQ: a flash begun or over */
        static uint32_t flashing;
        uint32_t f = 0;
        for (uint32_t t = 0; t < SEQ_TRACKS; t++)
            f |= (now - ui.hit_at[t] < 100u) << t;
        if (f != flashing && active_page == PAGE_SEQ)
            ui.redraw_grid = 1;
        flashing = f;
    }
    if (active_page == PAGE_SEQ && (seq.step[track_sel] != ui.shown_step || seq.playing != ui.shown_playing)) {
        ui.shown_step = seq.step[track_sel];            /* the step playing moved: its strip, or (stopped) the whole page */
        if (seq.playing != ui.shown_playing)
            ui.redraw = 1;
        ui.shown_playing = seq.playing;
        ui.redraw_grid = 1;
    }
    if (ui.redraw_grid && ui.tracks_until)   /* (the tracks' list over the steps: the whole page, not the strip) */
        ui.redraw = 1;
    if (ui.redraw) {
        ui.redraw = 0;
        ui.redraw_grid = 0;
        draw();
    } else if (ui.redraw_grid) {
        ui.redraw_grid = 0;
        draw_seq_grid();
    }
}

/* F0 7D 47 44 F7 ("GD"): version 1, then 22 unsigned words, five 7-bit bytes each.
 * Read-only companion to DEBUG, so automated hardware checks need no framebuffer allocation. */
int app_sysex(const uint8_t *p, uint32_t n)
{
    if (n != 3 || p[0] != 0x7d || p[1] != 0x47 || p[2] != 0x44)
        return 0;
    grav_debug_t d;
    plat_audio_debug_t a;
    uint32_t f = plat_lock();
    d = grav_debug;
    plat_unlock(f);
    plat_audio_debug(&a);
    uint32_t on = 0, error = 0, bad = 0, stack = 0;
#if FM1B_CPU2
    on = (uint32_t)plat_cpu2_on();
    error = (uint32_t)plat_cpu2_gave_up();
    bad = plat_cpu2_bad();
    stack = plat_cpu2_stack();
#endif
    const uint32_t values[] = {
        grav_dual_core, on, error, d.voice_us[0], d.voice_us[1], d.fx_us, d.wait_us,
        plat_cpu_pct(), a.last_us, a.peak_us, a.budget_us, a.overloads,
        plat_xruns() - ui.debug_xruns_base, d.active, d.masks[0], d.masks[1],
        d.jobs, d.steals, d.shed, synth_queue_overflows, bad, stack
    };
    uint8_t frame[6 + sizeof(values) / sizeof(values[0]) * 5];
    uint32_t k = 0;
    frame[k++] = 0xf0; frame[k++] = 0x7d; frame[k++] = 0x47; frame[k++] = 0x44; frame[k++] = 1;
    for (uint32_t i = 0; i < sizeof(values) / sizeof(values[0]); ++i)
        for (uint32_t shift = 0; shift < 35; shift += 7)
            frame[k++] = (uint8_t)((values[i] >> shift) & 0x7f);
    frame[k++] = 0xf7;
    plat_sysex_send(frame, k);
    return 1;
}

#ifdef PLAT_HOST
void app_host_reboot(void)
{
    memset(&ui, 0, sizeof ui);
    track_sel = 0;
    active_page = home_page = PAGE_SEQ;
    glo_page = PAGE_GLO;
    fx_page = PAGE_FX_GRAIN;
    fx_freeze = fx_seed = 0;
    fx_seed_auto = 1;
    memset(seq_midi_sent, 0, sizeof seq_midi_sent);   /* (every track's) */
    seq_midi_owed = 0;
}

int app_host_cmd(const char *cmd, const char *a, const char *b, const char *c, const char *d)
{
    (void)a; (void)b; (void)c; (void)d;
    if (!strcmp(cmd, "save_moddest")) {
        settings_capture();
        settings_t legacy = set;
        legacy.mod[2] = (uint8_t)clamp(atoi(a), 0, 100);
        plat_store_save(OBJ_SETTINGS, &legacy, sizeof legacy);
        return 1;
    }
    if (!strcmp(cmd, "debug_overload")) {
        app_overload();
        return 1;
    }
    if (!strcmp(cmd, "save012") || !strcmp(cmd, "save016")) {   /* the settings as 0.12 / 0.16 saved them */
        settings_capture();
        plat_store_save(OBJ_SETTINGS, &set, !strcmp(cmd, "save012") ? SET_SIZE_012 : SET_SIZE_016);
        return 1;
    }
    return 0;
}

int app_expect(const char *what, int *got)
{
    if (!strcmp(what, "octave"))
        *got = set.octave;
    else if (!strcmp(what, "dual_core"))
        *got = (int)grav_dual_core;
    else if (!strcmp(what, "worker_jobs"))
        *got = (int)grav_debug.jobs;
    else if (!strcmp(what, "worker_mask"))
        *got = (int)grav_debug.masks[1];
    else if (!strcmp(what, "fx_core"))       /* the core the effect ran on: 1 CPU1, a chunk behind the voices */
        *got = (int)grav_debug.fx_core;
    else if (!strncmp(what, "vgate", 5))     /* vgateN: voice N's note held (0..3 the synth tracks', 4..7 the drums') */
        *got = synth.voice[atoi(what + 5) % SYNTH_VOICES_MAX].gate;
    else if (!strcmp(what, "render_voices"))
        *got = (int)grav_debug.active;
    else if (!strcmp(what, "shed"))
        *got = (int)grav_debug.shed;
    else if (!strcmp(what, "steals"))
        *got = (int)grav_debug.steals;
    else if (!strcmp(what, "wave"))          /* the selected track's engine */
        *got = *track_engine(track_sel);
    else if (!strcmp(what, "track"))         /* the track selected: 0..3 (S1..S4) */
        *got = track_sel;
    else if (!strncmp(what, "trk_voices", 10))   /* trk_voicesN, trk_engineN: track N's (0..3) */
        *got = set.trk_voices[atoi(what + 10) % SYNTH_S_TRACKS];
    else if (!strncmp(what, "trk_engine", 10))   /* (0..7: D1..D4 are 4..7) */
        *got = *track_engine(atoi(what + 10) % SYNTH_TRACKS);
    else if (!strcmp(what, "seq_playing"))   /* the sequencer: playing, the step playing (-1), steps played */
        *got = seq.playing;
    else if (!strcmp(what, "seq_step"))
        *got = seq.step[track_sel];
    else if (!strcmp(what, "seq_count"))
        *got = (int)seq.count;
    else if (!strcmp(what, "seq_bpm"))       /* the tempo playing (the clock in's when following one) */
        *got = (int)seq_bpm(&seq);
    else if (!strcmp(what, "seq_swing"))
        *got = seq.pat.swing;
    else if (!strcmp(what, "seq_length"))
        *got = TRK.length;
    else if (!strcmp(what, "seq_ext"))       /* following MIDI clock in */
        *got = seq.ext;
    else if (!strcmp(what, "seq_page"))      /* the page of steps shown */
        *got = ui.view_page;
    else if (!strcmp(what, "step_mode"))
        *got = ui.step_mode;
    else if (!strcmp(what, "rec"))
        *got = ui.rec;
    else if (!strcmp(what, "rec_pos"))
        *got = ui.rec_pos;
    else if (!strncmp(what, "stepnote", 8))  /* stepnoteN, stepnN, stepvelN, steplenN, stepN: step N (0..63) */
        *got = TRK.step[atoi(what + 8) % SEQ_STEPS].note[0];
    else if (!strncmp(what, "stepn", 5))
        *got = TRK.step[atoi(what + 5) % SEQ_STEPS].n;
    else if (!strncmp(what, "stepvel", 7))
        *got = TRK.step[atoi(what + 7) % SEQ_STEPS].vel;
    else if (!strncmp(what, "steplen", 7))
        *got = TRK.step[atoi(what + 7) % SEQ_STEPS].len;
    else if (!strncmp(what, "step", 4))
        *got = TRK.step[atoi(what + 4) % SEQ_STEPS].on;
    else if (!strcmp(what, "lfo_dest"))
        *got = (int)lfo_destination(mod_params[2]);
    else if (!strcmp(what, "engine_ram"))    /* bytes of the selected track's (first) voice's RAM its engine took */
        *got = (int)grav_plaits_ram_used(synth.track[track_sel].first);
    else if (!strcmp(what, "voices"))        /* GLO VOICES, the selected track's: 0 OFF, 1 MONO, 2..4 POLY; a drum 1 */
        *got = SYNTH_IS_DRUM(track_sel) ? 1 : set.trk_voices[track_sel];
    else if (!strcmp(what, "voices_held"))   /* voices whose note is held */
        *got = (int)synth_voices_held(&synth);
    else if (!strcmp(what, "velocity_off"))  /* GLO VELOCITY off: the keys at a fixed velocity */
        *got = set.velocity_off;
    else if (!strcmp(what, "page"))          /* the page shown (PAGE_*: 4..6 the FX pages) */
        *got = active_page;
    else if (!strcmp(what, "fx_on"))         /* the effect running (not off) */
        *got = grav_beads_on();
    else if (!strcmp(what, "quality"))       /* FX MIX QUALITY: GRAV_FX_* */
        *got = fx_quality;
    else if (!strcmp(what, "freeze"))
        *got = fx_freeze;
    else if (!strcmp(what, "seed_auto"))
        *got = fx_seed_auto;
    else if (!strncmp(what, "fx", 2))        /* fxN: the effect's knob N (FX_*: 0 DENSITY .. 7 REVERB) */
        *got = fx_params[atoi(what + 2) % FX_N];
    else if (!strcmp(what, "note"))
        *got = synth.note;
    else if (!strcmp(what, "dirty"))
        *got = set_dirty;
    else if (!strcmp(what, "vel"))           /* the last key's velocity and pressure */
        *got = ui.vel[ui.last_key];
    else if (!strcmp(what, "prs"))
        *got = ui.prs[ui.last_key];
    else if (!strncmp(what, "turns", 5))    /* turnsN: encoder N's count (plat.h EN_*) */
        *got = ui.turns[atoi(what + 5) % NE];
    else if (!strncmp(what, "osc", 3)) {     /* oscN, modN, envN: parameter N (0..2) of a page, as the knobs left it */
        int i = atoi(what + 3) % 3;
        *got = osc_params[track_sel][i];
    } else if (!strncmp(what, "mod", 3))
        *got = mod_params[atoi(what + 3) % 3];
    else if (!strncmp(what, "env", 3))
        *got = env_params[atoi(what + 3) % 3];
    else
        return 0;
    return 1;
}
#endif
