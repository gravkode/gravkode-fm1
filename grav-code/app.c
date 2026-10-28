/* SPDX-License-Identifier: MPL-2.0
 * Copyright (C) 2026 Charles Vestal */
/* GRAV CODE: up to four voices on Plaits' engines (dsp/), with an envelope and an LFO (synth/), through Beads (the
 * effect). The keys play them with velocity and go out over USB MIDI (MIDI in, USB or TRS, plays them too); ALGO
 * picks the engine, PRESET its sound, K1-K3 the page's parameters (HOME: OSC, LFO: MOD, ENV: ENV, GLO: GLO, FX: the effect's three
 * pages), EDIT freezes the effect, SEL seeds it, OCT- / OCT+ the octave, MASTER the volume, SAVE stores it all. No
 * radio: Bluetooth and WiFi are left out of the build. Part of the unity build (device/unity.c, host/plat_host.c),
 * after the platform: gfx.c and announce.c are in scope. */
#include "fastmath.h"
#include "dsp/grav_engine.h"
#include "dsp/grav_plaits.h"
#include "synth/synth.h"
#include "synth/event_queue.h"
#include "seq/seq.h"
#include "seq/bank.h"
#include "presets/presets.h"
#include "presets/store.h"

_Static_assert(APP_BLOCK <= 256u, "dual-core voice buffers fit APP_BLOCK");

/* the effect's knobs (FX: GRAIN, SHAPE, MIX; QUALITY apart), 0..100 */
enum { FX_DENSITY, FX_TIME, FX_SIZE, FX_SHAPE, FX_PITCH, FX_FEEDBACK, FX_DRY_WET, FX_REVERB, FX_N };
static const uint8_t FX_DEFAULT[FX_N] = {75, 30, 65, 50, 50, 30, 0, 0};   /* (DRY/WET and REVERB 0: off) */

#define SET_MAGIC 0x47525639u
/* GLO's METRONOME: clicks while recording live; OFF; COUNT: and a bar's count-in when recording starts from a stop */
enum { METRO_REC, METRO_OFF, METRO_COUNT };
#define PREV_SET_MAGIC 0x54504C32u
#define OLD_SET_MAGIC 0x54504C31u            /* "TPL1": the settings record; another magic loads defaults */
typedef struct {
    uint32_t magic;
    int8_t octave;                       /* -2 .. +2 */
    uint8_t engine_id;                   /* GRAV_ENGINE_* (dsp/grav_plaits.h): one of Plaits' 24 */
    uint8_t rsv_ble;                     /* was Bluetooth switched off (REC held), when there was Bluetooth: kept,
                                          * unread, for the record's layout (saved settings still load) */
    uint8_t theme;                       /* (0.25.1) GLO's SCREEN: the palette + 1 (device/gfx.c PALETTES); 0 (as saved
                                          * before): the app's, APP_PALETTE */
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
    /* 0.17: the drum tracks' sounds (D1..D4): a drum engine (KICK, SNARE, HI-HAT), OSC's three knobs */
    uint8_t drum_engine[SYNTH_D_TRACKS];
    uint8_t drum_osc[SYNTH_D_TRACKS][3];
    /* 0.18: the song's tempo (every pattern's), and the pattern it was on */
    uint16_t bpm;                        /* SEQ_BPM_MIN..SEQ_BPM_MAX */
    uint8_t pattern;                     /* 0..BANK_PATTERNS - 1 */
    uint8_t metro;                       /* 0.24: GLO's METRONOME, METRO_*: REC (0, as saved before), OFF, COUNT */
    /* 0.19: the drums' knobs beyond TONE and DECAY (drum_osc's TIMBRE and MORPH): TUNE (50: as the note), and SHAPE's
     * three (dsp/grav_drums.h); drum_osc's HARMONICS unread since */
    uint8_t drum_tune[SYNTH_D_TRACKS];
    uint8_t drum_shape[SYNTH_D_TRACKS][3];
    grav_controls_t controls[SYNTH_TRACKS];
    /* 0.26: the mixer (SEL): each track's share through the effect (SEND, 0..100), the tracks muted (a bit each) */
    uint8_t send[SYNTH_TRACKS];
    uint8_t mute;
    uint8_t mix_reserved[7];
    /* 0.27: the mixer's fader on each track (VOL, 0..100; its sound's own LEVEL, OUTPUT's, goes with its preset) */
    uint8_t vol[SYNTH_TRACKS];
} settings_t;
#define SET_SIZE_012 20                  /* the record as 0.12 saved it: up to reserved[] */
#define SET_SIZE_013 32                  /* as 0.13..0.15 saved it: up to fx_reserved[] (one sound) */
#define SET_SIZE_016 56                  /* as 0.16 saved it: up to trk_reserved[] (no drums) */
#define SET_SIZE_017 72                  /* as 0.17 saved it: up to drum_osc[] (the tempo the pattern's) */
#define SET_SIZE_018 76                  /* as 0.18 saved it: up to metro, then reserved (the drums on Plaits'
                                          * three knobs) */
#define SET_SIZE_021 92
#define SET_SIZE_025 1404                /* as 0.21..0.25 saved it: up to the mixer (every track sent whole, none muted) */
#define SET_SIZE_026 1420                /* as 0.26 saved it: up to the faders (each at 100) */
_Static_assert(sizeof(settings_t) == 1428 && SET_SIZE_026 == __builtin_offsetof(settings_t, vol) &&
               SET_SIZE_025 == __builtin_offsetof(settings_t, send) &&
               SET_SIZE_021 == __builtin_offsetof(settings_t, controls) && SET_SIZE_018 == __builtin_offsetof(settings_t, drum_tune) && SET_SIZE_012 == __builtin_offsetof(settings_t, fx) &&
               SET_SIZE_013 == __builtin_offsetof(settings_t, trk_engine) &&
               SET_SIZE_016 == __builtin_offsetof(settings_t, drum_engine) &&
               SET_SIZE_017 == __builtin_offsetof(settings_t, bpm), "the settings record's layout");
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
static settings_t set_stored;            /* as the flash has it: SAVE writes the settings only when they differ */
static uint8_t set_dirty;                /* changed since the last save: SAVE is lit (or the pattern unsaved) */
static bank_t bank;                      /* the patterns in the flash (seq/bank.h) */
static uint32_t used_mask, used_seq;     /* the bank's patterns with a step on, as of its save used_seq (patterns_used) */

/* Parameter pages: UI state only, independent of DSP. */
enum {
    PAGE_OSC = 0,
    PAGE_MOD,
    PAGE_ENV,
    PAGE_GLO,                            /* the firmware and its global settings: VOICES, VELOCITY */
    PAGE_FX_GRAIN,                       /* the effect (FX: MIX first since 0.26.2, again GRAIN, SHAPE): DENSITY, TIME,
                                          * SIZE */
    PAGE_FX_SHAPE,                       /* SHAPE, PITCH, FEEDBACK */
    PAGE_FX_MIX,                         /* DRY/WET, REVERB, QUALITY */
    PAGE_DEBUG,                          /* GLO again: dual-core diagnostics */
    PAGE_SEQ,                            /* HOME: the sequence (HOME again: OSC, the sound) */
    PAGE_SHAPE,                          /* a drum track's: HOME after its DRUM page (OSC's): SHAPE's knobs */
    PAGE_SOUND,
    PAGE_OUTPUT,
    PAGE_SCREEN,                         /* (0.25.1) GLO again: THEME, the screen's colours (GLO again: DEBUG) */
    PAGE_MIXER,                          /* (0.26) SEL: the eight tracks' LEVEL, SEND, mutes; the effect's RETURN */
    PAGE_N
};
#define IS_FX_PAGE(p) ((p) >= PAGE_FX_GRAIN && (p) <= PAGE_FX_MIX)
#define IS_SOUND_PAGE(p) ((p) == PAGE_OSC || (p) == PAGE_SHAPE || (p) == PAGE_SOUND || (p) == PAGE_OUTPUT)

static uint8_t active_page = PAGE_SEQ;
static uint8_t fx_page = PAGE_FX_MIX;    /* the FX page shown last: FX goes back to it (0.26.2: FX MIX first) */
static uint8_t home_page = PAGE_SEQ;     /* HOME's (SEQ or OSC) and GLO's (GLO or DEBUG) shown last */
static uint8_t glo_page = PAGE_GLO;
static const char *const PAGE_NAME[PAGE_N] = {"OSC", "MOD", "ENV", "GLO", "FX GRAIN", "FX SHAPE", "FX MIX", "DEBUG",
                                              "SEQ", "SHAPE", "DETAIL", "OUTPUT", "SCREEN", "MIXER"};
static const char *page_name(uint32_t page);

/* the controls by their names in plat.h, in its order */
static const char *const BTN_NAME[NB] = {"FX", "SEL", "ENV", "LFO", "EDIT", "GLO", "HOME", "SAVE", "ARP", "SEQ",
                                         "PLAY", "REC", "OCT-", "OCT+"};

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
#define SEED_HOLD_MS 400u                    /* (0.26.2) SEL held this long on an FX page: SEED (a tap: the mixer) */
static const char *const QUALITY_NAME[GRAV_FX_QUALITIES] = {"DIGITAL", "VINTAGE", "CLEAN TAPE", "DIRTY TAPE"};

/* Plaits' three knobs (OSC), each track's: 0..100% */
static volatile int32_t osc_params[SYNTH_TRACKS][3];
static volatile int32_t drum_tune[SYNTH_D_TRACKS];          /* the drums' TUNE (50: the note; a half semitone each) */
static volatile int32_t drum_shape[SYNTH_D_TRACKS][3];      /* and SHAPE's knobs (dsp/grav_drums.h), 0..100 */
static volatile uint8_t track_sel;       /* the track selected (SELECT): the keys, MIDI in and the pages play it */
static preset_bank_t user_presets;
static int8_t preset_id[SYNTH_TRACKS];
static uint8_t preset_dirty, preset_write_req;
static uint8_t preset_import_failed;
_Static_assert(sizeof(settings_t) <= PLAT_STORE_MAX, "extended settings fit their existing store object");
static const char *const TRACK_NAME[SYNTH_TRACKS] = {"S1", "S2", "S3", "S4", "D1", "D2", "D3", "D4"};

static uint8_t *track_engine(uint32_t t)   /* a track's engine, where the settings keep it */
{
    return t < SYNTH_S_TRACKS ? &set.trk_engine[t] : &set.drum_engine[(t - SYNTH_S_TRACKS) % SYNTH_D_TRACKS];
}

static void preset_capture(uint32_t t, preset_t *p)
{
    memset(p, 0, sizeof *p);
    p->engine = *track_engine(t);
    p->drum = SYNTH_IS_DRUM(t);
    p->tune = 50;
    p->controls = set.controls[t];
    for (uint32_t i = 0; i < 3; i++) p->osc[i] = (uint8_t)osc_params[t][i];
    if (p->drum) {
        const uint32_t d = t - SYNTH_S_TRACKS;
        p->tune = (uint8_t)drum_tune[d];
        p->osc[2] = 0;
        for (uint32_t i = 0; i < 3; i++) p->shape[i] = (uint8_t)drum_shape[d][i];
    }
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
    {"VOICES", "VELOCITY", "METRONOME"},
    {"DENSITY", "TIME", "SIZE"},
    {"SHAPE", "PITCH", "FEEDBACK"},
    {"DRY/WET", "REVERB", "QUALITY"},
    {"", "", ""},
    {"TEMPO", "SWING", "LENGTH"},
    {"PUNCH", "SWEEP", "DRIVE"},
    [PAGE_SCREEN] = {"THEME", "", ""},
    [PAGE_MIXER] = {"VOL", "SEND", "RETURN"}
};

static int32_t clamp_percent(int32_t value)
{
    if (value < 0) return 0;
    if (value > 100) return 100;
    return value;
}

static volatile uint32_t midi_last, midi_last_port, midi_count;   /* the last message in (shown), and how many */
static volatile uint32_t midi_program;       /* a program change in, + 1 (0: none): the pattern (patterns()) */
/* (0.25) MIDI in's notes to record live: the audio interrupt's queue, the main loop's to empty (rec_midi) */
#define REC_MIDI_N 32u
static volatile uint8_t rec_midi_on;     /* recording live: queue them */
static volatile uint16_t rec_midi_q[REC_MIDI_N];   /* note | velocity << 8 (0: off) */
static volatile uint32_t rec_midi_head, rec_midi_tail;

static void rec_midi_put(uint32_t note, uint32_t vel)   /* audio interrupt */
{
    if (!rec_midi_on || rec_midi_head - rec_midi_tail >= REC_MIDI_N)
        return;
    rec_midi_q[rec_midi_head % REC_MIDI_N] = (uint16_t)(note | vel << 8);
    rec_midi_head++;
}

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
        if ((st & 0xF0u) == 0x90u && d2) {      /* (any channel: the track selected; recording live: recorded) */
            synth_audio_mute = 0u;
            synth_track_note_on(&synth, track_sel, (int32_t)d1, d2);
            rec_midi_put(d1, d2);
        }
        else if ((st & 0xF0u) == 0x80u || (st & 0xF0u) == 0x90u) {
            synth_track_note_off(&synth, track_sel, (int32_t)d1);
            rec_midi_put(d1, 0);
        }
        else if (st == 0xF8u)                /* clock, start, continue, stop: the sequencer follows them */
            seq_midi_clock(&seq);
        else if (st == 0xFAu || st == 0xFBu)
            seq_midi_start(&seq, st == 0xFBu);
        else if (st == 0xFCu)
            seq.want = SEQ_WANT_STOP;
        else if ((st & 0xF0u) == 0xC0u)      /* program change, any channel: 0..15 a pattern */
            midi_program = d1 + 1u;
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

/* (0.24) the metronome's click, while recording live (GLO's METRONOME): a sine of 1.6 kHz (the bar's first: 2.4 kHz,
 * louder) dying away over ~20 ms, 55 ms in all; made by a two-pole resonator, added after the effect and MASTER */
#define CLICK_SAMPLES 2400u
static struct {
    float y1, y2, c, env;                /* the resonator's last two, its 2 cos(w); the level */
    uint32_t left;                       /* samples still to come */
} click;
static volatile uint32_t clicks;         /* clicks begun (the tests) */

static void click_start(int accent)
{
    /* sin(w) and 2 cos(w), w = 2 pi f / 44100 */
    click.y1 = 0.0f;
    click.y2 = accent ? -0.33532f : -0.22599f;   /* y[-1] = 0, y[-2] = -sin(w): a sine from 0 */
    click.c = accent ? 1.88421f : 1.94826f;
    click.env = accent ? 0.30f : 0.18f;
    click.left = CLICK_SAMPLES;
    clicks++;
}

static void click_mix(int32_t *lr, uint32_t from, uint32_t to, uint32_t master_q12)
{
    const float gain = (float)master_q12 / 4096.0f * 8388607.0f;
    for (uint32_t i = from; i < to && click.left; i++, click.left--) {
        const float y = click.c * click.y1 - click.y2;
        click.y2 = click.y1;
        click.y1 = y;
        click.env *= 0.99887f;           /* (-1/e in 20 ms) */
        const int32_t v = (int32_t)(y * click.env * gain);
        for (uint32_t ch = 0; ch < 2u; ch++) {
            int32_t o = lr[2u * i + ch] + v;
            lr[2u * i + ch] = o > 8388607 ? 8388607 : o < -8388607 ? -8388607 : o;
        }
    }
}

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
    {                                        /* the metronome: a click sounding goes on; a beat begins another */
        uint32_t from = 0;
        for (uint32_t i = 0; i < ne; i++)
            if (ev[i].type == SEQ_EV_BEAT) {
                click_mix(lr, from, ev[i].at < n ? ev[i].at : n, master_q12);
                from = ev[i].at < n ? ev[i].at : n;
                click_start(ev[i].note);
            }
        click_mix(lr, from, n, master_q12);
    }

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
        sound[t].controls = &set.controls[t];
        sound[t].pitch = drum ? (float)(drum_tune[t - SYNTH_S_TRACKS] - 50) * 0.5f : pitch_offset;
        sound[t].timbre = timbre;            /* (a drum: TONE) */
        sound[t].morph = morph;              /* (a drum: DECAY) */
        sound[t].harmonics = (float)osc_params[t][2] / 100.0f;
        sound[t].send = (float)set.send[t] * 0.01f;   /* (0.26) the mixer's SEND */
        sound[t].gain = (float)(set.vol[t] * set.vol[t]) * 0.0001f;   /* (0.27) its fader, squared (100: 1, exactly) */
        for (uint32_t i = 0; i < 3; i++)
            sound[t].shape[i] = drum ? (float)drum_shape[t - SYNTH_S_TRACKS][i] * 0.01f : 0.0f;
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
static void drum_knobs_from(settings_t *s, uint32_t d)   /* a drum's TUNE and SHAPE from Plaits' HARMONICS */
{
    float shape[3];
    grav_drum_shape(s->drum_engine[d], (float)s->drum_osc[d][2] * 0.01f, shape);
    s->drum_tune[d] = 50;
    for (uint32_t i = 0; i < 3; i++)
        s->drum_shape[d][i] = (uint8_t)(shape[i] * 100.0f + 0.5f);
}

static void drum_defaults(settings_t *s)   /* the drum tracks' kit */
{
    for (uint32_t d = 0; d < SYNTH_D_TRACKS; d++) {
        s->drum_engine[d] = DRUM_ENGINE[d];
        for (uint32_t i = 0; i < 3; i++)
            s->drum_osc[d][i] = DRUM_OSC[d][i];
        drum_knobs_from(s, d);
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
    for (uint32_t t = 0; t < SYNTH_TRACKS; t++) {   /* (0.26) the mixer: every track through the effect at its full
                                              * level, none muted */
        set.send[t] = 100;
        set.vol[t] = 100;
    }
    set.mute = 0;
    set.octave = 0;
    set.engine_id = 0;
    set.rsv_ble = 0;
    set.theme = 0;
    set.osc[0] = 65; set.osc[1] = 50; set.osc[2] = 75;
    set.mod[0] = 50; set.mod[1] = 0; set.mod[2] = 0;
    set.env[0] = 10; set.env[1] = 50; set.env[2] = 30;
    set.voices = VOICES_DEFAULT;
    set.velocity_off = 0;
    set.reserved[0] = 0;
    fx_defaults(&set);
    track_defaults(&set);
    drum_defaults(&set);
    set.bpm = 120;
    set.pattern = 0;
    set.metro = METRO_REC;
    for (uint32_t t = 0; t < SYNTH_TRACKS; t++) grav_controls_init(&set.controls[t]);
}

/* the knobs' parameters as they are now, into the settings record (SAVE writes the record) */
static void settings_capture(void)
{
    for (uint32_t t = 0; t < SYNTH_S_TRACKS; t++)
        for (uint32_t i = 0; i < 3; i++)
            set.trk_osc[t][i] = (uint8_t)osc_params[t][i];
    for (uint32_t d = 0; d < SYNTH_D_TRACKS; d++) {
        for (uint32_t i = 0; i < 3; i++) {
            set.drum_osc[d][i] = (uint8_t)osc_params[SYNTH_S_TRACKS + d][i];
            set.drum_shape[d][i] = (uint8_t)drum_shape[d][i];
        }
        set.drum_tune[d] = (uint8_t)drum_tune[d];
    }
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
    set.bpm = seq.bpm;
    set.pattern = seq.pattern;
}

/* the settings record's sizes, as each version saved it (an older one is brought up to date as it loads) */
static int set_size_known(int n)
{
    return n == SET_SIZE_012 || n == SET_SIZE_013 || n == SET_SIZE_016 || n == SET_SIZE_017 || n == SET_SIZE_018 ||
           n == SET_SIZE_021 || n == SET_SIZE_025 || n == SET_SIZE_026 || n == (int)sizeof(settings_t);
}

/* a record an older version saved (loaded: its size): what it lacks at its defaults */
static void settings_upgrade(settings_t *s, int loaded)
{
    if (loaded <= SET_SIZE_021)              /* (before 0.22: no 6-op FM controls) */
        for (uint32_t t = 0; t < SYNTH_TRACKS; t++) grav_controls_init(&s->controls[t]);
    if (loaded == SET_SIZE_012)              /* (before 0.13, before the effect: its knobs at rest) */
        fx_defaults(s);
    if (loaded <= SET_SIZE_013)              /* (before 0.16, one sound: S1's, the other tracks their own) */
        track_defaults(s);
    if (loaded <= SET_SIZE_016)              /* (before 0.17: the drums' kit as it comes) */
        drum_defaults(s);
    if (loaded <= SET_SIZE_017) {            /* (before 0.18: the tempo the pattern's, on pattern 1) */
        s->bpm = 120;
        s->pattern = 0;
        s->metro = METRO_REC;
    }
    if (loaded <= SET_SIZE_018)              /* (before 0.19: TUNE as the note, SHAPE from HARMONICS) */
        for (uint32_t d = 0; d < SYNTH_D_TRACKS; d++)
            drum_knobs_from(s, d);
    if (loaded <= SET_SIZE_025) {            /* (before 0.26: no mixer; every track sent whole, none muted) */
        for (uint32_t t = 0; t < SYNTH_TRACKS; t++)
            s->send[t] = 100;
        s->mute = 0;
    }
    if (loaded <= SET_SIZE_026)              /* (before 0.27: no faders; each at 100) */
        for (uint32_t t = 0; t < SYNTH_TRACKS; t++)
            s->vol[t] = 100;
    if (s->theme > NPALETTES)                /* (a byte reserved before 0.25.1) */
        s->theme = 0;
    if (s->metro > METRO_COUNT)              /* (a byte reserved before 0.24) */
        s->metro = METRO_REC;
}

static int percent_ok(const uint8_t *v, uint32_t n)   /* every one 0..100 */
{
    for (uint32_t i = 0; i < n; i++)
        if (v[i] > 100u)
            return 0;
    return 1;
}

/* every value of a record (brought up to date) in its range: else none of it is taken */
static int settings_valid(const settings_t *s, int loaded)
{
    uint32_t voices = 0;
    for (uint32_t t = 0; t < SYNTH_TRACKS; t++) {
        const uint32_t engine = t < SYNTH_S_TRACKS ? s->trk_engine[t] : s->drum_engine[t - SYNTH_S_TRACKS];
        if ((loaded >= SET_SIZE_025 && !grav_controls_valid(engine, &s->controls[t])) || s->send[t] > 100u ||
            s->vol[t] > 100u)
            return 0;
    }
    if (s->bpm < SEQ_BPM_MIN || s->bpm > SEQ_BPM_MAX || s->pattern >= BANK_PATTERNS)
        return 0;
    for (uint32_t d = 0; d < SYNTH_D_TRACKS; d++)
        if (s->drum_tune[d] > 100u || !percent_ok(s->drum_shape[d], 3) || !grav_engine_is_drum(s->drum_engine[d]) ||
            !percent_ok(s->drum_osc[d], 3))
            return 0;
    for (uint32_t t = 0; t < SYNTH_S_TRACKS; t++) {
        voices += s->trk_voices[t];
        if (s->trk_engine[t] >= GRAV_ENGINE_COUNT || s->trk_voices[t] > 4u || !percent_ok(s->trk_osc[t], 3))
            return 0;
    }
    return voices <= SYNTH_POLY_VOICES && percent_ok(s->osc, 3) && percent_ok(s->mod, 3) && percent_ok(s->env, 3) &&
           percent_ok(s->fx, FX_N) && s->voices <= SYNTH_POLY_VOICES && s->velocity_off <= 1u &&
           s->fx_quality < GRAV_FX_QUALITIES;
}

/* the settings saved (loaded: the record's size, 0 if none), into set if they are sound; 0.11's and 0.12's first
 * records: their octave and engine. 1 if the tempo is still the pattern's (before 0.18) */
static int settings_load(int *loaded)
{
    settings_t s = {0};
    *loaded = plat_store_load(OBJ_SETTINGS, &s, sizeof s);
    settings_defaults();
    set_dirty = 0;
    if (s.octave < -2 || s.octave > 2)
        return 1;
    if (set_size_known(*loaded) && s.magic == SET_MAGIC && s.engine_id < GRAV_ENGINE_COUNT) {
        settings_upgrade(&s, *loaded);
        if (!settings_valid(&s, *loaded))
            return 1;
        set = s;
        if (set.voices == 0)                 /* (saved before 0.12, the byte reserved: the default) */
            set.voices = VOICES_DEFAULT;
        return *loaded <= SET_SIZE_017;
    }
    if (*loaded == 8 && (s.magic == PREV_SET_MAGIC || s.magic == OLD_SET_MAGIC) && s.engine_id <= 2u) {
        set.octave = s.octave;
        set.engine_id = set.trk_engine[0] = s.magic == PREV_SET_MAGIC ? s.engine_id : s.engine_id == 2u ? 1u : 0u;
        set_dirty = 1;
    }
    return 1;
}

void app_init(void)
{
    synth_init(&synth);
    synth_event_queue_init(&synth_queue);
    grav_engine_init(&grav_engine);
    int loaded;
    const int tempo_old = settings_load(&loaded);
    for (uint32_t t = 0; t < SYNTH_TRACKS; t++)
        for (uint32_t i = 0; i < 3; i++)
            osc_params[t][i] = t < SYNTH_S_TRACKS ? set.trk_osc[t][i] : set.drum_osc[t - SYNTH_S_TRACKS][i];
    for (uint32_t d = 0; d < SYNTH_D_TRACKS; d++) {
        drum_tune[d] = set.drum_tune[d];
        for (uint32_t i = 0; i < 3; i++)
            drum_shape[d][i] = set.drum_shape[d][i];
    }

    for (int i = 0; i < 3; ++i) {
        mod_params[i] = set.mod[i];
        env_params[i] = set.env[i];
    }
    for (int i = 0; i < FX_N; ++i)
        fx_params[i] = set.fx[i];
    fx_quality = set.fx_quality;
    synth_set_tracks(&synth, set.trk_voices);   /* (before the audio starts: no queue needed) */

    seq_init(&seq);
    seq.bpm = set.bpm;
    seq.mute = set.mute;                     /* (0.26) the mixer's mutes */
    bank_init(&bank);                        /* the patterns; their stale copies erased now, before the sound */
    while (bank_tidy(&bank))
        ;
    if (bank_count(&bank, BANK_LIVE) == 0) { /* none yet: 0.17's pattern and before (OBJ_SEQ, OBJ_DRUMS) is pattern 1 */
        union { seq_record_t r; seq_pattern_v1_t v1; seq_drum_record_t d; } rec;
        const int n = plat_store_load(OBJ_SEQ, &rec, sizeof rec);
        int found = 0;
        if (n == (int)sizeof rec.r)
            found = seq_from_record(&rec.r, &seq.pat);
        else if (n == (int)sizeof rec.v1)
            found = seq_from_v1(&rec.v1, &seq.pat);
        if (plat_store_load(OBJ_DRUMS, &rec, sizeof rec) == (int)sizeof rec.d)
            found |= seq_from_drum_record(&rec.d, &seq.pat);
        if (found) {
            if (tempo_old)
                seq.bpm = set.bpm = seq.pat.bpm;
            if (bank_begin(&bank, 0) == 0)
                while (bank_step(&bank, &seq.pat, BANK_SLOT / BANK_PAGE) == 0)
                    ;
            set.pattern = 0;
            if (plat_store_save(OBJ_SETTINGS, &set, sizeof set) == 0)   /* (the tempo with it, at once) */
                loaded = (int)sizeof set;
        }
    }
    used_seq = ~bank.seq;                    /* (patterns_used: read the bank afresh) */
    seq.pattern = set.pattern;               /* the pattern it was on */
    seq_load(&seq, bank_pattern(&bank, set.pattern));
    set_stored = set;
    if (loaded != (int)sizeof set)           /* (an older record, or none: SAVE writes this one) */
        memset(&set_stored, 0, sizeof set_stored);
    preset_store_init(&user_presets);
    {   /* 0.21 bank remains intact; interrupted imports resume at the next boot, before audio starts. */
        uint8_t legacy[1296] __attribute__((aligned(4)));
        const int size = plat_store_load(OBJ_PRESETS, legacy, sizeof legacy);
        preset_import_failed = size > 0 && preset_store_import_021(&user_presets, legacy, (uint32_t)size) < 0;
    }
    preset_dirty = user_presets.n_pending != 0;
    preset_write_req = 0;
    for (uint32_t t = 0; t < SYNTH_TRACKS; t++) {
        preset_t p;
        preset_capture(t, &p);
        preset_id[t] = (int8_t)preset_match(&user_presets, &p);
    }
#if FM1B_CPU2
    plat_cpu2_set(1);                     /* boot only, before audio/timers start */
#endif
}

/* --------------------------------------------------------------------- UI --- */
#define C_RED RGB(240, 55, 45)               /* REC, MUTE, OFF, a fault: the one red, whatever the theme */
#define LIVE_MIDI 8u                         /* MIDI in's notes held at once while recording live */
#define LIVE_SLOTS (NKEYS + LIVE_MIDI)
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
    uint8_t step_mode;                   /* SEQ: the white keys are the steps of view_page (on SEQ's page alone) */
    uint8_t seq_from, seq_from_home;     /* the page SEQ was pressed on (and HOME's): SEQ again goes back to it */
    uint8_t view_page;                   /* the page of 16 steps shown and edited */
    int8_t step_held;                    /* the step a white key holds (K1..K3 edit it); -1: none */
    uint8_t step_was_on, step_edited;    /* (a step on, let go untouched, goes off) */
    uint8_t seq_down, seq_used;          /* SEQ held; and a white key pressed meanwhile (the length: no mode change) */
    uint8_t erasing;                     /* (0.25) SEQ held while recording live: erasing */
    uint32_t seq_at;                     /* SEQ pressed, when */
    uint8_t shown_count;                 /* the count-in's beat the screen shows (0: none) */
    uint8_t rec, rec_pos;                /* REC (stopped): the next chord goes to rec_pos */
    uint32_t key_step;                   /* the keys pressed as steps (let go as steps) */
    uint8_t chord_open, chord_n, chord_vel, chord[SEQ_NOTES];   /* the notes held together, as they came */
    uint8_t rec_live;                    /* (0.24) REC while playing: the keys' notes written as they play */
    uint8_t live_track, live_step, live_ahead, live_pattern;   /* the last note recorded live (a chord: the next */
    uint32_t rec_at;                     /* within LIVE_CHORD_MS to the same step) and when */
    uint8_t live_held[LIVE_SLOTS];       /* the notes recorded live still held, a slot each (a key's: its number;
                                          * MIDI in's: NKEYS on): */
    uint8_t live_slot_step[LIVE_SLOTS], live_slot_track[LIVE_SLOTS], live_slot_note[LIVE_SLOTS];   /* its step, */
    uint32_t live_slot_at[LIVE_SLOTS];   /* track, note, and when (its length when let go) */
    uint8_t last_n, last_vel, last_chord[SEQ_NOTES];             /* the last chord: a new step's */
    uint32_t beat_count, beat_at;        /* PLAY's green: the last beat, and the bar's first */
    uint8_t beat_bar;
    int32_t shown_step;                  /* the step the screen shows playing, and whether */
    uint8_t shown_playing, redraw_grid;
    uint32_t shown_bpm;                  /* the tempo the header shows (seq_bpm), and whether a clock in's */
    uint8_t shown_ext;
    char bpm_text[8];
    uint8_t key_track[NKEYS];            /* the track each held key plays (its note-off goes there) */
    uint32_t tracks_until;               /* SELECT turned: the tracks' list shown until then */
    uint32_t shown_hits[SEQ_TRACKS], hit_at[SEQ_TRACKS];   /* each track's last note begun: its light flashes */
    int32_t lane_step;                   /* SEQ: the selected track's step playing, and when it began (its flash) */
    uint32_t step_at, grid_at;           /* (and the steps' strip's last frame, the whole page's or its own) */
    uint32_t grid_frames, grid_alone_at; /* the strip's frames drawn alone, the last one's time (the tests) */
    uint32_t vol_at;                     /* the volume last moved (SEQ's band shows it a moment) */
    /* the patterns (0.18) */
    uint8_t home_down, home_used, home_shown;   /* HOME held; a pattern picked meanwhile (no page change); the grid shown */
    uint32_t home_at;                    /* when HOME went down */
    uint32_t key_pat;                    /* the keys pressed as patterns (let go as such) */
    uint32_t key_mix;                    /* (0.26) the keys pressed on the mixer (mutes, tracks: let go as such) */
    uint8_t mixer_from;                  /* the page SEL opened the mixer on (SEL again: back to it) */
    uint8_t seed_down;                   /* SEL held as SEED (on an FX page) */
    uint8_t sel_down, sel_used;          /* SEL held; and as SEED meanwhile (let go: no mixer) */
    uint32_t sel_at;                     /* SEL pressed, when */
    uint16_t meter[SYNTH_TRACKS];        /* the mixer's meters, 0..256, falling back */
    float level[SYNTH_TRACKS];           /* (0.27) each track's level, followed: HOME's terricons (0.28; rings before) */
    uint8_t level_heard;                 /* (0.27.1) the tracks that sounded since a test last asked (a bit each) */
    uint32_t level_at;                   /* (its last update) */
    uint8_t preset_edited;               /* (0.27) the tracks whose sound was changed after its preset (a * by it) */
    uint8_t solo;                        /* (0.26.1) the tracks soloed, a bit each (black keys 1..8 on the mixer; not
                                          * saved): some, and only they sound, whatever their mutes */
    uint32_t patterns_until;             /* K4 turned: the patterns' grid shown until then */
    uint8_t save_req;                    /* SAVE: the pattern too, then SAVED */
    uint8_t full_said;                   /* (no free slot while playing: said once) */
    uint8_t shown_pattern;               /* the pattern announced, + 1 (0: none yet) */
    uint32_t save_gen;                   /* the edits the save under way holds */
    uint32_t quiet_at, tidy_at, blink_at;   /* the last sound (stale copies erased 3 s after), an erase, a blink */
    /* the icons (0.20) */
    int32_t live_x, live_y, live_w, live_h; /* the live area on the page (0 wide: none): its icon redrawn alone */
    uint32_t live_at, live_frames;       /* its last frame; frames drawn (the tests) */
    uint32_t splash_until;               /* ALGO turned: the engine's splash shown until then */
    uint8_t preset_down, preset_used, preset_shown, preset_page;
    uint32_t preset_at, key_preset;      /* SAVE modifier, its grid, and keys that must never play/record notes */
    uint8_t sound_page[SYNTH_TRACKS], sound_nav;
    int32_t dial_q8[3];
    uint32_t dial_id, dial_at, dial_frames;
    uint8_t dial_hot, dial_dirty, dial_next;
    uint32_t icon_strikes[SYNTH_TRACKS], icon_hit_at[SYNTH_TRACKS];   /* each track's notes begun: its icon's strike */
} ui;

static void text_cat(char *d, const char *s);
static uint32_t sound_pages(void);

static const char *preset_label(uint32_t t)  /* a track's sound's name (0.27: whole, and * once it is changed) */
{
    static char b[PRESET_TITLE + 2];
    const char *n = preset_title(&user_presets, *track_engine(t), SYNTH_IS_DRUM(t), preset_id[t]);
    if (!n)
        return "CUSTOM";
    b[0] = 0;
    text_cat(b, n);
    if (ui.preset_edited >> t & 1u)
        text_cat(b, "*");
    return b;
}

/* a sound on track t: its engine (the render builds it anew in the track's voices, at the next block), its knobs and
 * controls; a drum track's TUNE and SHAPE too (shape: none on a synth track) */
static void track_sound_set(uint32_t t, uint32_t engine, const grav_controls_t *controls, const uint8_t *osc,
                            uint8_t tune, const uint8_t *shape)
{
    const uint32_t f = plat_lock();
    *track_engine(t) = (uint8_t)engine;
    set.controls[t] = *controls;
    for (uint32_t i = 0; i < 3; i++) osc_params[t][i] = osc[i];
    if (shape && SYNTH_IS_DRUM(t)) {
        const uint32_t d = t - SYNTH_S_TRACKS;
        drum_tune[d] = tune;
        for (uint32_t i = 0; i < 3; i++) drum_shape[d][i] = shape[i];
    }
    plat_unlock(f);
    if (ui.sound_page[t] >= sound_pages())   /* (another engine: fewer pages, perhaps) */
        ui.sound_page[t] = 0;
}

/* (0.27) a preset on a track: its engine (if another), its knobs; the sequencer, the mixer and the other tracks as
 * they were; nothing written to the flash */
static void preset_apply(uint32_t t, uint32_t engine, int id)
{
    preset_t p;
    if (!preset_get(&user_presets, engine, SYNTH_IS_DRUM(t), id, &p))
        return;
    track_sound_set(t, engine, &p.controls, p.osc, p.tune, p.drum ? p.shape : 0);
    preset_id[t] = (int8_t)id;
    ui.preset_edited &= (uint8_t)~(1u << t);
    set_dirty = 1;
    ui.splash_until = ui.tracks_until = ui.patterns_until = 0;
    app_say("PRESET", preset_title(&user_presets, engine, SYNTH_IS_DRUM(t), id));
    ui.redraw = 1;
}

/* (0.27) the presets' browser (PRESET turned): every sound the selected track can play, engine after engine in
 * ALGO's order, each one's four factory sounds then the user's saved on it; or (OCT+) its current engine's alone.
 * Each one chosen plays at once, the other tracks going on; OCT- puts the sound back as it was when it opened. It
 * closes 3 s after the last turn, or as a page opens or another track is chosen, keeping the sound chosen */
#define PB_MAX (GRAV_ENGINE_COUNT * PRESET_FACTORY + PRESET_USERS)
#define PB_ROWS 5u
static struct {
    uint8_t open, all, track;
    uint16_t list[PB_MAX];                   /* engine << 8 | id */
    uint16_t n;
    int16_t sel;
    uint32_t until;
    uint8_t engine, edited, osc[3], tune, shape[3];   /* the sound as it opened (OCT-) */
    int8_t id;
    grav_controls_t controls;
} pb;

static uint32_t pb_engine(int32_t i) { return pb.list[i] >> 8; }
static int pb_id(int32_t i) { return pb.list[i] & 0xFF; }
static int pb_shown(int32_t i)               /* in the list shown: all, or the current engine's */
{
    return pb.all || pb_engine(i) == pb_engine(pb.sel);
}

static void pb_open(void)
{
    const uint32_t t = track_sel, drum = SYNTH_IS_DRUM(t), engines = drum ? 6u : GRAV_ENGINE_COUNT;
    preset_t p;
    pb.n = 0;
    for (uint32_t i = 0; i < engines; i++) {
        const uint32_t e = grav_engine_at((int)drum, i);
        for (int id = 0; id < (int)(PRESET_FACTORY + PRESET_USERS) && pb.n < PB_MAX; id++)
            if (preset_get(&user_presets, e, (int)drum, id, &p))
                pb.list[pb.n++] = (uint16_t)(e << 8 | (uint32_t)id);
    }
    int32_t playing = -1, first = -1;        /* the sound playing, else its engine's first */
    for (int32_t i = 0; i < pb.n; i++)
        if (pb_engine(i) == *track_engine(t)) {
            if (pb_id(i) == preset_id[t])
                playing = i;
            if (pb_id(i) == 0 && first < 0)
                first = i;
        }
    pb.sel = (int16_t)(playing >= 0 ? playing : first >= 0 ? first : 0);
    pb.track = (uint8_t)t;
    pb.engine = *track_engine(t);
    pb.id = (int8_t)preset_id[t];
    pb.edited = ui.preset_edited >> t & 1u;
    pb.controls = set.controls[t];
    for (uint32_t i = 0; i < 3; i++) pb.osc[i] = (uint8_t)osc_params[t][i];
    if (drum) {
        pb.tune = drum_tune[t - SYNTH_S_TRACKS];
        for (uint32_t i = 0; i < 3; i++) pb.shape[i] = drum_shape[t - SYNTH_S_TRACKS][i];
    }
    pb.all = 1;
    pb.open = pb.n > 0;
}

static void pb_close(void)
{
    if (!pb.open)
        return;
    pb.open = 0;
    ui.redraw = 1;
}

static void pb_move(int32_t e)               /* the next sound shown (or before), round, at once */
{
    int32_t i = pb.sel;
    for (int32_t k = 0; k < (e < 0 ? -e : e); k++) {
        int32_t j = i;
        do
            j = (j + (e < 0 ? pb.n - 1 : 1)) % pb.n;
        while (j != i && !pb_shown(j));
        i = j;
    }
    pb.until = plat_ms() + 3000u;
    if (i == pb.sel && preset_id[pb.track] == pb_id(i) && *track_engine(pb.track) == pb_engine(i))
        return;
    pb.sel = (int16_t)i;
    preset_apply(pb.track, pb_engine(i), pb_id(i));
}

static void pb_cancel(void)                  /* OCT-: the sound as it was when the browser opened */
{
    const uint32_t t = pb.track;
    track_sound_set(t, pb.engine, &pb.controls, pb.osc, pb.tune, pb.shape);
    preset_id[t] = pb.id;
    ui.preset_edited = (uint8_t)((ui.preset_edited & ~(1u << t)) | (uint32_t)pb.edited << t);
    pb_close();
    app_say("PRESET", "CANCEL");
}

static void preset_turn(int32_t e)           /* PRESET: the browser, and the next sound in it */
{
    if (!pb.open || pb.track != track_sel) {
        pb_open();
        if (pb.open && pb_id(pb.sel) != preset_id[track_sel]) {   /* (a sound of no preset: the first step lands
                                                                  * on its engine's first, back on its last) */
            if (e < 0) {
                while (pb.sel + 1 < pb.n && pb_engine(pb.sel + 1) == pb_engine(pb.sel))
                    pb.sel++;
                e++;
            } else
                e--;
            preset_apply(pb.track, pb_engine(pb.sel), pb_id(pb.sel));
            pb.until = plat_ms() + 3000u;
        }
    }
    if (pb.open && e)
        pb_move(e);
}

static void preset_save_slot(uint32_t slot)
{
    preset_t p;
    preset_capture(track_sel, &p);
    memcpy(p.name, "USER 00", 8);
    p.name[5] = (char)('0' + (slot + 1u) / 10u);
    p.name[6] = (char)('0' + (slot + 1u) % 10u);
    if (!preset_put(&user_presets, slot, &p)) {
        app_say("PRESET QUEUE FULL", "STOP TO SAVE");
        return;
    }
    /* A slot may have been selected by several tracks. Overwriting it must not rename an old sound. */
    for (uint32_t t = 0; t < SYNTH_TRACKS; t++)
        if (preset_id[t] == (int)(PRESET_FACTORY + slot)) preset_id[t] = PRESET_CUSTOM;
    preset_id[track_sel] = (int8_t)(PRESET_FACTORY + slot);
    ui.preset_edited &= (uint8_t)~(1u << track_sel);
    preset_dirty = preset_write_req = 1;
    ui.redraw = 1;
    app_say("PRESET QUEUED", seq.playing ? "STOP TO SAVE" : p.name);
}

static void presets_poll(void)
{
    if (ui.preset_down && !ui.preset_shown && plat_ms() - ui.preset_at >= 300u) {
        ui.preset_shown = 1;
        ui.redraw = 1;
    }
    if (!preset_write_req || seq.playing || seq.want == SEQ_WANT_PLAY || ui.keys ||
        !synth_quiet(&synth) || grav_debug.active || bank_busy(&bank)) return;
    /* One sound per idle frame: recheck transport/voices before the next queued record. */
    if (preset_store_flush(&user_presets) < 0) {
        preset_write_req = 0;               /* retry only on the next explicit SAVE */
        app_say("PRESET FAILED", "TAP SAVE TO RETRY");
    } else if (!user_presets.n_pending) {
        preset_write_req = 0;
        preset_dirty = 0;
        app_say("PRESET SAVED", "");
    } else return;
    ui.redraw = 1;
}

#include "ui/icons.c"

static float icon_time(void) { return (float)(plat_ms() % 600000u) * 0.001f; }   /* (10 minutes round: precise) */

/* a track's icon: its engine's knobs (a drum's TONE, DECAY, SHAPE's first), its notes' strike (a held one keeps
 * some) */
static void icon_fill(uint32_t t, icon_t *ic)
{
    const uint32_t now = plat_ms();
    const int drum = SYNTH_IS_DRUM(t);
    uint32_t strikes = 0;
    int held = 0;
    for (uint32_t v = 0; v < SYNTH_VOICES_MAX; v++)
        if (synth.voice_track[v] == t) {
            strikes += synth.voice[v].strikes;
            held |= synth.voice[v].gate;
        }
    if (strikes != ui.icon_strikes[t]) {
        ui.icon_strikes[t] = strikes;
        ui.icon_hit_at[t] = now;
    }
    for (uint32_t i = 0; i < 3u; i++)
        ic->k[i] = (float)(drum && i == 2u ? drum_shape[t - SYNTH_S_TRACKS][0] : osc_params[t][i]) * 0.01f;
    ic->hit = fm_expf(-(float)(now - ui.icon_hit_at[t]) / (drum ? 60.0f + 700.0f * ic->k[1] * ic->k[1] : 500.0f));
    if (held && !drum && ic->hit < 0.6f)
        ic->hit = 0.6f;
    ic->t = icon_time();
}

static void icon_box(uint32_t t, int32_t x, int32_t y, int32_t w, int32_t h, int small)   /* a track's icon there */
{
    icon_t ic;
    ic.x = x;
    ic.y = y;
    ic.w = w;
    ic.h = h;
    ic.small = small;
    icon_fill(t, &ic);
    icon_draw(*track_engine(t), &ic);
}

static void live_area(int32_t x, int32_t y, int32_t w, int32_t h)   /* the selected track's icon there, alive */
{
    icon_box(track_sel, x, y, w, h, 0);
    ui.live_x = x;
    ui.live_y = y;
    ui.live_w = w;
    ui.live_h = h;
}

/* the live area alone, between the screen's redraws: ~30 frames a second (10 while the sound is busy), its icon
 * drawn into a canvas of its size and sent alone (a 98 x 54 icon: 10 KB, under 2 ms out) */
static void draw_live(void)
{
    icon_t ic;
    cv_begin((uint32_t)ui.live_w, (uint32_t)ui.live_h, C_BLACK);
    ic.x = 0;
    ic.y = 0;
    ic.w = ui.live_w;
    ic.h = ui.live_h;
    ic.small = 0;
    icon_fill(track_sel, &ic);
    icon_draw(*track_engine(track_sel), &ic);
    cv_blit((uint32_t)ui.live_x, 36u + (uint32_t)ui.live_y);
}

/* text in lines of w pixels at most, word by word (lines at most): left, or centred on x + w / 2 */
static void draw_wrapped(const char *s, int32_t x, int32_t y, int32_t w, uint32_t lines, int centre, uint16_t c)
{
    char line[48];
    for (uint32_t l = 0; l < lines && *s; l++) {
        uint32_t n = 0, fit = 0;
        while (s[n] && n < sizeof line - 1u) {   /* the most words that fit */
            uint32_t e = n;
            while (s[e] == ' ')
                e++;
            while (s[e] && s[e] != ' ')
                e++;
            if (e >= sizeof line)
                break;
            memcpy(line, s, e);
            line[e] = 0;
            if (fit && text_w(&FONT_XS, line) > w)
                break;
            fit = n = e;
        }
        if (!fit)
            break;
        memcpy(line, s, fit);
        line[fit] = 0;
        cv_text(centre ? x + (w - text_w(&FONT_XS, line)) / 2 : x, y + (int32_t)l * 13, &FONT_XS, line, c);
        s += fit;
        while (*s == ' ')
            s++;
    }
}

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

/* the last MIDI message in: its three bytes in hex ("90 3C 64"), and where from */
static void midi_in_text(char *h)
{
    static const char HEX[] = "0123456789ABCDEF";
    for (uint32_t b = 0; b < 3u; b++) {
        const uint32_t v = (midi_last >> (8u + 8u * b)) & 0xFFu;
        h[3 * b] = HEX[v >> 4];
        h[3 * b + 1] = HEX[v & 15u];
        h[3 * b + 2] = b < 2u ? ' ' : 0;
    }
}

static const char *midi_in_port(void)
{
    static const char *const PORT[NPORTS] = {"USB", "BLE", "TRS"};
    return midi_last_port < NPORTS ? PORT[midi_last_port] : "";
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
    char t[160], o[4], o2[8];
    if (ann_on() && ann_where()) {
        octave_text(o);
        t[0] = 0;
        text_cat(t, APP_NAME ". OCTAVE ");
        text_cat(t, o);
        text_cat(t, ". WAVE ");
        text_cat(t, grav_engine_name(*track_engine(track_sel)));
        text_cat(t, ". TRACK ");
        text_cat(t, TRACK_NAME[track_sel]);
        int_text((int32_t)seq_bpm(&seq), o2);
        text_cat(t, ". TEMPO ");
        text_cat(t, o2);
        text_cat(t, ". PAGE ");
        text_cat(t, page_name(active_page));
        text_cat(t, ". PRESET ");
        text_cat(t, preset_label(track_sel));
        if (midi_count) {                    /* the last MIDI message in, as the screen shows it */
            char h[12];
            midi_in_text(h);
            text_cat(t, ". MIDI IN ");
            text_cat(t, midi_in_port());
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

/* the selected track's voices, by e: more take the free ones, then one from a track with more than one (the one with
 * the most; never another's last, 0.27.1: it would fall silent); none to take: as it was, NO VOICE FREE. Fewer are
 * left free. Every voice let go, as the layout changes */
static void voices_turn(int32_t e)
{
    uint8_t c[SYNTH_S_TRACKS];
    const uint32_t t = track_sel;
    uint32_t total = 0;
    char a[16];
    a[0] = 0;
    text_cat(a, TRACK_NAME[t]);
    text_cat(a, " VOICES");
    if (SYNTH_IS_DRUM(t)) {                  /* a drum track: its one voice, always */
        app_say(a, "DRUM");
        return;
    }
    for (uint32_t k = 0; k < SYNTH_S_TRACKS; k++)
        total += c[k] = set.trk_voices[k];
    int32_t want = clamp((int32_t)c[t] + e, 0, (int32_t)SYNTH_POLY_VOICES);
    int none = 0;
    while ((int32_t)c[t] < want) {
        if (total < SYNTH_POLY_VOICES) {
            total++;
        } else {
            uint32_t from = SYNTH_S_TRACKS;
            for (uint32_t k = SYNTH_S_TRACKS; k-- > 0;)
                if (k != t && c[k] > 1u && (from == SYNTH_S_TRACKS || c[k] > c[from]))
                    from = k;
            if (from == SYNTH_S_TRACKS) {
                none = 1;
                break;
            }
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
    app_say(a, none ? "NO VOICE FREE" : voices_text(c[t]));
}

/* GLO's knobs: K1 VOICES (OFF, MONO, 2, 3, 4), K2 VELOCITY (right: on, the keys' own; left: off, fixed), K3
 * METRONOME (OFF, REC, COUNT) */
static const char *const METRO_NAME[3] = {"REC", "OFF", "COUNT"};

/* (0.25.1) GLO's SCREEN, K1 THEME: the palettes (device/gfx.c) in the knob's order, the app's own (TEAL) first */
static const uint8_t THEME_ORDER[] = {6, 1, 2, 7, 0, 9, 5, 8, 3, 4};
#define THEMES (sizeof THEME_ORDER / sizeof THEME_ORDER[0])
_Static_assert(THEMES == NPALETTES, "every palette on the knob");

static uint32_t theme_palette(void)          /* the palette shown: the one chosen, or the app's */
{
    return set.theme && set.theme <= NPALETTES ? set.theme - 1u : APP_PALETTE;
}

static uint32_t theme_at(void)               /* its place on the knob */
{
    for (uint32_t i = 0; i < THEMES; i++)
        if (THEME_ORDER[i] == theme_palette())
            return i;
    return 0;
}

static void theme_turn(int32_t e)
{
    const uint32_t at = (uint32_t)clamp((int32_t)theme_at() + (e > 0 ? 1 : -1), 0, (int32_t)THEMES - 1);
    if (THEME_ORDER[at] != theme_palette()) {
        set.theme = (uint8_t)(THEME_ORDER[at] + 1u);
        set_dirty = 1;
        palette_set(theme_palette());        /* (every colour the screen draws comes from it: the whole screen anew) */
        ui.redraw = 1;
    }
    app_say("THEME", PALETTES[theme_palette()].name);
}

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
    } else {                                 /* METRONOME: OFF, REC (clicks while recording live), COUNT (and a count-in) */
        static const uint8_t ORDER[3] = {METRO_OFF, METRO_REC, METRO_COUNT};
        const int32_t at = set.metro == METRO_OFF ? 0 : set.metro == METRO_REC ? 1 : 2;
        const uint8_t m = ORDER[clamp(at + (e > 0 ? 1 : -1), 0, 2)];
        if (m != set.metro) {
            set.metro = m;
            set_dirty = 1;
        }
        app_say("METRONOME", METRO_NAME[set.metro]);
    }
}

static uint32_t sound_pages(void)
{
    return SYNTH_IS_DRUM(track_sel) ? 3u : 2u + grav_control_pages(*track_engine(track_sel));
}

static void sound_activate(uint32_t page)
{
    ui.sound_page[track_sel] = (uint8_t)(page % sound_pages());
    const uint32_t p = ui.sound_page[track_sel];
    active_page = home_page = p == 0 ? PAGE_OSC : p + 1u == sound_pages() ? PAGE_OUTPUT :
                              SYNTH_IS_DRUM(track_sel) ? PAGE_SHAPE : PAGE_SOUND;
    ui.redraw = 1;
}

/* a page's knob: K1..K3 (index 0..2) on OSC, MOD, ENV, the FX pages and a drum's SHAPE (not GLO's, nor MIX's
 * QUALITY). A drum track's OSC is its DRUM page: TUNE, DECAY (MORPH), TONE (TIMBRE) */
static volatile int32_t *param_of(uint32_t page, uint32_t index)
{
    const uint32_t d = (track_sel - SYNTH_S_TRACKS) % SYNTH_D_TRACKS;
    if (page == PAGE_OSC && SYNTH_IS_DRUM(track_sel))
        return index == 0u ? &drum_tune[d] : &osc_params[track_sel][2u - index];
    if (page == PAGE_OSC)
        return &osc_params[track_sel][index];
    if (page == PAGE_SHAPE)
        return &drum_shape[d][index % 3u];
    if (page == PAGE_MOD)
        return &mod_params[index];
    if (page == PAGE_ENV)
        return &env_params[index];
    return &fx_params[(page - PAGE_FX_GRAIN) * 3u + index];
}

static int32_t param_value(uint32_t page, uint32_t index)
{
    if (page == PAGE_FX_MIX && index == 2) return fx_quality;
    if (page == PAGE_SOUND) {
        grav_control_info_t info;
        if (!grav_control_info(*track_engine(track_sel), ui.sound_page[track_sel] - 1u, index, &info)) return -1;
        if (grav_controls_fm(*track_engine(track_sel)) && !(set.controls[track_sel].flags & GRAV_CUSTOM_FM)) {
            grav_controls_t c = set.controls[track_sel];
            grav_fm_patch(*track_engine(track_sel), (uint32_t)osc_params[track_sel][2], c.value);
            return grav_control_read(&c, &info);
        }
        return grav_control_read(&set.controls[track_sel], &info);
    }
    if (page == PAGE_OUTPUT) {
        const grav_controls_t *c = &set.controls[track_sel];
        if (SYNTH_IS_DRUM(track_sel)) return index == 0 ? c->level : index == 1 ? c->accent : !!(c->flags & GRAV_DRUM_HOLD);
        return index == 0 ? c->tune : index == 1 ? c->level : c->aux;
    }
    if (!IS_SOUND_PAGE(page) && page != PAGE_MOD && page != PAGE_ENV && !IS_FX_PAGE(page)) return -1;
    return *param_of(page, index);
}

/* drawn from the middle: DENSITY (no grains there: clocked left, random right), PITCH and a drum's TUNE (0 there) */
static int param_centred(uint32_t page, uint32_t index)
{
    if (page == PAGE_SOUND) {
        grav_control_info_t i;
        return grav_control_info(*track_engine(track_sel), ui.sound_page[track_sel] - 1u, index, &i) &&
               (i.kind == GC_SIGNED || i.kind == GC_FM_SIGNED);
    }
    return (page == PAGE_FX_GRAIN && index == 0u) || (page == PAGE_FX_SHAPE && index == 1u) ||
           (page == PAGE_OUTPUT && index == 0u && !SYNTH_IS_DRUM(track_sel)) ||
           (page == PAGE_OSC && index == 0u && SYNTH_IS_DRUM(track_sel));
}

/* a knob's name: a drum track's DRUM and SHAPE pages by its drum (dsp/grav_drums.h) */
static const char *param_name(uint32_t page, uint32_t index)
{
    if (page == PAGE_SOUND) {
        grav_control_info_t info;
        return grav_control_info(*track_engine(track_sel), ui.sound_page[track_sel] - 1u, index, &info) ? info.name : "--";
    }
    if (page == PAGE_OUTPUT) {
        static const char *const out[2][3] = {{"TUNE","LEVEL","AUX MIX"},{"LEVEL","ACCENT","HOLD"}};
        return out[SYNTH_IS_DRUM(track_sel)][index];
    }
    static const char *const DRUM[3] = {"TUNE", "DECAY", "TONE"};
    static const char *const SHAPE[3][3] = {{"PUNCH", "SWEEP", "DRIVE"}, {"SNAPPY", "DRIVE", "LEVEL"},
                                            {"NOISE", "DRIVE", "LEVEL"}};
    if (page == PAGE_OSC && SYNTH_IS_DRUM(track_sel))
        return DRUM[index % 3u];
    if (page == PAGE_SHAPE) {
        const int kind = grav_drum_kind(*track_engine(track_sel));
        return SHAPE[kind > 0 ? kind : 0][index % 3u];
    }
    if (page == PAGE_OSC)                    /* (0.21) by what each does on the engine: WAVESHAPE's FOLD, ASYMMETRY,
                                              * CURVE; FM 2-OP's INDEX, FEEDBACK, RATIO */
        return grav_engine_knob(*track_engine(track_sel), index);
    return PARAM_NAME[page][index % 3u];
}

static const char *page_name(uint32_t page)   /* a drum track's OSC: DRUM */
{
    if (page == PAGE_SOUND) {
        static char title[24];
        title[0] = 0;
        if (grav_controls_fm(*track_engine(track_sel)) && ui.sound_page[track_sel] >= 8u) {
            char op[8];
            text_cat(title, "OP "); int_text((ui.sound_page[track_sel] - 8u) / 8u + 1u, op);
            text_cat(title, op); text_cat(title, " ");
        }
        text_cat(title, grav_control_page(*track_engine(track_sel), ui.sound_page[track_sel] - 1u));
        return title;
    }
    return page == PAGE_OSC && SYNTH_IS_DRUM(track_sel) ? "DRUM" : PAGE_NAME[page];
}

/* a knob's value as the screen and a screen reader give it: PITCH in semitones (Beads' law), the rest as it is */
static void param_text(uint32_t page, uint32_t index, int32_t value, char *t)
{
    if (page == PAGE_SOUND) {
        grav_control_info_t i;
        t[0] = 0;
        if (!grav_control_info(*track_engine(track_sel), ui.sound_page[track_sel] - 1u, index, &i)) { text_cat(t,"--"); return; }
        if (value == GRAV_AUTO) { text_cat(t,"AUTO"); return; }
        if (i.kind == GC_STEP) {
            if (*track_engine(track_sel) == GRAV_ENGINE_CHIPTUNE && i.offset == 3) {
                static const char *const mode[] = {"UP","DOWN","UP/DOWN","RANDOM"};
                text_cat(t, mode[value]);
            } else if (*track_engine(track_sel) == GRAV_ENGINE_CHIPTUNE && i.offset == 4) int_text(1 << value, t);
            else if (!value) text_cat(t,"VOWEL");
            else int_text(value, t);
            return;
        }
        if (i.kind == GC_FM && i.offset == 142) {
            static const char *const wave[] = {"TRI","SAW DN","SAW UP","SQUARE","SINE","S&H"};
            text_cat(t, wave[value]); return;
        }
        if (i.kind == GC_FM && i.offset < 126 && (i.offset % 21 == 11 || i.offset % 21 == 12)) {
            static const char *const curve[] = {"-LIN","-EXP","+EXP","+LIN"};
            text_cat(t, curve[value]); return;
        }
        if (i.kind == GC_FM && i.offset < 126 && i.offset % 21 == 18 && value == 0) {
            uint8_t raw[GRAV_CONTROL_BYTES];
            const uint8_t *patch = set.controls[track_sel].value;
            if (!(set.controls[track_sel].flags & GRAV_CUSTOM_FM)) {
                grav_fm_patch(*track_engine(track_sel), (uint32_t)osc_params[track_sel][2], raw);
                patch = raw;
            }
            text_cat(t, patch[i.offset - 1] ? "0" : "0.5");
            return;
        }
        if (i.kind == GC_BOOL || i.kind == GC_FM_BOOL || i.kind == GC_FM_BIT) {
            if (i.kind == GC_FM_BOOL && i.offset < 126 && i.offset % 21 == 17) text_cat(t, value ? "FIXED" : "RATIO");
            else text_cat(t, value ? "ON" : "OFF");
        } else {
            if (i.kind == GC_SIGNED) value -= 50;
            else if (i.kind == GC_FM_SIGNED) value -= i.offset == 144 ? 24 : 7;
            else if (i.kind == GC_FM && i.offset == 134) value++;
            int_text(value, t);
        }
    } else if (page == PAGE_FX_MIX && index == 2) {
        static const char *const quality[] = {"DIGITAL","VINTAGE","CLEAN","DIRTY"};
        t[0] = 0; text_cat(t, quality[value]);
    } else if (page == PAGE_OUTPUT && SYNTH_IS_DRUM(track_sel) && index == 2) {
        t[0] = 0; text_cat(t, value ? "ON" : "OFF");
    } else if ((page == PAGE_OSC && index == 0u && SYNTH_IS_DRUM(track_sel)) ||
               (page == PAGE_OUTPUT && index == 0 && !SYNTH_IS_DRUM(track_sel))) {
        const int32_t half = value - 50, a = half < 0 ? -half : half;
        t[0] = half < 0 ? '-' : '+';
        int_text(a / 2, t + 1);
        if (a & 1)
            text_cat(t, ".5");
    } else if (page == PAGE_MOD && index == 2u) {
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

static void sound_param_turn(uint32_t index, int32_t e)
{
    grav_controls_t c = set.controls[track_sel];
    if (active_page == PAGE_SOUND) {
        grav_control_info_t i;
        if (!grav_control_info(*track_engine(track_sel), ui.sound_page[track_sel] - 1u, index, &i)) return;
        if (grav_controls_fm(*track_engine(track_sel)) && !(c.flags & GRAV_CUSTOM_FM))
            grav_fm_patch(*track_engine(track_sel), (uint32_t)osc_params[track_sel][2], c.value);
        int32_t old = grav_control_read(&c, &i);
        if (old == GRAV_AUTO) old = i.kind == GC_BOOL ? (e > 0 ? 0 : 1) : i.kind == GC_STEP ? (e > 0 ? -1 : i.max + 1) : 50;
        int32_t value = clamp(old + e, i.kind < GC_FM ? -1 : 0, i.max);
        grav_control_write(&c, &i, value < 0 ? GRAV_AUTO : (uint32_t)value);
    } else if (SYNTH_IS_DRUM(track_sel)) {
        if (index == 0) c.level = (uint8_t)clamp_percent(c.level + e);
        else if (index == 1) c.accent = (uint8_t)clamp_percent(c.accent + e);
        else if (e > 0) c.flags |= GRAV_DRUM_HOLD;
        else c.flags &= ~GRAV_DRUM_HOLD;
    } else {
        uint8_t *v = index == 0 ? &c.tune : index == 1 ? &c.level : &c.aux;
        *v = (uint8_t)clamp_percent(*v + e);
    }
    const uint32_t f = plat_lock();
    set.controls[track_sel] = c;
    plat_unlock(f);
    ui.preset_edited |= (uint8_t)(1u << track_sel);   /* (0.27: its preset's name kept, marked *) */
    set_dirty = 1;
    char t[12]; param_text(active_page, index, param_value(active_page, index), t);
    app_say(param_name(active_page, index), t);
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
    seq.edits++;                             /* (unsaved: counted with the change, so no switch comes between) */
    plat_unlock(f);
    ui.redraw = 1;
}

static void length_set(int32_t length)       /* the selected track's LENGTH */
{
    uint32_t f = plat_lock();
    TRK.length = (uint8_t)clamp(length, 1, (int32_t)SEQ_STEPS);
    seq.edits++;
    plat_unlock(f);
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
            length_set((int32_t)s + 1);
            ui.seq_used = 1;
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
    if (index == 0u) {                       /* the song's (a setting) */
        seq.bpm = (uint16_t)clamp((int32_t)seq.bpm + e, SEQ_BPM_MIN, SEQ_BPM_MAX);
        int_text(seq.bpm, t);
        app_say("TEMPO", t);
        set_dirty = 1;
    } else if (index == 1u) {                /* the selected track's own (0.25); SEQ held: the pattern's, every track's */
        const int all = ui.seq_down;
        uint32_t f = plat_lock();
        if (all) {
            seq.pat.swing = (uint8_t)clamp((int32_t)seq.pat.swing + e, SEQ_SWING_MIN, SEQ_SWING_MAX);
            for (uint32_t k = 0; k < SEQ_TRACKS; k++)
                seq.pat.track[k].swing = 0;
        } else
            TRK.swing = (uint8_t)clamp((int32_t)seq_track_swing(&seq.pat, track_sel) + e, SEQ_SWING_MIN, SEQ_SWING_MAX);
        seq.edits++;
        plat_unlock(f);
        if (all)
            ui.seq_used = 1;                 /* (SEQ let go: no step mode) */
        int_text((int32_t)seq_track_swing(&seq.pat, track_sel), t);
        text_cat(t, "%");
        app_say(all ? "SWING ALL" : "SWING", t);
    } else {
        length_set((int32_t)TRK.length + e);
        int_text(TRK.length, t);
        app_say("LENGTH", t);
    }
}

/* HOME held and a white key, K4, MIDI program change: the pattern to play next (stopped: at once, patterns()); the
 * one playing again: the queue undone */
static void pattern_pick(int32_t n)
{
    char t[8];
    n = clamp(n, 0, (int32_t)BANK_PATTERNS - 1);
    uint32_t f = plat_lock();
    seq.next = (uint32_t)n == seq.pattern ? SEQ_NO_PATTERN : (uint8_t)n;
    seq.next_ready = 0;
    plat_unlock(f);
    ui.patterns_until = plat_ms() + 1500u;
    ui.redraw = 1;
    int_text(n + 1, t);
    if (seq.playing)
        app_say(seq.next != SEQ_NO_PATTERN ? "NEXT PATTERN" : "PATTERN", t);
}

static uint32_t patterns_used(void)          /* a bit for each pattern with a step on (the one playing: as it is) */
{
    if (used_seq != bank.seq) {              /* (the flash's, again after each save) */
        used_seq = bank.seq;
        used_mask = 0;
        for (uint32_t n = 0; n < BANK_PATTERNS; n++) {
            const seq_pattern_t *p = bank_pattern(&bank, n);
            if (p && seq_has_steps(p))
                used_mask |= 1u << n;
        }
    }
    return (used_mask & ~(1u << seq.pattern)) | (seq_has_steps(&seq.pat) ? 1u << seq.pattern : 0u);
}

/* the patterns, each frame: the one playing saved when it is left (or on SAVE), a few pages a frame (the sound goes
 * on); the next one readied for the audio interrupt (stopped: in at once); the stale copies erased once nothing has
 * sounded for 3 s (an erase stops the sound for a moment) */
static void patterns(void)
{
    const uint32_t now = plat_ms();
    const int dirty = seq.edits != seq.saved;
    char t[8];
    if (midi_program) {                      /* a program change in: 1..16 the patterns */
        const uint32_t program = midi_program - 1u;
        midi_program = 0;
        if (program < BANK_PATTERNS)
            pattern_pick((int32_t)program);
    }
    if (bank_busy(&bank)) {
        const int r = bank_step(&bank, &seq.pat, 8u);   /* (each page a moment with interrupts off) */
        if (r > 0) {
            seq.saved = ui.save_gen;
            ui.redraw = 1;
        } else if (r < 0) {
            ui.save_req = 0;
            app_say("SAVE", "FAILED");
        }
    } else if (dirty && (ui.save_req || seq.next != SEQ_NO_PATTERN)) {
        if (bank_begin(&bank, seq.pattern) == 0) {
            ui.save_gen = seq.edits;
            ui.full_said = 0;
        } else if (!seq.playing) {
            bank_tidy(&bank);                /* stopped: room made now */
        } else if (!ui.full_said) {
            ui.full_said = 1;
            app_say("PATTERN", "STOP TO SAVE");
        }
    } else if (!dirty) {
        if (ui.save_req) {
            ui.save_req = 0;
            app_say("SAVED", "");
        }
        if (seq.next != SEQ_NO_PATTERN && !seq.playing) {   /* stopped: the next one in now */
            uint32_t f = plat_lock();
            seq_load(&seq, bank_pattern(&bank, seq.next));
            seq.pattern = seq.next;
            seq.next = SEQ_NO_PATTERN;
            seq.next_ready = 0;
            plat_unlock(f);
        } else if (seq.next != SEQ_NO_PATTERN && !seq.next_ready) {   /* playing: in at the pattern's end */
            seq.next_src = bank_pattern(&bank, seq.next);
            seq.next_ready = 1;
        }
    }
    if (seq.pattern + 1u != ui.shown_pattern) {   /* switched: said, shown */
        if (ui.shown_pattern) {
            int_text(seq.pattern + 1, t);
            app_say("PATTERN", t);
        }
        ui.shown_pattern = (uint8_t)(seq.pattern + 1u);
        ui.step_held = -1;
        ui.redraw = 1;
    }
    if (seq.next != SEQ_NO_PATTERN && now - ui.blink_at >= 250u) {   /* the next one blinks */
        ui.blink_at = now;
        ui.redraw = 1;
    }
    if (ui.patterns_until && now >= ui.patterns_until) {
        ui.patterns_until = 0;
        ui.redraw = 1;
    }
    if (ui.home_down && !ui.home_shown && !ui.sound_nav && now - ui.home_at >= 300u) {
        ui.home_shown = 1;
        ui.redraw = 1;
    }
    if (seq.playing || ui.keys || !synth_quiet(&synth))
        ui.quiet_at = now;
    else if (!bank_busy(&bank) && bank_count(&bank, BANK_STALE) && now - ui.quiet_at >= 3000u &&
             now - ui.tidy_at >= 250u) {
        ui.tidy_at = now;
        bank_tidy(&bank);
    }
}

/* SELECT, or a black key in step mode: the track the keys, MIDI in, PRESET, OSC, VOICES and the steps are for */
static void track_select(uint32_t t)
{
    if (t >= SYNTH_TRACKS)
        return;
    if (t != track_sel) {
        const uint32_t previous_page = active_page;
        const uint32_t previous_sound = ui.sound_page[track_sel];
        track_sel = (uint8_t)t;
        ui.step_held = -1;
        if (IS_SOUND_PAGE(previous_page)) {
            uint32_t page = previous_page == PAGE_OUTPUT ? sound_pages() - 1u :
                            previous_page == PAGE_OSC || (!SYNTH_IS_DRUM(t) && previous_page == PAGE_SHAPE) ? 0u :
                            previous_sound < sound_pages() - 1u ? previous_sound : sound_pages() - 2u;
            sound_activate(page);
        }
    }
    if (!SYNTH_IS_DRUM(t) && home_page == PAGE_SHAPE)   /* (SHAPE: a drum's; a synth track's sound instead) */
        home_page = PAGE_OSC;
    if (!SYNTH_IS_DRUM(t) && active_page == PAGE_SHAPE)
        active_page = PAGE_OSC;
    ui.tracks_until = plat_ms() + 1500u;
    ui.redraw = 1;
    app_say(TRACK_NAME[t], grav_engine_name(*track_engine(t)));
}

/* step mode off: the white keys play again, no step held */
static void step_mode_off(void)
{
    ui.step_mode = 0;
    ui.step_held = -1;
}

/* HOME's own view: the terricons (SEQ's page, step mode off) */
static void show_home(void)
{
    step_mode_off();
    active_page = home_page = PAGE_SEQ;
}

static void rec_step_say(void)               /* step recording: the step the next chord goes to */
{
    char a[16], b[8];
    a[0] = 0;
    text_cat(a, "REC STEP ");
    int_text(ui.rec_pos + 1, b);
    text_cat(a, b);
    app_say(a, "");
}

/* the keys let go of a chord: it is the next new step's, and with REC (stopped), it is written at rec_pos */
static void chord_done(void)
{
    ui.last_n = ui.chord_n;
    for (uint32_t k = 0; k < SEQ_NOTES; k++)
        ui.last_chord[k] = k < ui.chord_n ? ui.chord[k] : ui.chord[0];
    ui.last_vel = ui.chord_vel;
    if (!ui.rec || seq.playing)              /* (recording live: written as they came, live_note) */
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
    rec_step_say();
}

/* (0.24) live recording: REC while playing. A key's note goes to the selected track at once, quantized to its
 * nearest step (seq_quantize): the one playing if pressed in its first half, else the next, which then does not
 * play this time round (the note is heard already). The first note on a step replaces what was there; notes within
 * LIVE_CHORD_MS of it join it as a chord (up to four). Its velocity is the key's (or GLO's fixed one); its length,
 * how long the key is held, in steps (at least one). A drum track's: the drum's note, whatever the key. */
#define LIVE_CHORD_MS 60u
static void live_note(uint32_t slot, int32_t note, uint32_t vel)
{
    const uint32_t now = plat_ms(), t = track_sel;
    const uint8_t n8 = (uint8_t)(SYNTH_IS_DRUM(t) ? DRUM_NOTE[t - SYNTH_S_TRACKS] : clamp(note, 0, 127));
    const int chord = ui.rec_at && now - ui.rec_at < LIVE_CHORD_MS && ui.live_track == t &&
                      ui.live_pattern == seq.pattern;
    int ahead = 0;
    uint32_t f = plat_lock();
    const int32_t s = chord ? ui.live_step : seq_quantize(&seq, t, &ahead);
    if (s < 0 || (uint32_t)s >= seq.pat.track[t].length) {
        plat_unlock(f);
        return;
    }
    seq_step_t *st = &seq.pat.track[t].step[s];
    if (chord && st->on) {                   /* a chord's further note */
        int there = 0;
        for (uint32_t i = 0; i < st->n && i < SEQ_NOTES; i++)
            there |= st->note[i] == n8;
        if (!there && st->n < SEQ_NOTES)
            st->note[st->n++] = n8;
    } else {                                 /* a step's first: it replaces what was there */
        st->on = 1;
        st->n = 1;
        for (uint32_t i = 0; i < SEQ_NOTES; i++)
            st->note[i] = n8;
        st->vel = (uint8_t)clamp((int32_t)vel, 1, 127);
        st->len = 1;
    }
    if (chord ? ui.live_ahead && seq.step[t] != s : ahead)
        seq.skip[t] = (uint8_t)(s + 1);      /* (heard now: not again as the step comes) */
    seq.edits++;
    plat_unlock(f);
    if (!chord) {
        ui.rec_at = now ? now : 1u;
        ui.live_track = (uint8_t)t;
        ui.live_step = (uint8_t)s;
        ui.live_ahead = (uint8_t)ahead;
        ui.live_pattern = seq.pattern;
    }
    ui.live_held[slot] = 1;
    ui.live_slot_step[slot] = (uint8_t)s;
    ui.live_slot_track[slot] = (uint8_t)t;
    ui.live_slot_note[slot] = (uint8_t)clamp(note, 0, 127);
    ui.live_slot_at[slot] = now;
}

static void live_release(uint32_t slot)      /* a note recorded live let go: its step's length, as long as it was held */
{
    if (!ui.live_held[slot])
        return;
    ui.live_held[slot] = 0;
    if (ui.live_pattern != seq.pattern)      /* (another pattern came in meanwhile) */
        return;
    const uint32_t step_ms = 15000u / (seq_bpm(&seq) ? seq_bpm(&seq) : 120u);
    const uint32_t len = (uint32_t)clamp((int32_t)((plat_ms() - ui.live_slot_at[slot] + step_ms / 2u) / step_ms), 1,
                                         (int32_t)SEQ_STEPS);
    uint32_t f = plat_lock();
    seq_step_t *st = &seq.pat.track[ui.live_slot_track[slot]].step[ui.live_slot_step[slot]];
    if (st->on && len > st->len) {           /* (a chord: its longest) */
        st->len = (uint8_t)len;
        seq.edits++;
    }
    plat_unlock(f);
}

/* (0.25) MIDI in's notes while recording live (rec_midi_on): queued by the audio interrupt (midi_in), recorded here as
 * the keys' are, each in a slot of its own (NKEYS on) until its note-off */
static void rec_midi(void)
{
    while (rec_midi_tail != rec_midi_head) {
        const uint32_t e = rec_midi_q[rec_midi_tail % REC_MIDI_N], note = e & 127u, vel = e >> 8;
        rec_midi_tail++;
        uint32_t slot = LIVE_SLOTS;
        for (uint32_t i = NKEYS; i < LIVE_SLOTS; i++)
            if (ui.live_held[i] && ui.live_slot_note[i] == note)
                slot = i;
        if (vel) {
            if (slot < LIVE_SLOTS)           /* (on again before its off: that one let go first) */
                live_release(slot);
            for (slot = NKEYS; slot < LIVE_SLOTS && ui.live_held[slot]; slot++) {}
            if (slot < LIVE_SLOTS && ui.rec && seq.playing)
                live_note(slot, (int32_t)note, vel);
        } else if (slot < LIVE_SLOTS)
            live_release(slot);
    }
}

/* (0.26) the mixer: a track muted (its steps start no notes, the ones sounding end), or on again */
static void mutes_apply(void)               /* the tracks the sequencer silences: all but the soloed, or the muted */
{
    seq.mute = ui.solo ? (uint8_t)~ui.solo : set.mute;
}

static void mute_toggle(uint32_t t)
{
    set.mute ^= (uint8_t)(1u << t);
    mutes_apply();
    set_dirty = 1;
    ui.redraw = 1;
    app_say(TRACK_NAME[t], set.mute >> t & 1u ? "MUTE" : "ON");
}

/* (0.26.1) a track soloed (the others silent while any is), or not; several at once; the mutes kept beneath */
static void solo_toggle(uint32_t t)
{
    ui.solo ^= (uint8_t)(1u << t);
    mutes_apply();
    ui.redraw = 1;
    app_say(TRACK_NAME[t], ui.solo >> t & 1u ? "SOLO" : "SOLO OFF");
}

/* the mixer's knobs: K1 the selected track's VOL (its fader, 0.27; before, its sound's LEVEL), K2 its SEND (its
 * share through the effect), K3 the effect's RETURN (FX MIX's DRY/WET) */
static void mixer_turn(uint32_t index, int32_t e)
{
    char a[16], t[8];
    if (index == 0u) {                       /* (0.27) its fader: the mixer's, not its sound's (a preset leaves it) */
        set.vol[track_sel] = (uint8_t)clamp((int32_t)set.vol[track_sel] + e, 0, 100);
        set_dirty = 1;
        int_text(set.vol[track_sel], t);
    } else if (index == 1u) {
        set.send[track_sel] = (uint8_t)clamp((int32_t)set.send[track_sel] + e, 0, 100);
        set_dirty = 1;
        int_text(set.send[track_sel], t);
        text_cat(t, "%");
    } else {
        fx_params[FX_DRY_WET] = clamp_percent(fx_params[FX_DRY_WET] + e);
        set_dirty = 1;
        int_text(fx_params[FX_DRY_WET], t);
        text_cat(t, "%");
        app_say("RETURN", t);
        return;
    }
    a[0] = 0;
    text_cat(a, PARAM_NAME[PAGE_MIXER][index]);
    text_cat(a, " ");
    text_cat(a, TRACK_NAME[track_sel]);
    app_say(a, t);
}

/* SEQ held, REC: the selected track's steps all off (its length kept) */
static void track_clear(void)
{
    uint32_t f = plat_lock();
    for (uint32_t i = 0; i < SEQ_STEPS; i++)
        TRK.step[i].on = 0;
    seq.skip[track_sel] = 0;
    seq.edits++;
    plat_unlock(f);
    ui.step_held = -1;
    app_say("CLEAR", TRACK_NAME[track_sel]);
}

static uint32_t page_button(uint32_t page)   /* the button that opens a page */
{
    return page == PAGE_SEQ || IS_SOUND_PAGE(page) ? B_HOME : page == PAGE_MOD ? B_LFO :
           page == PAGE_ENV ? B_ENV :
           page == PAGE_GLO || page == PAGE_SCREEN || page == PAGE_DEBUG ? B_GLO : page == PAGE_MIXER ? B_SEL : B_FX;
}

/* its blink's period: the first of a button's pages slow, the next faster (FX: MIX, GRAIN, SHAPE) */
static uint32_t page_period(uint32_t page)
{
    return page == PAGE_OSC || page == PAGE_SCREEN || page == PAGE_FX_GRAIN ? 500u :
           page == PAGE_FX_SHAPE || page == PAGE_SHAPE || page == PAGE_DEBUG ? 250u : 1000u;
}

/* recording live follows the sequencer: once it stops (PLAY, a MIDI stop), no more; the metronome and MIDI in's
 * notes while it lasts; SEQ held, the selected track's steps erased as they come */
static void rec_follow(uint32_t now)
{
    if (ui.rec && seq.playing)
        ui.rec_live = 1;
    else if (ui.rec && ui.rec_live) {
        ui.rec = 0;
        ui.redraw = 1;
    }
    if (!ui.rec)
        ui.rec_live = 0;
    seq.beats = (uint8_t)(ui.rec && seq.playing && set.metro != METRO_OFF);
    rec_midi_on = (uint8_t)(ui.rec && seq.playing);
    if (ui.rec && seq.playing && ui.seq_down && now - ui.seq_at >= 150u) {
        const int32_t st = seq.step[track_sel];
        if (st >= 0 && TRK.step[st].on) {
            uint32_t f = plat_lock();
            TRK.step[st].on = 0;
            seq.edits++;
            plat_unlock(f);
        }
        if (!ui.erasing) {
            ui.erasing = 1;
            ui.seq_used = 1;                 /* (SEQ let go: no step mode) */
            app_say("ERASE", TRACK_NAME[track_sel]);
        }
    } else if (ui.erasing && !ui.seq_down) {
        ui.erasing = 0;
        ui.redraw = 1;
    }
}

/* what the sequencer played since: each track's note begun (its lights and terricon flash), the selected track's step,
 * the quarter notes (the bar's first marked) */
static void seq_follow(uint32_t now)
{
    for (uint32_t t = 0; t < SEQ_TRACKS; t++)
        if (seq.hits[t] != ui.shown_hits[t]) {
            ui.shown_hits[t] = seq.hits[t];
            ui.hit_at[t] = now;
        }
    if (seq.step[track_sel] != ui.lane_step) {
        ui.lane_step = seq.step[track_sel];
        ui.step_at = now;
    }
    if (seq.playing) {
        const uint32_t count = seq.count;
        if (count != ui.beat_count) {
            ui.beat_count = count;
            if (count && (count - 1u) % 4u == 0u) {
                ui.beat_at = now;
                ui.beat_bar = (count - 1u) % 16u == 0u;
            }
        }
    }
}

/* the keys' lights (lit, mid, dim; a mode's own, else as the keys are held) */
typedef struct { uint32_t lit, mid, dim; } key_lights_t;

static void step_lights(key_lights_t *l, uint32_t now)   /* step mode: a step on lit, off within the length mid,
                                                           * past it dim; the one playing the other way round; the
                                                           * held one lit. Black 1..8 the tracks: the selected lit,
                                                           * a note flashing, one with no voice dim */
{
    const uint32_t length = TRK.length;
    for (uint32_t i = 0; i < SEQ_PAGE; i++) {
        const uint32_t s = ui.view_page * SEQ_PAGE + i, bit = 1u << WHITE_KEYS[i];
        const int on = TRK.step[s].on, inside = s < length;
        const int playing = seq.playing && seq.step[track_sel] == (int32_t)s;
        if (ui.step_held == (int8_t)s || (playing ? !on : on && inside))
            l->lit |= bit;
        else if (inside && !playing)
            l->mid |= bit;
        else if (!inside)
            l->dim |= bit;
    }
    for (uint32_t t = 0; t < SYNTH_TRACKS; t++) {
        const uint32_t bit = 1u << BLACK_KEYS[t];
        if (t == track_sel || now - ui.hit_at[t] < 80u)
            l->lit |= bit;
        else if (SYNTH_IS_DRUM(t) || set.trk_voices[t])
            l->mid |= bit;
        else
            l->dim |= bit;
    }
}

static void mixer_lights(key_lights_t *l, uint32_t now)   /* (0.26) the mixer: white 1..8 a track playing lit,
                                                            * muted dim; black 1..8 soloed lit, a note flashing */
{
    for (uint32_t t = 0; t < SYNTH_TRACKS; t++) {
        const uint32_t w = 1u << WHITE_KEYS[t], b = 1u << BLACK_KEYS[t];
        if (set.mute >> t & 1u)
            l->dim |= w;
        else
            l->lit |= w;
        if (ui.solo >> t & 1u)
            l->lit |= b;
        else if (now - ui.hit_at[t] < 80u)
            l->mid |= b;
        else
            l->dim |= b;
    }
}

static void pattern_lights(key_lights_t *l, uint32_t now)   /* HOME held: the playing pattern lit, the next
                                                              * blinking, one with steps mid, an empty one dim */
{
    const uint32_t used = patterns_used();
    for (uint32_t n = 0; n < BANK_PATTERNS; n++) {
        const uint32_t bit = 1u << WHITE_KEYS[n];
        if (n == seq.pattern || (n == seq.next && (now / 125u) & 1u))
            l->lit |= bit;
        else if (used & 1u << n || n == seq.next)
            l->mid |= bit;
        else
            l->dim |= bit;
    }
}

static void slot_lights(key_lights_t *l)   /* SAVE held: the page's preset slots, a saved one mid, the key just
                                            * pressed lit */
{
    for (uint32_t n = 0; n < 16u; n++) {
        const uint32_t bit = 1u << WHITE_KEYS[n], slot = 16u * ui.preset_page + n;
        if (ui.key_preset & bit) l->lit |= bit;
        else if (preset_used(&user_presets, slot)) l->mid |= bit;
        else l->dim |= bit;
    }
}

/* the lights: the buttons (the open page's blinking, PLAY green on the beat, red recording; SAVE with something
 * unsaved, EDIT frozen, SEQ in step mode, FX with the effect on, SEL with SEED on its own) and the keys */
static void lights(uint32_t btn, uint32_t keys)
{
    const uint32_t now = plat_ms(), play = 1u << B_PLAY;
    rec_follow(now);
    seq_follow(now);
    uint32_t lit = (btn & ~play) | (set_dirty || preset_dirty || seq.edits != seq.saved ? 1u << B_SAVE : 0u) |
                   (fx_freeze ? 1u << B_EDIT : 0u) | (ui.step_mode ? 1u << B_SEQ : 0u), mid = 0;
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
    if (ui.rec) {                            /* PLAY red; REC lit on each beat recording live, else blinking */
        const int on = seq.playing ? now - ui.beat_at < (ui.beat_bar ? 120u : 60u) : (now / 125u) & 1u;
        lit |= play;
        if (on)
            lit |= 1u << B_REC;
        else
            mid |= 1u << B_REC;
    } else if (seq.playing && now - ui.beat_at < (ui.beat_bar ? 120u : 60u))
        lit |= LED_PLAY_GREEN;               /* PLAY green on each beat, longer on the bar's first */
    key_lights_t k = {keys, 0, 0};
    if (ui.preset_down || ui.home_down || ui.step_mode || active_page == PAGE_MIXER) {
        k.lit = k.mid = 0;
        if (ui.preset_down)
            slot_lights(&k);
        else if (ui.home_down)
            pattern_lights(&k, now);
        else if (ui.step_mode)
            step_lights(&k, now);
        else
            mixer_lights(&k, now);
    }
    uint32_t dim = ((1u << NB) - 1u) & ~lit & ~mid;
    if (!(lit & LED_PLAY_GREEN) && !(lit & play))
        dim = (dim & ~play) | LED_PLAY_GREEN;   /* PLAY glows green, as the other buttons glow */
    else
        dim &= ~play;
    plat_lights(lit, k.lit, dim, k.dim, 2);
    plat_lights_mid(mid, k.mid);
}

static void page_open(uint32_t k)            /* a page's button: its page (HOME, GLO, FX again: the next one) */
{
    if (k == B_FX && IS_FX_PAGE(active_page))   /* FX again: the next of its pages */
        fx_page = active_page == PAGE_FX_MIX ? PAGE_FX_GRAIN : active_page == PAGE_FX_GRAIN ? PAGE_FX_SHAPE :
                  PAGE_FX_MIX;            /* (0.26.2: MIX, GRAIN, SHAPE) */
    if (k == B_HOME && ui.step_mode) {       /* from step mode (every track): HOME's own view, the one track */
        show_home();
        app_say("HOME", TRACK_NAME[track_sel]);
        return;
    }
    if (k == B_HOME) {
        if (active_page == PAGE_SEQ) sound_activate(0);
        else if (IS_SOUND_PAGE(active_page)) {
            if (ui.sound_page[track_sel] + 1u < sound_pages()) sound_activate(ui.sound_page[track_sel] + 1u);
            else active_page = home_page = PAGE_SEQ;
        } else if (IS_SOUND_PAGE(home_page)) sound_activate(ui.sound_page[track_sel]);
        else active_page = home_page;
        app_say("PAGE", page_name(active_page));
        ui.redraw = 1;
        return;
    }
    if (k == B_GLO)                          /* GLO: GLO, again DEBUG, and back */
        glo_page = active_page == PAGE_GLO ? PAGE_SCREEN : active_page == PAGE_SCREEN ? PAGE_DEBUG :
                   active_page == PAGE_DEBUG ? PAGE_GLO : glo_page;
    active_page = k == B_LFO  ? PAGE_MOD :
                  k == B_ENV  ? PAGE_ENV :
                  k == B_GLO  ? glo_page : fx_page;
    app_say("PAGE", page_name(active_page));
    ui.redraw = 1;
}

/* the keys pressed as something other than notes (a preset slot, a pattern, a mute or a solo, a step or a track):
 * let go as such, whatever the mode is by then; no pressure sent, no chord */
static uint32_t keys_taken(void)
{
    return ui.key_preset | ui.key_pat | ui.key_mix | ui.key_step;
}

/* a key pressed: SAVE held, a preset slot; HOME held, a pattern (1..16); the mixer, a mute (white 1..8) or a solo
 * (black 1..8, 0.26.1); step mode, a step (white) or a track (black 1..8); else a note on the selected track */
static void key_down(uint32_t k)
{
    const uint32_t bit = 1u << k;
    const int w = key_index(WHITE_KEYS, 16, k), b = key_index(BLACK_KEYS, 11, k);
    if (ui.preset_down) {
        ui.key_preset |= bit;
        ui.preset_used = ui.preset_shown = 1;
        if (w >= 0)
            preset_save_slot(16u * ui.preset_page + (uint32_t)w);
    } else if (ui.home_down) {
        ui.key_pat |= bit;
        ui.home_used = 1;
        if (w >= 0)
            pattern_pick(w);
    } else if (active_page == PAGE_MIXER) {   /* (the other keys silent) */
        ui.key_mix |= bit;
        if (w >= 0 && w < (int)SYNTH_TRACKS)
            mute_toggle((uint32_t)w);
        else if (b >= 0 && b < (int)SYNTH_TRACKS)
            solo_toggle((uint32_t)b);
    } else if (ui.step_mode) {
        ui.key_step |= bit;
        if (w >= 0)
            step_key((uint32_t)w, 1);
        else if (b >= 0 && b < (int)SYNTH_TRACKS)
            track_select((uint32_t)b);
    } else {                                 /* a note: key 0 is F3 (MIDI 53), the octave as it is now */
        const int32_t note = 53 + (int32_t)k + 12 * set.octave;
        const uint32_t v = set.velocity_off ? FIXED_VELOCITY : touch_vel(k);   /* GLO VELOCITY: the keys', or fixed */
        ui.vel[k] = (uint8_t)v;
        ui.prs[k] = 0;
        ui.note[k] = (uint8_t)note;
        ui.last_key = (int32_t)k;
        ui.key_track[k] = track_sel;         /* (its note-off to the same track, whatever is selected then) */
        queue_key_event(SYNTH_EVENT_NOTE_ON, note, v, track_sel);
        plat_midi_send(0x09u | (0x90u | track_sel) << 8 | (uint32_t)note << 16 | v << 24, TO_USB);   /* its channel */
        if (ui.rec && seq.playing)           /* recording live: written as it plays */
            live_note(k, note, v);
        if (!ui.chord_open) {                /* a chord: the notes held together, in the order they came */
            ui.chord_open = 1;
            ui.chord_n = 0;
            ui.chord_vel = (uint8_t)v;
        }
        if (ui.chord_n < SEQ_NOTES)
            ui.chord[ui.chord_n++] = (uint8_t)clamp(note, 0, 127);
    }
}

static void key_up(uint32_t k)
{
    const uint32_t bit = 1u << k;
    if (ui.key_step & bit) {                 /* (a step: let go as one) */
        const int w = key_index(WHITE_KEYS, 16, k);
        ui.key_step &= ~bit;
        if (w >= 0)
            step_key((uint32_t)w, 0);
    } else if (keys_taken() & bit) {
        ui.key_preset &= ~bit;
        ui.key_pat &= ~bit;
        ui.key_mix &= ~bit;
    } else {
        const int32_t note = ui.note[k];
        ui.prs[k] = 0;
        queue_key_event(SYNTH_EVENT_NOTE_OFF, note, 0, ui.key_track[k]);
        plat_midi_send(0x08u | (0x80u | ui.key_track[k]) << 8 | (uint32_t)note << 16, TO_USB);
        live_release(k);
    }
}

/* the keys: pressed, let go, a held note's pressure (out over MIDI; not drawn); the chord done once its notes are */
static void keys_input(uint32_t keys)
{
    for (uint32_t k = 0; k < NKEYS; k++) {
        const uint32_t bit = 1u << k;
        if ((keys & bit) && !(ui.keys & bit))
            key_down(k);
        else if (!(keys & bit) && (ui.keys & bit))
            key_up(k);
        else if ((keys & bit) && !(keys_taken() & bit) && touch_pressure(k) != ui.prs[k]) {
            ui.prs[k] = touch_pressure(k);
            plat_midi_send(0x0Au | (0xA0u | ui.key_track[k]) << 8 | (uint32_t)ui.note[k] << 16 |
                           (uint32_t)ui.prs[k] << 24, TO_USB);
        }
    }
    if (ui.chord_open && !(keys & ~keys_taken())) {
        ui.chord_open = 0;
        chord_done();
    }
}

/* REC: stopped, step recording (the keys' notes and chords one step each); playing, live recording; SEQ held, the
 * track cleared */
static void rec_press(void)
{
    if (ui.seq_down) {
        ui.seq_used = 1;
        track_clear();
        return;
    }
    ui.rec = !ui.rec;
    if (!ui.rec)
        app_say("REC", "OFF");
    else if (seq.playing) {
        show_home();
        app_say("REC", "LIVE");
    } else {
        show_home();
        ui.rec_pos = (uint8_t)(ui.view_page * SEQ_PAGE < TRK.length ? ui.view_page * SEQ_PAGE : 0u);
        rec_step_say();
    }
}

/* PLAY: the sequencer started (REC on: recording live from the start; METRONOME COUNT: after a bar), or stopped */
static void play_press(void)
{
    if (seq.playing) {
        seq.want = SEQ_WANT_STOP;
        app_say("STOP", "");
    } else {
        const int count = ui.rec && set.metro == METRO_COUNT && !seq.ext;
        seq.count_in = count ? 16u : 0u;
        seq.want = SEQ_WANT_PLAY;
        app_say(ui.rec ? "REC" : "PLAY", ui.rec ? (count ? "COUNT IN" : "LIVE") : seq.ext ? "MIDI CLOCK" : "");
    }
}

/* OCT- and OCT+: SAVE held, the preset slots' page; step mode, the steps' page; else the octave */
static void octave_press(int32_t e)
{
    if (ui.preset_down) {
        ui.preset_page = (uint8_t)clamp((int32_t)ui.preset_page + e, 0, 3);
        ui.preset_used = ui.preset_shown = ui.redraw = 1;
    } else if (ui.step_mode) {
        char t[4];
        ui.view_page = (uint8_t)clamp((int32_t)ui.view_page + e, 0, SEQ_PAGES - 1);
        int_text(ui.view_page + 1, t);
        app_say("PAGE", t);
    } else
        octave_step(e);
}

/* a button pressed. The presets' browser open (0.27): OCT+ all of them or the engine's, OCT- back as it was; another
 * button closes it, keeping the sound, and acts. HOME, SEQ, SEL and SAVE act when let go (held, they change the keys
 * and the knobs) */
static void button_press(uint32_t k)
{
    if (pb.open) {
        if (k == B_OCTUP) {
            pb.all = !pb.all;
            pb.until = plat_ms() + 3000u;
            app_say("PRESETS", pb.all ? "ALL ENGINES" : grav_engine_name(pb_engine(pb.sel)));
            return;
        }
        if (k == B_OCTDN) {
            pb_cancel();
            return;
        }
        pb_close();
    }
    switch (k) {
    case B_HOME:
        ui.home_down = 1;
        ui.home_used = ui.home_shown = 0;
        ui.sound_nav = 0;
        ui.home_at = plat_ms();
        break;
    case B_LFO:
    case B_ENV:
    case B_GLO:
    case B_FX:
        page_open(k);
        break;
    case B_PLAY:
        play_press();
        break;
    case B_REC:
        rec_press();
        break;
    case B_SEQ:                              /* (let go: step mode on or off; held, a white key the length) */
        ui.seq_down = 1;
        ui.seq_used = 0;
        ui.seq_at = plat_ms();
        break;
    case B_EDIT:                             /* FREEZE: the effect's recording held, or let go */
        fx_freeze = !fx_freeze;
        app_say("FREEZE", fx_freeze ? "ON" : "OFF");
        break;
    case B_SEL:                              /* (let go: the mixer, from every page, 0.26.2; held on an FX page, SEED) */
        ui.sel_down = 1;
        ui.sel_used = 0;
        ui.sel_at = plat_ms();
        break;
    case B_OCTDN:
    case B_OCTUP:
        octave_press(k == B_OCTUP ? 1 : -1);
        break;
    case B_SAVE:                             /* (latched before the keys: a tap saves when let go) */
        break;
    default:
        app_say("BUTTON", BTN_NAME[k]);
        break;
    }
}

/* SAVE let go: a tap saves the settings (and the presets waiting); held, it showed the preset slots */
static void save_let_go(void)
{
    ui.preset_down = 0;
    ui.redraw = 1;
    if (!ui.preset_used && !ui.preset_shown) {
        settings_capture();
        if (memcmp(&set, &set_stored, sizeof set) && plat_store_save(OBJ_SETTINGS, &set, sizeof set) != 0) {
            app_say("SAVE", "FAILED");
        } else {
            set_stored = set;
            set_dirty = 0;
            ui.save_req = 1;
        }
        if (preset_dirty) preset_write_req = 1;
    }
    ui.preset_shown = 0;
}

/* SEQ let go, unless it set the length: step mode (every track, the white keys the steps), or back where it was */
static void seq_let_go(void)
{
    ui.seq_down = 0;
    if (ui.seq_used)
        return;
    ui.step_mode = !ui.step_mode;
    ui.step_held = -1;
    if (ui.step_mode) {
        ui.rec = 0;
        ui.seq_from = active_page;
        ui.seq_from_home = home_page;
        active_page = home_page = PAGE_SEQ;
    } else if (IS_SOUND_PAGE(ui.seq_from))   /* (the track's own sound page: it may be another track now) */
        sound_activate(ui.sound_page[track_sel]);
    else {
        active_page = ui.seq_from;
        home_page = ui.seq_from_home;
    }
    app_say("STEP", ui.step_mode ? "ON" : "OFF");
    ui.redraw = 1;
}

/* SEL: let go, the mixer (again: back to the page it was opened on), unless it was held as SEED; held on an FX page,
 * SEED: grains by hand while it is held, and from then on (held 2 s more: on their own again) */
static void sel_follow(uint32_t btn)
{
    if (ui.sel_down && !(btn & 1u << B_SEL)) {
        ui.sel_down = 0;
        fx_seed = ui.seed_down = 0;
        if (!ui.sel_used) {
            if (active_page == PAGE_MIXER)
                active_page = ui.mixer_from;
            else {
                ui.mixer_from = active_page;
                active_page = PAGE_MIXER;
            }
            app_say("PAGE", page_name(active_page));
            ui.redraw = 1;
        }
    } else if (ui.sel_down && !ui.seed_down && IS_FX_PAGE(active_page) && plat_ms() - ui.sel_at >= SEED_HOLD_MS) {
        ui.sel_used = 1;
        ui.seed_down = 1;
        ui.seed_at = plat_ms();
        fx_seed = 1;
        app_say("SEED", fx_seed_auto ? "MANUAL" : "");
        fx_seed_auto = 0;
    } else if (ui.seed_down && !fx_seed_auto && plat_ms() - ui.seed_at >= SEED_AUTO_MS) {
        fx_seed_auto = 1;
        app_say("SEED", "AUTO");
    }
}

/* the buttons let go: SAVE, HOME (its page, unless it picked a pattern), SEQ, SEL */
static void buttons_let_go(uint32_t btn)
{
    if (ui.preset_down && !(btn & 1u << B_SAVE))
        save_let_go();
    if (ui.home_down && !(btn & 1u << B_HOME)) {
        ui.home_down = 0;
        if (ui.home_shown)
            ui.redraw = 1;
        ui.home_shown = 0;
        if (!ui.home_used)
            page_open(B_HOME);
    }
    if (ui.seq_down && !(btn & 1u << B_SEQ))
        seq_let_go();
    sel_follow(btn);
}

/* DEBUG's knobs: K1 one core or two, K2 its counts reset */
static void debug_turn(uint32_t index, int32_t e)
{
    if (index == 0u) {
        grav_dual_core = e > 0;
        app_say("CORES", grav_dual_core ? "2" : "1");
    } else if (index == 1u) {
        grav_debug_reset = 1;
        plat_audio_debug_reset();
        ui.debug_xruns_base = plat_xruns();
        app_say("DEBUG", "RESET");
    }
}

/* SELECT: HOME held, the sound's pages (round); step recording, the step; else the track, on every page */
static void select_turn(int32_t e)
{
    if (ui.home_down) {
        const int32_t n = (int32_t)sound_pages();
        ui.home_used = ui.sound_nav = 1;
        ui.home_shown = 0;
        ui.patterns_until = ui.tracks_until = ui.splash_until = 0;
        sound_activate((uint32_t)(((int32_t)ui.sound_page[track_sel] + e % n + n) % n));
        app_say("PAGE", page_name(active_page));
    } else if (ui.rec && !seq.playing) {
        const int32_t n = (int32_t)TRK.length;
        char t[8];
        ui.rec_pos = (uint8_t)(((int32_t)ui.rec_pos + e % n + n) % n);
        ui.view_page = (uint8_t)(ui.rec_pos / SEQ_PAGE);
        int_text(ui.rec_pos + 1, t);
        app_say("REC STEP", t);
    } else {
        pb_close();                          /* (0.27: another track: the browser closes) */
        track_select((uint32_t)clamp((int32_t)track_sel + e, 0, SYNTH_TRACKS - 1));
    }
}

/* LFO's K3: where the LFO goes (TIMBRE, MORPH, PITCH), a third of the knob each */
static void lfo_dest_turn(int32_t e)
{
    const uint32_t destination = (uint32_t)clamp((int32_t)lfo_destination(mod_params[2]) + e, 0, 2);
    const int32_t value = (int32_t)destination * 34;
    if (value != mod_params[2]) {
        mod_params[2] = value;
        set_dirty = 1;
    }
    app_say("LFO DEST", LFO_DEST_NAME[destination]);
}

/* the selected track's 6-op FM sound back to its engine's own (AUTO) values: a new engine, or ALGORITHM turned */
static void controls_auto(uint32_t t)
{
    const uint32_t f = plat_lock();
    set.controls[t].flags &= ~GRAV_CUSTOM_FM;
    memset(set.controls[t].value, GRAV_AUTO, GRAV_CONTROL_BYTES);
    plat_unlock(f);
}

/* a page's knob, 0..100: OSC, SHAPE, LFO, ENV, the effect's (the sound's edits marked *, 0.27) */
static void page_knob_turn(uint32_t index, int32_t e)
{
    volatile int32_t *value = param_of(active_page, index);
    const int32_t previous = *value;
    char t[12];
    *value = clamp_percent(previous + e);
    if (*value != previous) {
        set_dirty = 1;
        if (active_page == PAGE_OSC || active_page == PAGE_SHAPE)
            ui.preset_edited |= (uint8_t)(1u << track_sel);
        if (active_page == PAGE_OSC && index == 2 && grav_controls_fm(*track_engine(track_sel)))
            controls_auto(track_sel);
    }
    param_text(active_page, index, *value, t);
    app_say(param_name(active_page, index), t);
}

/* ALGO: the selected track's engine, in Plaits' order (a drum track: its six drums); its sound the engine's own,
 * its first page, its splash a moment. The presets' browser closes (0.27) */
static void algo_turn(int32_t e)
{
    pb_close();
    uint8_t *engine = track_engine(track_sel);
    *engine = (uint8_t)(SYNTH_IS_DRUM(track_sel) ? grav_drum_step(*engine, e) : grav_engine_step(*engine, e));
    set_dirty = 1;
    preset_id[track_sel] = PRESET_CUSTOM;
    ui.preset_edited &= (uint8_t)~(1u << track_sel);
    controls_auto(track_sel);
    ui.sound_page[track_sel] = 0;
    if (IS_SOUND_PAGE(active_page))
        sound_activate(0);
    ui.splash_until = plat_ms() + 1500u;
    app_say("ALGO", grav_engine_name(*engine));
}

/* an encoder turned: K1..K3 by the page (a held step's first), K4 the pattern, PRESET, ALGO, SELECT */
static void encoder_turn(uint32_t k, int32_t e)
{
    const int knob = k >= EN_K1 && k <= EN_K3;
    const uint32_t index = k - EN_K1;
    ui.turns[k] += e;
    ui.turned_at[k] = plat_ms();
    ui.redraw = 1;
    if (knob && ui.step_held >= 0)
        step_turn(index, e);
    else if ((knob || k == EN_K4) && active_page == PAGE_DEBUG)
        debug_turn(index, e);
    else if (knob && active_page == PAGE_SEQ)
        seq_turn(index, e);
    else if (k == EN_SELECT)
        select_turn(e);
    else if (knob && active_page == PAGE_MIXER)
        mixer_turn(index, e);
    else if (knob && active_page == PAGE_SCREEN) {
        if (index == 0u)                     /* (K2, K3: none) */
            theme_turn(e);
    } else if (knob && active_page == PAGE_GLO)
        glo_turn(index, e);
    else if (k == EN_K3 && active_page == PAGE_FX_MIX)
        quality_turn(e);
    else if (k == EN_K3 && active_page == PAGE_MOD)
        lfo_dest_turn(e);
    else if (knob && (active_page == PAGE_SOUND || active_page == PAGE_OUTPUT))
        sound_param_turn(index, e);
    else if (knob)
        page_knob_turn(index, e);
    else if (k == EN_K4)                     /* the pattern: the next (or the one queued) and on */
        pattern_pick((int32_t)(seq.next != SEQ_NO_PATTERN ? seq.next : seq.pattern) + e);
    else if (k == EN_PRESET)
        preset_turn(e);
    else if (k == EN_ALGO)
        algo_turn(e);
}

static void input(void)
{
    const uint32_t btn = plat_buttons();
    touch_poll();                            /* the sensed keys' presses, velocities and pressure */
    const uint32_t keys = touch_keys();
    if ((btn & 1u << B_SAVE) && !(ui.btn & 1u << B_SAVE)) {   /* (SAVE latched before the keys: a white key in the
                                                               * same frame is a slot) */
        ui.preset_down = 1;
        ui.preset_used = ui.preset_shown = 0;
        ui.preset_at = plat_ms();
    }
    keys_input(keys);
    for (uint32_t k = 0; k < NB; k++)
        if ((btn & 1u << k) && !(ui.btn & 1u << k))
            button_press(k);
    buttons_let_go(btn);
    for (uint32_t k = 0; k < NE; k++) {
        const int32_t e = plat_enc((int)k);
        if (e)
            encoder_turn(k, e);
    }
    if (btn != ui.btn || (keys != ui.keys && (ui.step_mode || ui.rec || ui.home_down || ui.preset_down)))
        ui.redraw = 1;                       /* (the keys are not drawn: a key redraws where it acts) */
    ui.keys = keys;
    ui.btn = btn;
    rec_midi();                              /* (MIDI in's notes, recording live) */
    if (ui.step_mode && active_page != PAGE_SEQ)   /* another page opened (LFO, ENV, FX, GLO, HOME + SELECT): the
                                                    * white keys play again; step mode is SEQ's page alone */
        step_mode_off();
    lights(btn, keys);
}

void app_input_only(void) { input(); }

static void cell(int32_t x, int32_t y, int32_t w, const char *s, int on)   /* a labelled box, lit when on */
{
    cv_rect(x, y, w, 18, on ? C_HI : C_LINE);
    cv_text(x + (w - text_w(&FONT_XS, s)) / 2, y + 2, &FONT_XS, s, on ? C_BLACK : C_GRAY);
}

/* A small dial strip: its own redraw while a value settles, with no audio-rate or full-screen animation. */
static const int8_t DIAL_POINT[49][2] = {
    {-90,90},
    {-98,81},
    {-106,71},
    {-112,60},
    {-117,49},
    {-122,37},
    {-125,25},
    {-126,12},
    {-127,0},
    {-126,-12},
    {-125,-25},
    {-122,-37},
    {-117,-49},
    {-112,-60},
    {-106,-71},
    {-98,-81},
    {-90,-90},
    {-81,-98},
    {-71,-106},
    {-60,-112},
    {-49,-117},
    {-37,-122},
    {-25,-125},
    {-12,-126},
    {0,-127},
    {12,-126},
    {25,-125},
    {37,-122},
    {49,-117},
    {60,-112},
    {71,-106},
    {81,-98},
    {90,-90},
    {98,-81},
    {106,-71},
    {112,-60},
    {117,-49},
    {122,-37},
    {125,-25},
    {126,-12},
    {127,0},
    {126,12},
    {125,25},
    {122,37},
    {117,49},
    {112,60},
    {106,71},
    {98,81},
    {90,90}
};
static int32_t param_frac(uint32_t i)   /* a page's knob's value, 0..25600 */
{
    int32_t v = param_value(active_page, i), max = 100;
    if (v < 0 || v == GRAV_AUTO) return 0;
    if (active_page == PAGE_SOUND) {
        grav_control_info_t info;
        if (grav_control_info(*track_engine(track_sel), ui.sound_page[track_sel] - 1u, i, &info)) max = info.max;
    } else if (active_page == PAGE_MOD && i == 2) { v = (int32_t)lfo_destination(mod_params[2]); max = 2; }
    else if (active_page == PAGE_FX_MIX && i == 2) { v = fx_quality; max = 3; }
    else if (active_page == PAGE_OUTPUT && SYNTH_IS_DRUM(track_sel) && i == 2) max = 1;
    return clamp(v, 0, max) * 25600 / max;
}

static const char *dial_hint(uint32_t i)
{
    if (active_page == PAGE_SOUND) {
        const uint32_t e = *track_engine(track_sel);
        grav_control_info_t info;
        if (!grav_control_info(e, ui.sound_page[track_sel] - 1u, i, &info)) return "";
        if (grav_controls_fm(e)) return "DX";
        if ((e == GRAV_ENGINE_WAVESHAPING && info.offset == 0) ||
            (e == GRAV_ENGINE_GRAIN && info.offset >= 3) ||
            (e == GRAV_ENGINE_BASS_DRUM && info.offset >= 3) || e == GRAV_ENGINE_SNARE_DRUM || e == GRAV_ENGINE_HI_HAT ||
            (e == GRAV_ENGINE_PHASE_DISTORTION && (info.offset == 1 || info.offset == 2))) return "AUX";
        if (e == GRAV_ENGINE_SPEECH) return "WORDS";
        if (e == GRAV_ENGINE_CHIPTUNE && (info.offset == 3 || info.offset == 4)) return "CLOCK";
        if (e == GRAV_ENGINE_PARTICLE && info.offset == 2) return "DIFFUSE";
        return info.kind == GC_BOOL || info.kind == GC_STEP ? "" : "%";
    }
    if ((active_page == PAGE_OUTPUT && !SYNTH_IS_DRUM(track_sel) && i == 0) ||
        (active_page == PAGE_OSC && SYNTH_IS_DRUM(track_sel) && i == 0)) return "st";
    if ((active_page == PAGE_OUTPUT && SYNTH_IS_DRUM(track_sel) && i == 2) ||
        (active_page == PAGE_MOD && i == 2) || (active_page == PAGE_FX_MIX && i == 2)) return "";
    if (active_page == PAGE_FX_SHAPE && i == 1) return "st";
    return "%";
}

/* a ring of radius r about (cx, cy), pixel by pixel (midpoint circle) */
static void dial_ring(int32_t cx, int32_t cy, int32_t r, uint16_t c)
{
    int32_t x = r, y = 0, err = 1 - r;
    while (x >= y) {
        cv_pset(cx + x, cy + y, c); cv_pset(cx - x, cy + y, c); cv_pset(cx + x, cy - y, c); cv_pset(cx - x, cy - y, c);
        cv_pset(cx + y, cy + x, c); cv_pset(cx - y, cy + x, c); cv_pset(cx + y, cy - x, c); cv_pset(cx - y, cy - x, c);
        y++;
        if (err < 0)
            err += 2 * y + 1;
        else {
            x--;
            err += 2 * (y - x) + 1;
        }
    }
}

/* A knob of the page's three (K1..K3): its name, its value as text, where its pointer goes (0..25600), whether it
 * starts from the middle, a mode under it when that says something (AUX, WORDS, DX). The same on every page: the
 * sound's, MOD, ENV, the effect's; SEQ's TEMPO, SWING, LENGTH (a step held: its NOTE, VELOCITY, LENGTH); GLO's. */
#define KNOB_AUTO (-1)                       /* a ring alone: AUTO, Plaits' own value */
#define KNOB_NONE (-3)                       /* nothing there (a page with fewer than three) */
typedef struct {
    const char *name;
    char value[16];
    int32_t frac;
    uint8_t centred;
    uint8_t inherit;                     /* (SEQ's SWING) the pattern's, not the track's own: its value grey */
    const char *hint;
} knob_t;

static void knob_get(uint32_t i, knob_t *k)
{
    k->hint = "";
    k->centred = 0;
    k->inherit = 0;
    k->value[0] = 0;
    if (active_page == PAGE_SEQ) {
        static const char *const HELD[3] = {"NOTE", "VELOCITY", "LENGTH"};
        if (ui.step_held >= 0) {
            const seq_step_t *st = &TRK.step[(uint32_t)ui.step_held];
            k->name = HELD[i];
            if (i == 0u) {
                chord_text(st, k->value);
                k->frac = (int32_t)st->note[0] * 25600 / 127;
            } else if (i == 1u) {
                int_text(st->vel, k->value);
                k->frac = ((int32_t)st->vel - 1) * 25600 / 126;
            } else {
                int_text(st->len, k->value);
                k->frac = ((int32_t)st->len - 1) * 25600 / ((int32_t)SEQ_STEPS - 1);
            }
        } else {
            const int32_t bpm = (int32_t)seq_bpm(&seq);
            k->name = PARAM_NAME[PAGE_SEQ][i];
            if (i == 0u) {
                int_text(bpm, k->value);
                k->frac = clamp(bpm - (int32_t)SEQ_BPM_MIN, 0, SEQ_BPM_MAX - SEQ_BPM_MIN) * 25600 /
                          (int32_t)(SEQ_BPM_MAX - SEQ_BPM_MIN);
            } else if (i == 1u) {            /* the selected track's (grey: the pattern's) */
                const int32_t sw = (int32_t)seq_track_swing(&seq.pat, track_sel);
                int_text(sw, k->value);
                text_cat(k->value, "%");
                k->inherit = !TRK.swing;
                k->frac = (sw - (int32_t)SEQ_SWING_MIN) * 25600 / (int32_t)(SEQ_SWING_MAX - SEQ_SWING_MIN);
            } else {
                int_text(TRK.length, k->value);
                k->frac = ((int32_t)TRK.length - 1) * 25600 / ((int32_t)SEQ_STEPS - 1);
            }
        }
        return;
    }
    if (active_page == PAGE_MIXER) {         /* the selected track's VOL (0.27) and SEND, the effect's RETURN */
        const int32_t v = i == 0u ? set.vol[track_sel] : i == 1u ? set.send[track_sel] : fx_params[FX_DRY_WET];
        k->name = PARAM_NAME[PAGE_MIXER][i];
        int_text(v, k->value);
        if (i)
            text_cat(k->value, "%");
        k->frac = v * 256;
        return;
    }
    if (active_page == PAGE_SCREEN) {        /* THEME alone */
        k->name = PARAM_NAME[PAGE_SCREEN][i];
        if (i) {
            k->frac = KNOB_NONE;
            return;
        }
        text_cat(k->value, PALETTES[theme_palette()].name);
        k->frac = (int32_t)theme_at() * 25600 / (int32_t)(THEMES - 1u);
        return;
    }
    if (active_page == PAGE_GLO) {
        k->name = PARAM_NAME[PAGE_GLO][i];
        if (i == 0u) {
            const uint32_t voices = SYNTH_IS_DRUM(track_sel) ? 1u : set.trk_voices[track_sel];
            text_cat(k->value, track_voices_text(track_sel));
            k->frac = (int32_t)voices * 25600 / (int32_t)SYNTH_POLY_VOICES;
        } else if (i == 1u) {
            text_cat(k->value, set.velocity_off ? "OFF" : "ON");
            k->frac = set.velocity_off ? 0 : 25600;
        } else {
            text_cat(k->value, METRO_NAME[set.metro]);
            k->frac = set.metro == METRO_OFF ? 0 : set.metro == METRO_REC ? 12800 : 25600;
        }
        return;
    }
    const int32_t v = param_value(active_page, i);
    k->name = param_name(active_page, i);
    if (v < 0) {
        k->frac = KNOB_NONE;
        return;
    }
    param_text(active_page, i, v, k->value);
    k->centred = (uint8_t)param_centred(active_page, i);
    k->hint = dial_hint(i);
    if (k->hint[0] == '%' && !k->hint[1])    /* (a percentage goes without saying) */
        k->hint = "";
    k->frac = v == GRAV_AUTO ? KNOB_AUTO : param_frac(i);
}

static int32_t dial_target(uint32_t i)
{
    knob_t k;
    knob_get(i, &k);
    return k.frac >= 0 ? k.frac : 0;
}

static void dials_sync(void)
{
    const uint32_t id = 1u + ((uint32_t)active_page << 24) + ((uint32_t)track_sel << 16) +
                        ((uint32_t)ui.sound_page[track_sel] << 8) + *track_engine(track_sel);
    if (ui.dial_id == id) return;
    ui.dial_id = id;
    for (uint32_t i = 0; i < 3; i++) ui.dial_q8[i] = dial_target(i);
    ui.dial_at = plat_ms();
}

/* the knobs' row on this page: its top, its height (a knob redraws alone in an 80-wide tile of it) and the knobs'
 * radius. (SEQ's are small, in the bottom band: draw_chips) */
static int32_t knob_row(int32_t *h, int32_t *r)
{
    *h = active_page == PAGE_GLO || active_page == PAGE_SCREEN ? 84 : 96;
    *r = 17;
    return 46;
}

/* how much a knob grows while it is turned, in pixels: at once to the full, back over 200 ms once let be */
static uint32_t knob_since(uint32_t i)       /* since knob i was last turned, ms (never: a long while) */
{
    return ui.turned_at[EN_K1 + i] ? plat_ms() - ui.turned_at[EN_K1 + i] : 0xFFFFFFFFu;
}

static int32_t knob_grow(uint32_t i, int32_t full)
{
    const uint32_t t = knob_since(i);
    return t < 250u ? full : t < 450u ? full * (int32_t)(450u - t) / 200 : 0;
}

/* a knob's face at (x, y), its ring rr: the scale's nine ticks from rr + t0 to rr + t1 (lighter up to the value; a
 * centred one's middle tick longer), the ring, the pointer where the value has eased to (two pixels wide), the centre */
static void dial_face(int32_t x, int32_t y, int32_t rr, const knob_t *k, uint32_t i, int hot, int32_t t0, int32_t t1)
{
    const int set_v = k->frac >= 0;
    const uint32_t progress = (uint32_t)clamp(ui.dial_q8[i], 0, 25600) * 48u;
    const uint32_t at = progress / 25600u, fraction = progress % 25600u;
    for (uint32_t j = 0; j <= 48; j += 6) {   /* the scale: lighter up to the value (from the middle if centred) */
        const int lit = set_v && (k->centred ? (j >= (at < 24 ? at : 24) && j <= (at < 24 ? 24 : at)) : j <= at);
        const int32_t r0 = rr + t0, r1 = k->centred && j == 24 ? rr + t1 + 2 : rr + t1;
        cv_line(x + DIAL_POINT[j][0] * r0 / 127, y + DIAL_POINT[j][1] * r0 / 127,
                x + DIAL_POINT[j][0] * r1 / 127, y + DIAL_POINT[j][1] * r1 / 127, lit ? C_GRAY : C_LINE);
    }
    dial_ring(x, y, rr, hot ? C_GRAY : C_DIM);
    if (set_v) {                         /* the pointer, where the value has eased to; two pixels wide */
        const uint32_t next = at < 48 ? at + 1 : at;
        const int32_t dx = (DIAL_POINT[at][0] * (int32_t)(25600u - fraction) +
                            DIAL_POINT[next][0] * (int32_t)fraction) / 25600;
        const int32_t dy = (DIAL_POINT[at][1] * (int32_t)(25600u - fraction) +
                            DIAL_POINT[next][1] * (int32_t)fraction) / 25600;
        const uint16_t c = hot ? C_HI : C_WHITE;
        const int32_t ox = dy > dx ? (dy > -dx ? 1 : 0) : (dy < -dx ? 1 : 0), oy = 1 - ox;
        cv_line(x + dx * 3 / 127, y + dy * 3 / 127, x + dx * (rr - 3) / 127, y + dy * (rr - 3) / 127, c);
        cv_line(x + ox + dx * 3 / 127, y + oy + dy * 3 / 127, x + ox + dx * (rr - 3) / 127,
                y + oy + dy * (rr - 3) / 127, c);
    }
    cv_rect(x - 1, y - 1, 2, 2, C_DIM);
}

/* K1..K3 as knobs, in the manner of the Octatrack's: a thin ring, nine ticks over its 270 degrees (lighter up to
 * the value; a centred one's middle tick longer), a pointer easing to the value; the name above, the value under,
 * a mode under that when it says something. Grey and white; the knob being turned grows a little and takes the
 * theme's colour, then settles back. Each in an 80-wide tile of the row: animate_dials redraws one a frame. */
static void draw_dials(int32_t y0, uint32_t mask, int32_t xoffset)
{
    int32_t h, r;
    knob_row(&h, &r);
    const int32_t full = 3, cy0 = 17 + r + 5 + full;   /* (room above for a grown knob's ticks) */
    (void)h;
    dials_sync();
    ui.dial_hot &= (uint8_t)~mask;
    ui.dial_dirty &= (uint8_t)~mask;
    for (uint32_t i = 0; i < 3u; i++) {
        if (!(mask & 1u << i))
            continue;
        knob_t k;
        knob_get(i, &k);
        if (k.frac == KNOB_NONE)
            continue;
        const int32_t x = 40 + (int32_t)i * 80 - xoffset, y = y0 + cy0;
        const int hot = knob_since(i) < 450u;
        ui.dial_hot |= (uint8_t)(hot << i);
        cv_text(x - text_w(&FONT_XS, k.name) / 2, y0, &FONT_XS, k.name, hot ? C_HI : C_GRAY);
        dial_face(x, y, r + knob_grow(i, full), &k, i, hot, 3, 5);
        const felucca_font_t *font = text_w(&FONT_S, k.value) <= 70 ? &FONT_S : &FONT_XS;
        cv_text(x - text_w(font, k.value) / 2, y + r + 6, font, k.value, k.frac >= 0 ? C_WHITE : C_GRAY);
        if (k.hint[0])
            cv_text(x - text_w(&FONT_T, k.hint) / 2, y + r + 24, &FONT_T, k.hint, C_DIM);
    }
}

/* SEQ's knobs (TEMPO, SWING, LENGTH; a step held: its NOTE, VELOCITY, LENGTH), small, in the bottom band's first
 * CHIP_H rows: each in an 80-wide tile, its knob at the left, its name and value beside it. They grow as they turn,
 * as the others do. */
#define CHIP_Y 2                             /* (in the bottom band) */
#define CHIP_H 38
static void draw_chips(int32_t y0, uint32_t mask, int32_t xoffset)
{
    dials_sync();
    ui.dial_hot &= (uint8_t)~mask;
    ui.dial_dirty &= (uint8_t)~mask;
    for (uint32_t i = 0; i < 3u; i++) {
        if (!(mask & 1u << i))
            continue;
        knob_t k;
        knob_get(i, &k);
        const int32_t x = 18 + (int32_t)i * 80 - xoffset, y = y0 + 19;
        const int hot = knob_since(i) < 450u;
        ui.dial_hot |= (uint8_t)(hot << i);
        dial_face(x, y, 10 + knob_grow(i, 2), &k, i, hot, 2, 4);
        cv_text(x + 17, y0 + 2, &FONT_T, k.name, hot ? C_HI : C_GRAY);
        const felucca_font_t *font = text_w(&FONT_S, k.value) <= 46 ? &FONT_S : &FONT_XS;
        cv_text(x + 17, y0 + 16, font, k.value, k.inherit ? C_GRAY : C_WHITE);
    }
}

static void draw_knobs(void)
{
    char title[24], n[8];
    int32_t h, r;
    title[0] = 0;
    text_cat(title, page_name(active_page));
    if (IS_SOUND_PAGE(active_page)) {        /* the page, its number of how many; the engine's icon, alive */
        char total[8];
        int_text(ui.sound_page[track_sel] + 1u, n);
        text_cat(n, "/");
        int_text(sound_pages(), total);
        text_cat(n, total);
        cv_text(230 - text_w(&FONT_XS, n), 24, &FONT_XS, n, C_WHITE);
        live_area(158, 22, 38, 20);
    }
    cv_text(8, 24, &FONT_XS, title, C_HI);
    draw_dials(knob_row(&h, &r), 7u, 0);
}

/* an animation's frame: ~30 a second, 10 while the sound takes 75 % or more of its time */
static uint32_t frame_ms(void)
{
    return plat_cpu_pct() >= 75u ? 96u : 30u;
}

/* a list over the page (the tracks, the patterns, the preset slots, the presets' browser): what is under it stays */
static int list_open(void)
{
    return ui.tracks_until || ui.patterns_until || ui.home_shown || ui.preset_shown || pb.open;
}

static int animate_dials(uint32_t now)
{
    if (!(IS_SOUND_PAGE(active_page) || active_page == PAGE_MOD || active_page == PAGE_ENV || IS_FX_PAGE(active_page) ||
          active_page == PAGE_SEQ || active_page == PAGE_GLO || active_page == PAGE_SCREEN || active_page == PAGE_MIXER) ||
        ui.splash_until || list_open()) return 0;
    const uint32_t dt = now - ui.dial_at;
    if (dt < frame_ms()) return 0;
    uint32_t hot = 0;
    for (uint32_t i = 0; i < 3; i++) {
        hot |= (knob_since(i) < 450u) << i;
        if (knob_since(i) < 470u)
            ui.dial_dirty |= (uint8_t)(1u << i);   /* (growing, or settling back) */
    }
    ui.dial_dirty |= (uint8_t)(hot ^ ui.dial_hot);
    for (uint32_t i = 0; i < 3; i++) {
        const int32_t target = dial_target(i), delta = target - ui.dial_q8[i];
        if (delta > 12 || delta < -12) {
            ui.dial_q8[i] += delta * (int32_t)(dt < 60u ? dt : 60u) / 80;
            ui.dial_dirty |= (uint8_t)(1u << i);
        } else ui.dial_q8[i] = target;
    }
    ui.dial_at = now;
    if (active_page == PAGE_SCREEN)          /* (THEME alone: K2's and K3's tiles are the palettes' swatches) */
        ui.dial_dirty &= 1u;
    if (!ui.dial_dirty) return 0;
    /* One 80-wide tile of the row per frame (~2.5 ms SPI), even when a preset changes all three values at once. */
    uint32_t dial = ui.dial_next;
    for (uint32_t n = 0; n < 3; n++, dial = (dial + 1u) % 3u)
        if (ui.dial_dirty & 1u << dial) break;
    ui.dial_next = (uint8_t)((dial + 1u) % 3u);
    if (active_page == PAGE_SEQ && !ui.step_mode)   /* (HOME's terricons: no knobs drawn) */
        return 0;
    if (active_page == PAGE_SEQ || active_page == PAGE_MIXER) {   /* (SEQ's and the mixer's: small, below) */
        cv_begin(80, CHIP_H, C_BLACK);
        draw_chips(0, 1u << dial, (int32_t)dial * 80);
        cv_blit(dial * 80, 180u + CHIP_Y);
    } else {
        int32_t h, r;
        const int32_t top = knob_row(&h, &r);
        cv_begin(80, (uint32_t)h, C_BLACK);
        draw_dials(0, 1u << dial, (int32_t)dial * 80);
        cv_blit(dial * 80, 36u + (uint32_t)top);
    }
    ui.dial_frames++;
    return 1;
}

/* SEQ's page (HOME), in two views:
 *   HOME (the page as it opens): the song at a glance, a ridge of eight terricons (0.28; 0.27: concentric rings), a
 *   track each, S1 at the left and D4 at the right, each in a colour of its own, moving with its sound
 *   (draw_terricons). The bottom band names the track, its sound and engine.
 *   SEQ (step mode: the white keys the steps, the black ones the tracks): every track's 16 steps of the page shown,
 *   a row each, S1..S4 then D1..D4, named at the left; a step on lit (the selected track's in the theme's colour,
 *   brighter the harder), each track's own length and step playing, a note flashing white as it begins; under them
 *   the pattern's four pages, the one shown lit, the place playing travelling across them.
 * TEMPO, SWING and LENGTH are K1..K3 on both. While anything moves, the page redraws alone, ~30 times a second (10
 * while the sound is busy); still, nothing does. */
#define STRIP_Y 22                           /* the strip redrawn as it plays: from here to the page's end */
#define GRID_Y 23                            /* SEQ: the tracks' rows, 13 apart (the drums' 3 lower), to 127 */
#define POS_Y 132                            /* the pages */

static int32_t step_x(uint32_t i) { return 24 + (int32_t)i * 13 + (int32_t)(i / 4u) * 2; }   /* 12 wide (16: to 237) */

static uint16_t mix565(uint16_t a, uint16_t b, int32_t t)   /* a to b, t 0..256 */
{
    const int32_t ar = a >> 11, ag = (a >> 5) & 63, ab = a & 31, br = b >> 11, bg = (b >> 5) & 63, bb = b & 31;
    return (uint16_t)(((ar + (br - ar) * t / 256) << 11) | ((ag + (bg - ag) * t / 256) << 5) | (ab + (bb - ab) * t / 256));
}

static int32_t flash256(uint32_t since, uint32_t ms)   /* a flash's strength: 256 as it begins, 0 ms later */
{
    return since < ms ? 256 - (int32_t)(since * 256u / ms) : 0;
}

static int32_t step_frac(uint32_t now)       /* how far through the step playing, 0..256 */
{
    const uint32_t bpm = seq_bpm(&seq), ms = 15000u / (bpm ? bpm : 120u);
    return clamp((int32_t)((now - ui.step_at) * 256u / ms), 0, 256);
}

/* the pattern's four pages under either view: the one shown lit; where the selected track plays, travelling */
static void draw_pages(int32_t dy, uint32_t page, int32_t playing, int32_t frac)
{
    for (uint32_t p = 0; p < SEQ_PAGES; p++) {
        const int32_t x = step_x(p * 4u), w = step_x(p * 4u + 3u) + 12 - x;
        cv_rect(x, POS_Y + dy, w, 3, p * SEQ_PAGE >= TRK.length ? RGB(24, 24, 30) : p == page ? C_HI : C_DIM);
    }
    if (playing >= 0) {
        const int32_t x = step_x(0) + (playing * 256 + frac) * (step_x(15) + 12 - step_x(0)) / (int32_t)(SEQ_STEPS * 256u);
        cv_rect(x - 1, POS_Y + dy - 2, 3, 7, C_WHITE);
    }
}

/* HOME: the terricons (0.28; 0.27's rings before). A ridge of eight, S1 at the left to D4 at the right, on one
 * ground line; each its height and place, the overlapping ones drawn from the tallest (furthest) to the lowest */
#define TC_BASE 136                          /* the ground line */
#define TC_HALF 36                           /* half a terricon's foot */
#define TC_N 22                              /* points up each slope (2 TC_N + 1 along it) */
static const uint8_t TC_X[SEQ_TRACKS] = {26, 56, 86, 116, 146, 176, 204, 228};   /* its peak's x */
static const uint8_t TC_H[SEQ_TRACKS] = {96, 78, 88, 70, 58, 64, 48, 52};        /* its height */
static const uint8_t TC_ORDER[SEQ_TRACKS] = {0, 2, 1, 3, 5, 4, 7, 6};            /* tallest first */
static const uint16_t TRACK_HUE[SEQ_TRACKS] = {   /* each track's colour, left to right: rose to violet */
    RGB(255, 61, 127), RGB(255, 106, 61), RGB(255, 166, 43), RGB(255, 225, 74),
    RGB(75, 227, 122), RGB(30, 196, 170), RGB(61, 155, 255), RGB(155, 107, 255)};

/* how a terricon moves, by its engine: up to three waves along its slopes (how many half waves from foot to foot,
 * their share, how fast they travel, in radians a second), how far they bend at full level (px, the tallest's) and
 * how fast its level falls (ms) */
enum { LOOK_KICK, LOOK_SNARE, LOOK_HAT, LOOK_BASS, LOOK_LEAD, LOOK_PAD, LOOK_PLUCK, LOOK_TEXTURE };
static const struct { uint8_t k[3]; float w[3], speed[3], depth, release; } TRACK_LOOK[] = {
    {{3, 5, 0}, {1.0f, 0.5f, 0.0f}, {4.0f, 7.0f, 0.0f}, 6.0f, 140.0f},      /* a kick: a strong bulge, quick */
    {{7, 11, 0}, {0.8f, 0.6f, 0.0f}, {6.0f, 9.0f, 0.0f}, 4.5f, 160.0f},     /* a snare */
    {{13, 19, 23}, {0.6f, 0.5f, 0.4f}, {9.0f, 13.0f, 17.0f}, 3.0f, 90.0f},  /* a hi-hat: small quick teeth */
    {{2, 3, 0}, {1.0f, 0.6f, 0.0f}, {1.5f, 2.0f, 0.0f}, 5.0f, 260.0f},      /* a bass: big, smooth */
    {{3, 5, 0}, {0.8f, 0.5f, 0.0f}, {2.0f, 3.0f, 0.0f}, 4.0f, 200.0f},      /* a lead */
    {{2, 3, 0}, {0.8f, 0.5f, 0.0f}, {0.4f, 0.6f, 0.0f}, 4.0f, 500.0f},      /* a pad: slow waves */
    {{4, 6, 0}, {0.7f, 0.5f, 0.0f}, {3.0f, 4.0f, 0.0f}, 4.0f, 180.0f},      /* a pluck */
    {{9, 14, 0}, {0.6f, 0.5f, 0.0f}, {5.0f, 8.0f, 0.0f}, 3.0f, 130.0f}};    /* noise, grains, words */

static uint32_t track_look(uint32_t engine)
{
    switch (engine) {
    case GRAV_ENGINE_BASS_DRUM: case GRAV_ENGINE_SYN_KICK: return LOOK_KICK;
    case GRAV_ENGINE_SNARE_DRUM: case GRAV_ENGINE_SYN_SNARE: return LOOK_SNARE;
    case GRAV_ENGINE_HI_HAT: case GRAV_ENGINE_RING_HAT: return LOOK_HAT;
    case GRAV_ENGINE_VA_VCF: case GRAV_ENGINE_FM: case GRAV_ENGINE_VIRTUAL_ANALOG: return LOOK_BASS;
    case GRAV_ENGINE_CHORDS: case GRAV_ENGINE_STRING_MACHINE: case GRAV_ENGINE_SWARM: case GRAV_ENGINE_ADDITIVE:
    case GRAV_ENGINE_WAVE_TERRAIN: return LOOK_PAD;
    case GRAV_ENGINE_STRING: case GRAV_ENGINE_MODAL: case GRAV_ENGINE_PARTICLE: return LOOK_PLUCK;
    case GRAV_ENGINE_NOISE: case GRAV_ENGINE_GRAIN: case GRAV_ENGINE_SPEECH: return LOOK_TEXTURE;
    default: return LOOK_LEAD;
    }
}

static int track_used(uint32_t t)            /* a step on in its pattern: it has a terricon */
{
    const seq_track_t *tr = &seq.pat.track[t];
    for (uint32_t i = 0; i < tr->length && i < SEQ_STEPS; i++)
        if (tr->step[i].on)
            return 1;
    return 0;
}

/* each track's level: the render's measure of it (grav_track_peak, every block), up at once, falling back as its
 * look says; a silent track's none. 1 while any moves (or a drum's hit still swells) */
static int levels_update(uint32_t now)
{
    const uint32_t dt = now - ui.level_at;
    int moving = 0;
    ui.level_at = now;
    for (uint32_t t = 0; t < SEQ_TRACKS; t++) {
        const float p = grav_track_peak[t], v = p > 0.0f ? p / (p + 0.12f) : 0.0f;
        float e = ui.level[t] * fm_expf(-(float)dt / TRACK_LOOK[track_look(*track_engine(t))].release);
        if (v > e)
            e = v;
        if (seq.mute >> t & 1u || e < 0.004f)
            e = 0.0f;
        moving |= e != ui.level[t] || e > 0.0f || now - ui.hit_at[t] < 200u;
        ui.level[t] = e;
        ui.level_heard |= (uint8_t)((e > 0.02f) << t);
    }
    return moving;
}

/* a line through n points, w pixels thick: cv_line's pixels along it, a w x w square on each, a run of them along a
 * row or a column filled as one rectangle (the pixels of w x w one-pixel lines, for a fraction of the work) */
static void thick_line(const int16_t *x, const int16_t *y, uint32_t n, uint16_t c, int32_t w)
{
    const int32_t o = (w - 1) / 2;
    for (uint32_t i = 0; i + 1u < n; i++) {
        int32_t x0 = x[i], y0 = y[i];
        const int32_t x1 = x[i + 1u], y1 = y[i + 1u];
        const int32_t dx = x1 > x0 ? x1 - x0 : x0 - x1, sx = x0 < x1 ? 1 : -1;
        const int32_t dy = y1 > y0 ? y0 - y1 : y1 - y0, sy = y0 < y1 ? 1 : -1;
        int32_t err = dx + dy, lx = x0, hx = x0, ly = y0, hy = y0;   /* the run so far: lx..hx, ly..hy */
        while (x0 != x1 || y0 != y1) {       /* (cv_line's steps) */
            const int32_t e2 = 2 * err;
            if (e2 >= dy) {
                err += dy;
                x0 += sx;
            }
            if (e2 <= dx) {
                err += dx;
                y0 += sy;
            }
            if ((y0 == ly && ly == hy) || (x0 == lx && lx == hx)) {   /* along the run's row or column */
                lx = x0 < lx ? x0 : lx;
                hx = x0 > hx ? x0 : hx;
                ly = y0 < ly ? y0 : ly;
                hy = y0 > hy ? y0 : hy;
            } else {
                cv_rect(lx - o, ly - o, hx - lx + w, hy - ly + w, c);
                lx = hx = x0;
                ly = hy = y0;
            }
        }
        cv_rect(lx - o, ly - o, hx - lx + w, hy - ly + w, c);
    }
}

/* the ground under a line through n points left to right, down to the base: black (what is behind it hidden) */
static void ground_under(const int16_t *x, const int16_t *y, uint32_t n, int32_t base)
{
    for (uint32_t i = 0; i + 1u < n; i++)
        for (int32_t c = x[i]; c < x[i + 1u]; c++) {
            const int32_t yy = y[i] + (y[i + 1u] - y[i]) * (c - x[i]) / (x[i + 1u] - x[i]);
            cv_rect(c, yy, 1, base - yy, C_BLACK);
        }
}

/* HOME: the terricons. A track with no step on in its pattern has none; one silent (muted, or another soloed) is
 * grey and still; one quiet is its colour, dim and still; one sounding lives: its slopes bend with its sound (its
 * level, followed), in its engine's way (a kick a strong, quick bulge, and its peak jumps as it hits; a hi-hat small,
 * quick teeth; a pad slow waves). The selected track's is thicker, white at its core, a marker and its name over
 * its peak; its level at the top right */
static void draw_terricons(int32_t dy, uint32_t now)
{
    const float time = (float)(now % 600000u) * 0.001f;
    const int32_t base = TC_BASE + dy;
    int16_t px[2 * TC_N + 1], py[2 * TC_N + 1];
    for (uint32_t o = 0; o < SEQ_TRACKS; o++) {
        const uint32_t t = TC_ORDER[o];
        if (!track_used(t))                  /* empty: no terricon */
            continue;
        const int muted = seq.mute >> t & 1u, sel = t == track_sel;
        const uint32_t look = track_look(*track_engine(t)), since = now - ui.hit_at[t];
        const int drum_hit = !muted && look <= LOOK_HAT && since < 400u;
        float amp = muted ? 0.0f : ui.level[t];
        if (drum_hit)                        /* a drum's hit: a swell as it begins, its peak up a moment */
            amp += 0.6f * fm_expf(-(float)since / 90.0f);
        if (amp > 1.2f)
            amp = 1.2f;
        const float h = (float)TC_H[t] * (drum_hit ? 1.0f + 0.18f * fm_expf(-(float)since / 120.0f) : 1.0f);
        const float depth = 1.3f * TRACK_LOOK[look].depth * (0.55f + 0.45f * (float)TC_H[t] / 96.0f) * amp;
        const float wsum = TRACK_LOOK[look].w[0] + TRACK_LOOK[look].w[1] + TRACK_LOOK[look].w[2];
        for (uint32_t i = 0; i <= 2u * TC_N; i++) {
            const float u = (float)i / (float)TC_N - 1.0f, au = u < 0.0f ? -u : u;   /* -1 .. 1, foot to foot */
            float d = 0.0f;
            if (depth > 0.05f)
                for (uint32_t k = 0; k < 3u; k++)
                    if (TRACK_LOOK[look].k[k])
                        d += TRACK_LOOK[look].w[k] * fm_sinf((float)TRACK_LOOK[look].k[k] * 3.1415927f * (u + 1.0f) +
                                                             TRACK_LOOK[look].speed[k] * time + 1.3f * (float)t);
            const float bend = depth * d / wsum * 4.0f * au * (1.0f - au);   /* (none at the feet, nor the peak) */
            px[i] = (int16_t)(TC_X[t] + (int32_t)((float)TC_HALF * u + (u < 0.0f ? -0.5f : 0.5f)));
            py[i] = (int16_t)((float)base - h * (1.0f - au) * (1.0f - 0.12f * u * u) - bend + 0.5f);
        }
        ground_under(px, py, 2u * TC_N + 1u, base);
        if (muted)                           /* silent: grey, still */
            thick_line(px, py, 2u * TC_N + 1u, C_DIM, sel ? 2 : 1);
        else if (amp < 0.02f)                /* quiet: its colour, dim */
            thick_line(px, py, 2u * TC_N + 1u, mix565(C_BLACK, TRACK_HUE[t], sel ? 200 : 120), sel ? 2 : 1);
        else {                               /* sounding: a glow under, brighter the louder; its colour on it */
            thick_line(px, py, 2u * TC_N + 1u, mix565(C_BLACK, TRACK_HUE[t], 40 + (int32_t)(amp * 90.0f)), sel ? 5 : 4);
            thick_line(px, py, 2u * TC_N + 1u, TRACK_HUE[t], 2);
        }
        if (sel) {                           /* the selected track's: a white core; over its peak a marker, its name */
            const int32_t tx = px[TC_N], ty = py[TC_N];
            if (!muted)
                thick_line(px, py, 2u * TC_N + 1u, C_WHITE, 1);
            for (int32_t k = 0; k < 6; k++)
                cv_rect(tx - k, ty - 5 - k, 2 * k + 1, 1, C_WHITE);
            cv_text(tx - text_w(&FONT_T, TRACK_NAME[t]) / 2, ty - 24, &FONT_T, TRACK_NAME[t], C_WHITE);
        }
    }
    cv_rect(0, base + 1, 240, 144, C_BLACK); /* the ground, and nothing under it (the feet's glow cut) */
    cv_rect(0, base, 240, 1, C_LINE);
    {                                        /* the selected track's level, top right */
        const int32_t lv = (int32_t)(ui.level[track_sel] * 6.0f + 0.5f);
        for (int32_t k = 0; k < 6; k++)
            cv_rect(198 + k * 6, 4 + dy, 4, 10, k < lv ? TRACK_HUE[track_sel] : C_LINE);
    }
}

/* SEQ (step mode): every track's steps of the page shown, a row each, in the track's colour (its terricon's, 0.28):
 * a step on brighter the harder; the selected track's row on a ground of its colour, its steps at their brightest */
static void draw_grid(int32_t dy, uint32_t now)
{
    const uint32_t page = ui.view_page;
    for (uint32_t t = 0; t < SEQ_TRACKS; t++) {
        const seq_track_t *tr = &seq.pat.track[t];
        const int32_t y = GRID_Y + dy + (int32_t)t * 13 + (t >= SEQ_S_TRACKS ? 3 : 0), sel = t == track_sel;
        const int32_t at = seq.playing ? seq.step[t] : -1, hit = flash256(now - ui.hit_at[t], 200u);
        const uint16_t hue = TRACK_HUE[t], sel_lo = mix565(C_BLACK, hue, 90);
        if (sel)                             /* the selected track: its row on a ground of its colour */
            cv_rect(0, y - 1, 240, 13, mix565(C_BLACK, hue, 40));
        const int muted = seq.mute >> t & 1u;   /* (0.26) silent (muted, or another soloed): its name red, steps dim */
        cv_text(3, y - 1, &FONT_T, TRACK_NAME[t], muted ? C_RED : sel ? C_WHITE : hue);
        for (uint32_t i = 0; i < SEQ_PAGE; i++) {
            const uint32_t s = page * SEQ_PAGE + i;
            const int32_t x = step_x(i);
            if (s >= tr->length) {           /* past its length: a dot */
                cv_rect(x + 5, y + 5, 2, 1, C_LINE);
                continue;
            }
            const seq_step_t *st = &tr->step[s];
            uint16_t c = i % 4u ? RGB(20, 20, 25) : RGB(28, 28, 34);
            if (st->on)
                c = muted ? C_DIM : mix565(C_BLACK, hue, (sel ? 150 : 90) + (int32_t)st->vel * (sel ? 106 : 120) / 127);
            if (at == (int32_t)s)            /* its step playing: lit; a note, flashing as it begins */
                c = st->on ? mix565(c, C_WHITE, 64 + hit * 192 / 256) : RGB(70, 70, 82);
            cv_rect(x, y, 12, 11, c);
            if (st->on && st->n > 1u)        /* a chord: a notch */
                cv_rect(x + 9, y, 3, 3, sel ? sel_lo : RGB(20, 20, 25));
            if (sel && ui.step_held == (int8_t)s) {   /* the step held: framed */
                cv_rect(x - 1, y - 1, 14, 1, C_WHITE);
                cv_rect(x - 1, y + 11, 14, 1, C_WHITE);
                cv_rect(x - 1, y, 1, 11, C_WHITE);
                cv_rect(x + 12, y, 1, 11, C_WHITE);
            }
            if (sel && ui.rec && !seq.playing && ui.rec_pos == s)
                cv_rect(x, y + 9, 12, 2, C_RED);
        }
    }
    draw_pages(dy, page, seq.playing ? seq.step[track_sel] : -1, seq.playing ? step_frac(now) : 0);
}

static void draw_seq_body(int32_t dy)        /* the strip (rows STRIP_Y on), drawn dy rows up or down */
{
    if (ui.step_mode)
        draw_grid(dy, plat_ms());
    else
        draw_terricons(dy, plat_ms());
}

static void draw_seq(void)
{
    draw_seq_body(0);
}

/* (0.26) the mixer (SEL), under the track's line: the eight tracks a strip each, S1..S4 then D1..D4: its name (the
 * selected one's in the theme's colour), its VOL as a fader (0.27; before, its sound's LEVEL), a meter of its sound beside it (falling back over
 * 0.6 s), its SEND as a bar (its share through the effect), M lit red when it is muted, S lit when it is soloed (a
 * silent track's strip dim). While it sounds, the strip under the track's line redraws alone, as SEQ's does */
static void draw_mixer_body(int32_t dy)
{
    const int32_t H = 64;
    for (uint32_t t = 0; t < SYNTH_TRACKS; t++) {
        const int32_t x = 8 + (int32_t)t * 28, top = 40 + dy, sel = t == track_sel;
        const int muted = seq.mute >> t & 1u, mute = set.mute >> t & 1u, solo = ui.solo >> t & 1u;   /* (silent:
                                                                     * muted, or another soloed) */
        const int32_t lv = (int32_t)set.vol[t] * H / 100, m = (int32_t)ui.meter[t] * H / 256;
        if (sel)
            cv_rect(x, 22 + dy, 26, 15, C_HI);
        cv_text(x + 13 - text_w(&FONT_XS, TRACK_NAME[t]) / 2, 22 + dy, &FONT_XS, TRACK_NAME[t],
                sel ? C_BLACK : muted ? C_DIM : C_WHITE);
        cv_rect(x + 5, top, 4, H, RGB(24, 24, 30));   /* the fader: its slot, filled to the level, its cap */
        cv_rect(x + 5, top + H - lv, 4, lv, muted ? C_DIM : sel ? C_HI : C_GRAY);
        cv_rect(x + 1, top + H - lv - 1, 12, 3, muted ? C_DIM : C_WHITE);
        cv_rect(x + 16, top, 6, H, RGB(18, 18, 23));   /* the meter, a segment every other row */
        for (int32_t r = 0; r < m; r += 2)
            cv_rect(x + 16, top + H - 1 - r, 6, 1, r >= H * 7 / 8 ? C_WHITE : r >= H / 2 ? C_HI : C_AMB);
        cv_rect(x + 1, 110 + dy, 22, 4, RGB(24, 24, 30));   /* the send */
        cv_rect(x + 1, 110 + dy, (int32_t)set.send[t] * 22 / 100, 4, muted ? C_DIM : sel ? C_HI : C_GRAY);
        for (uint32_t b = 0; b < 2u; b++) {  /* M: muted, lit red; S (0.26.1): soloed, lit in the theme's colour */
            const int on = b ? solo : mute;
            const int32_t bx = x + 1 + (int32_t)b * 12;
            const char *l = b ? "S" : "M";
            if (on)
                cv_rect(bx, 120 + dy, 11, 14, b ? C_HI : C_RED);
            else {
                cv_rect(bx, 120 + dy, 11, 14, C_LINE);
                cv_rect(bx + 1, 121 + dy, 9, 12, C_BLACK);
            }
            cv_text(bx + 6 - text_w(&FONT_T, l) / 2, 121 + dy, &FONT_T, l, on ? C_BLACK : C_DIM);
        }
    }
}

static uint32_t meter_of(float peak)         /* a track's loudest sample (0..~1) as a meter's height, 0..256 */
{
    return peak > 0.0f ? (uint32_t)(peak / (peak + 0.15f) * 256.0f) : 0u;
}

/* HOME held, K4 turned: the 16 patterns for a moment, over the page: the one playing lit, the next one framed
 * (blinking), one with steps white, an empty one dim */
static void draw_patterns(void)
{
    const uint32_t used = patterns_used(), blink = (plat_ms() / 250u) & 1u;
    char t[8];
    cv_rect(4, 20, 232, 118, C_LINE);
    cv_rect(5, 21, 230, 116, C_BLACK);
    cv_text(12, 24, &FONT_XS, "PATTERN", C_GRAY);
    if (seq.next != SEQ_NO_PATTERN) {
        cv_text(150, 24, &FONT_XS, "NEXT", C_GRAY);
        int_text(seq.next + 1, t);
        cv_text(184, 24, &FONT_XS, t, C_HI);
    }
    for (uint32_t n = 0; n < BANK_PATTERNS; n++) {
        const int32_t x = 9 + (int32_t)(n % 4u) * 57, y = 42 + (int32_t)(n / 4u) * 23;
        const int cur = n == seq.pattern, nxt = n == seq.next;
        if (cur)
            cv_rect(x, y, 53, 20, C_HI);
        else if (nxt && blink)
            cv_rect(x, y, 53, 20, C_WHITE);
        else if (nxt) {
            cv_rect(x, y, 53, 20, C_WHITE);
            cv_rect(x + 1, y + 1, 51, 18, C_BLACK);
        } else
            cv_rect(x, y + 18, 53, 2, used & 1u << n ? C_LINE : C_BLACK);
        int_text((int32_t)n + 1, t);
        cv_text(x + 26 - text_w(&FONT_S, t) / 2, y + 2, &FONT_S, t,
                cur || (nxt && blink) ? C_BLACK : used & 1u << n || nxt ? C_WHITE : C_DIM);
    }
}

static void draw_preset_slots(void)
{
    char t[12];
    cv_rect(4, 20, 232, 123, C_LINE);
    cv_rect(5, 21, 230, 121, C_BLACK);
    cv_text(12, 24, &FONT_XS, "SAVE PRESET", C_HI);
    int_text(ui.preset_page + 1, t);
    text_cat(t, "/4");
    cv_text(195, 24, &FONT_XS, t, C_WHITE);
    for (uint32_t n = 0; n < 16u; n++) {
        const uint32_t slot = 16u * ui.preset_page + n;
        const int used = preset_used(&user_presets, slot), held = ui.key_preset >> WHITE_KEYS[n] & 1u;
        const int32_t x = 9 + (int32_t)(n % 4u) * 57, y = 41 + (int32_t)(n / 4u) * 21;
        cv_rect(x, y, 53, 20, held ? C_HI : C_LINE);
        if (!held) cv_rect(x + 1, y + 1, 51, 18, C_BLACK);
        int_text(slot + 1, t);
        cv_text(x + 6, y + 2, &FONT_S, t, held ? C_BLACK : used ? C_WHITE : C_DIM);
        if (used) cv_text(x + 39, y + 5, &FONT_XS, preset_user(&user_presets, slot)->drum ? "D" : "S",
                          held ? C_BLACK : C_HI);
    }
    cv_text(10, 128, &FONT_T, "OCT-/+ PAGE  KEY: SAVE/REPLACE", C_GRAY);
}

/* (0.27) the presets' browser, over the page: PRESETS and what it shows (ALL ENGINES, or the engine's); five rows, the
 * one playing framed: its engine's icon, its name, its kind; at the foot what OCT+ and OCT- do */
static void draw_preset_browser(void)
{
    char t[40];
    int32_t shown = 0, at = 0, top;
    cv_rect(4, 20, 232, 122, C_LINE);
    cv_rect(5, 21, 230, 120, C_BLACK);
    cv_text(12, 24, &FONT_XS, "PRESETS", C_HI);
    t[0] = 0;
    text_cat(t, pb.all ? "ALL ENGINES" : "ENGINE: ");
    if (!pb.all)
        text_cat(t, grav_engine_name(pb_engine(pb.sel)));
    cv_text(228 - text_w(&FONT_T, t), 26, &FONT_T, t, C_GRAY);
    for (int32_t i = 0; i < pb.n; i++)       /* the one playing's place among those shown */
        if (pb_shown(i)) {
            if (i == pb.sel)
                at = shown;
            shown++;
        }
    top = clamp(at - (int32_t)PB_ROWS / 2, 0, shown > (int32_t)PB_ROWS ? shown - (int32_t)PB_ROWS : 0);
    for (int32_t i = 0, k = 0; i < pb.n && k < top + (int32_t)PB_ROWS; i++) {
        if (!pb_shown(i))
            continue;
        if (k++ < top)
            continue;
        const int32_t y = 42 + (k - 1 - top) * 17, sel = i == pb.sel;
        const uint32_t e = pb_engine(i);
        const int id = pb_id(i);
        const char *name = preset_title(&user_presets, e, SYNTH_IS_DRUM(pb.track), id);
        icon_t ic;
        if (sel) {
            cv_rect(8, y - 1, 224, 17, C_HI);
            cv_rect(9, y, 222, 15, C_BLACK);
        }
        memset(&ic, 0, sizeof ic);           /* its engine's icon, small and still */
        ic.x = 12; ic.y = y + 2; ic.w = 18; ic.h = 11; ic.small = 1;
        ic.k[0] = ic.k[1] = ic.k[2] = 0.5f;
        icon_draw(e, &ic);
        cv_text(36, y - 1, &FONT_S, name ? name : "", sel ? C_WHITE : C_GRAY);
        const char *cat = PRESET_CATEGORY[preset_category(e, id, name)];
        cv_text(226 - text_w(&FONT_T, cat), y + 2, &FONT_T, cat, sel ? C_HI : C_DIM);
    }
    cv_text(12, 128, &FONT_T, "OCT+ ALL / ENGINE    OCT- CANCEL", C_DIM);
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
        cv_rect(142, y + 1, 20, 12, C_BLACK);   /* its icon, small (the list is where a track is picked) */
        icon_box(t, 143, y + 1, 18, 12, 1);
        cv_text(170, y + 1, &FONT_XS, track_voices_text(t), sel ? C_BLACK : C_GRAY);
    }
}

/* ALGO turned: the engine's splash for a moment, over the page: its icon, alive; its name; its line */
static void draw_splash(void)
{
    const uint32_t e = *track_engine(track_sel);
    const char *name = grav_engine_name(e);
    cv_rect(4, 20, 232, 118, C_LINE);
    cv_rect(5, 21, 230, 116, C_BLACK);
    live_area(50, 24, 140, 60);
    {                                        /* (0.27) its place in ALGO's order: 3 / 24 */
        char t[12], n[6];
        const int drum = SYNTH_IS_DRUM(track_sel);
        int_text((int32_t)grav_engine_rank(drum, e) + 1, t);
        text_cat(t, " / ");
        int_text(drum ? 6 : GRAV_ENGINE_COUNT, n);
        text_cat(t, n);
        cv_text(228 - text_w(&FONT_XS, t), 24, &FONT_XS, t, C_GRAY);
    }
    cv_text(120 - text_w(&FONT_M, name) / 2, 86, &FONT_M, name, C_HI);
    draw_wrapped(engine_desc(e), 10, 111, 220, 2u, 1, C_GRAY);
}

/* only the strip under the track's line (the steps moved, a flash, the cursor): not the whole screen */
static void draw_seq_grid(void)
{
    if (active_page == PAGE_SEQ && !ui.step_mode) {   /* HOME's terricons: the whole page */
        cv_begin(240, 144, C_BLACK);
        draw_terricons(0, plat_ms());
        cv_blit(0, 36);
        return;
    }
    cv_begin(240, 144u - STRIP_Y, C_BLACK);
    if (active_page == PAGE_MIXER)
        draw_mixer_body(-STRIP_Y);
    else
        draw_seq_body(-STRIP_Y);
    cv_blit(0, 36 + STRIP_Y);
}

/* GLO: VOICES, VELOCITY and METRONOME as the other pages' knobs are drawn; the identity and the version below */
static void draw_glo(void)
{
    int32_t h, r, x = 8;
    uint32_t used = 0;
    char t[8];
    draw_dials(knob_row(&h, &r), 7u, 0);
    for (uint32_t k = 0; k < SYNTH_S_TRACKS; k++) {   /* (0.27.1) the synth tracks' four voices: each's, the free */
        cv_text(x, 26, &FONT_T, TRACK_NAME[k], k == track_sel ? C_HI : C_GRAY);
        x += text_w(&FONT_T, TRACK_NAME[k]) + 3;
        int_text(set.trk_voices[k], t);
        cv_text(x, 26, &FONT_T, set.trk_voices[k] ? t : "OFF", set.trk_voices[k] ? C_WHITE : C_RED);
        x += text_w(&FONT_T, set.trk_voices[k] ? t : "OFF") + 10;
        used += set.trk_voices[k];
    }
    int_text((int32_t)(SYNTH_POLY_VOICES - used), t);
    cv_text(x, 26, &FONT_T, "FREE", C_GRAY);
    cv_text(x + text_w(&FONT_T, "FREE") + 3, 26, &FONT_T, t, C_WHITE);
    cv_text(8, 130, &FONT_XS, "ID", C_GRAY);
    cv_text(26, 130, &FONT_XS, FELUCCA_ID, C_WHITE);
    cv_text(150, 130, &FONT_XS, "VER", C_GRAY);   /* (K3 was the version until 0.24: METRONOME now) */
    cv_text(176, 130, &FONT_XS, APP_VERSION, C_WHITE);
}

/* GLO's SCREEN: THEME as a knob; beside it every palette in the knob's order, five a row: a swatch of its two
 * colours and its name, the one shown framed */
static void draw_screen_page(void)
{
    int32_t h, r;
    draw_dials(knob_row(&h, &r), 1u, 0);
    for (uint32_t i = 0; i < THEMES; i++) {
        const palette_t *pl = &PALETTES[THEME_ORDER[i]];
        const int32_t x = 96 + (int32_t)(i % 5u) * 28, y = 56 + (int32_t)(i / 5u) * 34;
        const int shown = THEME_ORDER[i] == theme_palette();
        if (shown)
            cv_rect(x - 2, y - 2, 28, 18, C_WHITE);
        cv_rect(x - 1, y - 1, 26, 16, C_BLACK);
        cv_rect(x, y, 24, 7, pl->c[3]);
        cv_rect(x, y + 7, 24, 7, pl->c[4]);
        cv_text(x + 12 - text_w(&FONT_T, pl->name) / 2, y + 17, &FONT_T, pl->name, shown ? C_WHITE : C_DIM);
    }
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
    cv_text(172, 24, &FONT_T, on ? (grav_dual_core ? "ON" : "IDLE") : "OFF", on ? C_HI : C_RED);
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

/* the bottom band's message: a moment's (app_say), the preset's state, an import failed; 0: none. Shorter if it
 * would be wider than w */
static int band_message(char *m, int32_t w)
{
    m[0] = 0;
    if (preset_import_failed) text_cat(m, "PRESET IMPORT: REBOOT TO RETRY");
    else if (ui.msg_until && plat_ms() < ui.msg_until) {
        text_cat(m, ui.msg[0]);
        if (ui.msg[1][0]) { text_cat(m, " "); text_cat(m, ui.msg[1]); }
        if (text_w(&FONT_T, m) > w) { m[0] = 0; text_cat(m, ui.msg[0]); }
    } else if (preset_dirty)
        text_cat(m, preset_write_req ? "PRESET PENDING: STOP / RELEASE" : "PRESET UNSAVED: TAP SAVE");
    return m[0] != 0;
}

static void draw_cpu(int32_t x, int32_t y)   /* the render's load; red once a render ran late (heard: a crackle) */
{
    char t[8];
    cv_text(x, y, &FONT_XS, "CPU", C_GRAY);
    int_text((int32_t)ui.cpu, t);
    text_cat(t, "%");
    cv_text(x + 26, y, &FONT_XS, t, ui.xruns ? C_RED : C_HI);
}

/* SEQ's bottom band: its three knobs, small; under them PLAY or STOP, REC and its step / STEP / a MIDI clock in,
 * the pattern and the next one (blinking), the preset unsaved, the load. A moment's message (OCTAVE, PATTERN, the
 * volume ...) over the left of that line */
static void draw_seq_band(void)
{
    char t[48];
    const int32_t y = 43;
    draw_chips(CHIP_Y, 7u, 0);
    if ((ui.msg_until && plat_ms() < ui.msg_until) || preset_import_failed) {
        band_message(t, 160);
        cv_text(8, y + 2, &FONT_T, t, preset_import_failed ? C_HI : C_GRAY);
    } else if (plat_ms() - ui.vol_at < 1300u && ui.vol_at) {
        cv_text(8, y, &FONT_XS, "VOL", C_GRAY);
        int_text((int32_t)(plat_master() * 100u / 4096u), t);
        text_cat(t, "%");
        cv_text(36, y, &FONT_XS, t, C_HI);
    } else {
        cv_text(8, y, &FONT_XS, seq.playing ? "PLAY" : "STOP", seq.playing ? C_HI : C_GRAY);
        if (ui.rec) {                        /* REC and its step; recording live: LIVE (and the metronome's state) */
            cv_text(48, y, &FONT_XS, "REC", C_RED);
            if (seq.playing && ui.shown_count) {   /* counting in: the beats to come */
                t[0] = 'I'; t[1] = 'N'; t[2] = ' ';
                int_text(ui.shown_count, t + 3);
                cv_text(74, y, &FONT_XS, t, C_HI);
            } else if (seq.playing)
                cv_text(74, y, &FONT_XS, ui.erasing ? "ERASE" : "LIVE", ui.erasing ? C_HI : C_WHITE);
            else {
                int_text(ui.rec_pos + 1, t);
                cv_text(74, y, &FONT_XS, t, C_WHITE);
            }
        } else if (ui.step_mode)
            cv_text(48, y, &FONT_XS, "STEP", C_HI);
        else if (seq.ext)
            cv_text(48, y, &FONT_XS, "CLOCK", C_WHITE);
        t[0] = 'P';                          /* the pattern; the next one, blinking */
        int_text(seq.pattern + 1, t + 1);
        cv_text(104, y, &FONT_XS, t, C_WHITE);
        if (seq.next != SEQ_NO_PATTERN && (plat_ms() / 250u) & 1u) {
            t[0] = '>';
            int_text(seq.next + 1, t + 1);
            cv_text(104 + text_w(&FONT_XS, "P16") + 2, y, &FONT_XS, t, C_HI);
        }
        if (preset_dirty)
            cv_text(144, y + 2, &FONT_T, preset_write_req ? "PENDING" : "UNSAVED", C_HI);
    }
    draw_cpu(178, y);
}

/* (0.27) HOME's bottom band: the selected track (TRK 03) and its engine, its state (MUTE, SOLO, FREEZE) and the load;
 * its sound's name, large (a * once it is changed). A moment's message (TEMPO 120 ...) or the count-in in its place */
static void draw_home_band(void)
{
    char t[48], n[24];
    const uint32_t tr = track_sel;
    t[0] = 0;
    text_cat(t, "TRK 0");
    int_text((int32_t)tr + 1, n);
    text_cat(t, n);
    cv_text(8, 6, &FONT_XS, t, C_GRAY);
    cv_text(60, 8, &FONT_T, grav_engine_name(*track_engine(tr)), C_DIM);
    const int silent = !SYNTH_IS_DRUM(tr) && !set.trk_voices[tr];   /* (0.27.1) VOICES OFF: it cannot sound */
    const char *state = silent ? "NO VOICE" : seq.mute >> tr & 1u ? "MUTE" : ui.solo >> tr & 1u ? "SOLO"
                      : fx_freeze ? "FREEZE" : "";
    cv_text(172 - text_w(&FONT_XS, state), 6, &FONT_XS, state,   /* (ending clear of CPU) */
            silent || seq.mute >> tr & 1u ? C_RED : C_HI);
    draw_cpu(178, 6);
    if (ui.shown_count) {                    /* counting in: the beats to come */
        t[0] = 'I'; t[1] = 'N'; t[2] = ' ';
        int_text(ui.shown_count, t + 3);
        cv_text(8, 26, &FONT_M, t, C_RED);
        return;
    }
    if (band_message(t, 220)) {              /* a moment's message: its first word small, the rest large */
        if (ui.msg_until && plat_ms() < ui.msg_until && ui.msg[1][0]) {
            cv_text(8, 26, &FONT_XS, ui.msg[0], C_GRAY);
            cv_text(8 + text_w(&FONT_XS, ui.msg[0]) + 6, 22, &FONT_M, ui.msg[1], C_HI);
        } else
            cv_text(8, 30, &FONT_S, t, C_HI);
        return;
    }
    n[0] = 0;                                /* its sound: the preset's name, * once changed */
    text_cat(n, preset_label(tr));
    if (preset_id[tr] == PRESET_CUSTOM && n[0] == 'C')   /* (none loaded: the engine's name) */
        n[0] = 0, text_cat(n, grav_engine_name(*track_engine(tr)));
    cv_text(8, 22, text_w(&FONT_M, n) <= 224 ? &FONT_M : &FONT_S, n, C_WHITE);
}

/* (0.26) the mixer's bottom band: its three knobs, small; under them how the keys act here, or a moment's message;
 * the load */
static void draw_mixer_band(void)
{
    char t[48];
    const int32_t y = 43;
    draw_chips(CHIP_Y, 7u, 0);
    if (band_message(t, 160))
        cv_text(8, y + 2, &FONT_T, t, preset_import_failed ? C_HI : C_GRAY);
    else
        cv_text(8, y + 2, &FONT_T, "WHITE 1-8 MUTE  BLACK 1-8 SOLO", C_GRAY);
    draw_cpu(178, y);
}

/* the page's state, in the bottom band's first line: a sound page's HOME; the effect's SEED and whether it is on */
static void draw_status(int32_t y)
{
    if (IS_SOUND_PAGE(active_page)) {
        cv_text(8, y + 2, &FONT_T, "HOME  NEXT PAGE", C_GRAY);
        cv_text(232 - text_w(&FONT_T, "HOLD HOME + SELECT  JUMP"), y + 2, &FONT_T, "HOLD HOME + SELECT  JUMP", C_DIM);
    } else if (IS_FX_PAGE(active_page)) {
        cv_text(8, y, &FONT_XS, "SEED", C_GRAY);
        cv_text(42, y, &FONT_XS, fx_seed_auto ? "AUTO" : "MANUAL", C_WHITE);
        cv_text(178, y, &FONT_XS, "FX", C_GRAY);
        cv_text(198, y, &FONT_XS, fx_wanted() ? "ON" : "OFF", fx_wanted() ? C_HI : C_WHITE);
    }
}

/* the header's battery: a rounded 26x14 body and its terminal; three levels (none invented between), a bolt on USB
 * power, empty when unknown */
static void draw_battery(int batt)
{
    const int32_t bx = 202, by = 9;
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
        cv_line(bx + 14, by + 2, bx + 10, by + 7, RGB(0, 255, 0));
        cv_line(bx + 10, by + 7, bx + 15, by + 7, RGB(0, 255, 0));
        cv_line(bx + 15, by + 7, bx + 11, by + 12, RGB(0, 255, 0));
    } else if (batt > 0 && batt <= 3)
        cv_rect(bx + 3, by + 3, batt * 7, 8, batt == 3 ? RGB(0, 255, 0) : batt == 2 ? RGB(255, 200, 0) : C_RED);
}

/* the header: the name; the song's tempo in the middle (the theme's colour: a MIDI clock in's, leading); the pattern
 * and PLAY (a triangle), STOP (a square) or REC (a red dot) (0.27); the battery */
static void draw_header(void)
{
    char t[4];
    cv_begin(240, 32, C_BLACK);
    cv_text(6, 6, &FONT_B, "GRAV CODE", C_WHITE);
    {
        const int32_t w = text_w(&FONT_B, ui.bpm_text) + 3 + text_w(&FONT_T, "BPM"), x = 120 - w / 2;
        cv_text(x, 6, &FONT_B, ui.bpm_text, ui.shown_ext ? C_HI : C_WHITE);
        cv_text(x + w - text_w(&FONT_T, "BPM"), 10, &FONT_T, "BPM", C_GRAY);
    }
    t[0] = 'P';
    t[1] = (char)('0' + (seq.pattern + 1u) / 10u);
    t[2] = (char)('0' + (seq.pattern + 1u) % 10u);
    t[3] = 0;
    cv_text(150, 8, &FONT_XS, t, C_WHITE);
    if (ui.rec) {
        for (int32_t r = 0; r < 9; r++) {
            const int32_t h = r < 2 || r > 6 ? (r == 0 || r == 8 ? 3 : 7) : 9;
            cv_rect(180 + r, 15 - h / 2, 1, h, C_RED);
        }
    } else if (seq.playing) {
        for (int32_t k = 0; k < 9; k++)
            cv_rect(180 + k, 11 + k / 2, 1, 9 - (k / 2) * 2, C_HI);
    } else
        cv_rect(180, 11, 8, 8, C_GRAY);
    draw_battery(plat_battery());
    cv_blit(0, 0);
}

/* the page: the track and its engine above it (HOME's terricons name it in the bottom band), FREEZE and MUTE or SOLO on
 * every page; then the page's own, and a splash or a list over it */
static void draw_page(void)
{
    cv_begin(240, 144, C_BLACK);
    ui.live_w = 0;                           /* (none, unless a page draws one) */
    if (!(active_page == PAGE_SEQ && !ui.step_mode)) {
        cv_text(8, 4, &FONT_S, TRACK_NAME[track_sel], C_WHITE);
        cv_text(30, 4, &FONT_S, grav_engine_name(*track_engine(track_sel)), C_HI);
        if (fx_freeze)
            cell(172, 2, 60, "FREEZE", 1);
        if (seq.mute >> track_sel & 1u)      /* (0.26: muted, or another soloed; 0.26.1: SOLO) */
            cv_text(fx_freeze ? 132 : 200, 6, &FONT_XS, "MUTE", C_RED);
        else if (ui.solo >> track_sel & 1u)
            cv_text(fx_freeze ? 132 : 200, 6, &FONT_XS, "SOLO", C_HI);
    }
    if (active_page == PAGE_GLO)
        draw_glo();
    else if (active_page == PAGE_SCREEN)
        draw_screen_page();
    else if (active_page == PAGE_DEBUG)
        draw_debug();
    else if (active_page == PAGE_SEQ)
        draw_seq();
    else if (active_page == PAGE_MIXER)
        draw_mixer_body(0);
    else
        draw_knobs();
    if (ui.splash_until)
        draw_splash();
    if (list_open())
        ui.live_w = 0;                       /* (a list over it: no live area) */
    if (ui.tracks_until)
        draw_tracks();
    if (ui.patterns_until || ui.home_shown)
        draw_patterns();
    if (ui.preset_shown)
        draw_preset_slots();
    if (pb.open)
        draw_preset_browser();
    cv_blit(0, 36);
}

/* the bottom band's last MIDI message in, and where from */
static void draw_midi_in(void)
{
    char h[12];
    midi_in_text(h);
    cv_text(8, 41, &FONT_XS, "MIDI IN", C_GRAY);
    cv_text(56, 41, &FONT_XS, midi_in_port(), C_HI);
    cv_text(86, 41, &FONT_XS, h, C_WHITE);
}

/* the bottom band: HOME's, SEQ's and the mixer's own; else the page's state (draw_status), the octave, the sound's
 * preset, the volume; a message, the last MIDI message in or DEBUG's queue; the load. No keys drawn: the FM-1's own
 * are under the screen */
static void draw_bottom(void)
{
    char t[48];
    cv_begin(240, 60, C_BLACK);
    cv_rect(8, 0, 224, 1, C_LINE);
    if (active_page == PAGE_SEQ && !ui.step_mode)
        draw_home_band();
    else if (active_page == PAGE_SEQ)
        draw_seq_band();
    else if (active_page == PAGE_MIXER)
        draw_mixer_band();
    else {
        draw_status(6);
        octave_text(t);
        cv_text(8, 23, &FONT_XS, "OCT", C_GRAY);
        cv_text(34, 23, &FONT_XS, t, C_HI);
        cv_text(62, 23, &FONT_XS, "PRESET", C_GRAY);
        cv_text(106, 23, &FONT_XS, preset_label(track_sel), C_HI);
        if (band_message(t, 170))
            cv_text(8, 43, &FONT_T, t, preset_dirty || preset_import_failed ? C_HI : C_GRAY);
        else if (active_page == PAGE_DEBUG)
            debug_number(8, 43, "QUEUE", synth_queue_overflows, 0);
        else if (midi_count)
            draw_midi_in();
        cv_text(178, 23, &FONT_XS, "VOL", C_GRAY);
        int_text((int32_t)(plat_master() * 100u / 4096u), t);
        text_cat(t, "%");
        cv_text(204, 23, &FONT_XS, t, C_HI);
        draw_cpu(178, 41);
    }
    cv_blit(0, 180);
}

static void draw(void)
{
    draw_header();
    draw_page();
    draw_bottom();
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
    palette_set(theme_palette());            /* (0.25.1) GLO's SCREEN: the theme chosen (the start set the app's) */
    pb.open = 0;                             /* (0.27) no presets' browser at the start */
    lcd_fill(0, 0, 240, 240, C_BLACK);
    ui.redraw = 1;
    if (preset_import_failed) app_say("PRESET IMPORT FAILED", "REBOOT TO RETRY");
}

/* what the header, the bottom band and the lists show, changed (a knob's light going out, the load, DEBUG's numbers,
 * MIDI in, the volume, a message over, the tempo, PLAY or STOP) or gone (the presets' browser 3 s after its last
 * turn, the tracks' list): all of it redrawn */
static void frame_changes(uint32_t now, uint32_t pct)
{
    static uint32_t last_pct, lit_until, last_midi;
    static uint8_t seen;
    for (uint32_t i = 0; i < NE; i++)        /* an encoder's light goes out */
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
        if (pct != last_pct && seen)
            ui.vol_at = now ? now : 1u;      /* (SEQ's band: the volume a moment) */
        seen = 1;
        last_pct = pct;
        if (now >= ui.msg_until)
            ui.msg_until = 0;
        ui.redraw = 1;
    }
    if (seq_bpm(&seq) != ui.shown_bpm || seq.ext != ui.shown_ext) {   /* the tempo: TEMPO turned, or a clock in's */
        ui.shown_bpm = seq_bpm(&seq);
        ui.shown_ext = seq.ext;
        int_text((int32_t)ui.shown_bpm, ui.bpm_text);
        ui.redraw = 1;
    }
    if (pb.open && now >= pb.until)
        pb_close();
    if (ui.tracks_until && now >= ui.tracks_until) {
        ui.tracks_until = 0;
        ui.redraw = 1;
    }
    if (seq.playing != ui.shown_playing) {   /* PLAY or STOP: the header's (every page's), SEQ's bottom band */
        ui.shown_playing = seq.playing;
        ui.redraw = 1;
    }
}

/* SEQ's page: HOME's terricons while they move; step mode's grid when a step moves, and while it plays its
 * flashes and cursor; ~30 times a second (10 while the sound is busy), not under a list. The count-in's beats to
 * come (4 .. 1, in the bottom band); the volume's moment over */
static void seq_frame(uint32_t now)
{
    if (!ui.step_mode) {
        if (now - ui.grid_at >= frame_ms() && levels_update(now))
            ui.redraw_grid = 1;
    } else {
        ui.level_at = now;
        if (seq.step[track_sel] != ui.shown_step) {
            ui.shown_step = seq.step[track_sel];
            ui.redraw_grid = 1;
        }
        if (seq.playing && now - ui.grid_at >= frame_ms())
            ui.redraw_grid = 1;
    }
    if (list_open() || ui.splash_until)
        ui.redraw_grid = 0;
    {
        const uint32_t c = seq.count_in, n = !seq.playing ? 0u : c >= 16u ? 4u : c ? 4u - (15u - c) / 4u :
                           seq.start_due ? 1u : 0u;
        if (n != ui.shown_count) {
            ui.shown_count = (uint8_t)n;
            ui.redraw = 1;
        }
    }
    if (ui.vol_at && now - ui.vol_at >= 1300u) {
        ui.vol_at = 0;
        ui.redraw = 1;
    }
}

/* the mixer's meters (0.26): up at once, falling back over 0.6 s; redrawn while they move */
static void mixer_frame(uint32_t now)
{
    if (now - ui.grid_at < frame_ms())
        return;
    int moving = 0;
    const uint32_t fall = (now - ui.grid_at) * 256u / 600u;
    for (uint32_t t = 0; t < SYNTH_TRACKS; t++) {
        const uint32_t target = meter_of(grav_track_peak[t]);
        uint32_t m = ui.meter[t] > fall ? ui.meter[t] - fall : 0u;
        if (target > m)
            m = target;
        moving |= m != ui.meter[t] || m;
        ui.meter[t] = (uint16_t)m;
    }
    if (moving && !(list_open() || ui.splash_until))
        ui.redraw_grid = 1;
    else
        ui.grid_at = now;                    /* (still: the next fall from now) */
}

/* a frame: the input, the patterns, the presets, the screen reader; then whatever changed drawn, the least that
 * shows it: everything, SEQ's or the mixer's strip, a knob's tile, or a live icon's area */
void app_frame(void)
{
    static int last_batt = -2;
    const uint32_t now = plat_ms(), pct = plat_master() * 100u / 4096u;
    const int batt = plat_battery();
    if (batt != last_batt) {
        last_batt = batt;
        ui.redraw = 1;
    }
    input();
    patterns();
    presets_poll();
    speak();
    frame_changes(now, pct);
    if (active_page == PAGE_SEQ)
        seq_frame(now);
    else if (active_page == PAGE_MIXER)
        mixer_frame(now);
    if (ui.splash_until && now >= ui.splash_until) {
        ui.splash_until = 0;
        ui.redraw = 1;
    }
    if (ui.redraw) {
        ui.redraw = 0;
        ui.redraw_grid = 0;
        ui.grid_at = now;
        draw();
        ui.live_at = now;
    } else if (ui.redraw_grid) {
        ui.redraw_grid = 0;
        ui.grid_at = ui.grid_alone_at = now;
        ui.grid_frames++;
        draw_seq_grid();
    } else if (animate_dials(now)) {
        /* (a knob's tile alone) */
    } else if (ui.live_w && now - ui.live_at >= frame_ms()) {
        ui.live_at = now;                    /* (the icon alive: its area alone) */
        ui.live_frames++;
        draw_live();
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
    fx_page = PAGE_FX_MIX;
    fx_freeze = fx_seed = 0;
    fx_seed_auto = 1;
    memset(seq_midi_sent, 0, sizeof seq_midi_sent);   /* (every track's) */
    seq_midi_owed = 0;
}

int app_host_cmd(const char *cmd, const char *a, const char *b, const char *c, const char *d)
{
    (void)a; (void)b; (void)c; (void)d;
    if (!strcmp(cmd, "preset_fail")) { preset_store_test_fail(atoi(a)); return 1; }
    if (!strcmp(cmd, "soundpage")) { sound_activate((uint32_t)atoi(a)); return 1; }
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
    if (!strcmp(cmd, "save017")) {           /* the flash as 0.17 left it: the pattern in OBJ_SEQ and OBJ_DRUMS (with the
                                              * tempo), the settings without one, no bank */
        settings_capture();
        seq.pat.bpm = seq.bpm;
        plat_store_save(OBJ_SETTINGS, &set, SET_SIZE_017);
        plat_store_save(OBJ_SEQ, seq_record_of(&seq.pat), sizeof(seq_record_t));
        plat_store_save(OBJ_DRUMS, seq_drum_record_of(&seq.pat), sizeof(seq_drum_record_t));
        for (uint32_t off = BANK_LO; off < BANK_HI; off += 0x1000u)
            plat_data_erase(off);
        return 1;
    }
    if (!strcmp(cmd, "save025")) {           /* (0.26) the settings as 0.21..0.25 saved them: no mixer */
        settings_capture();
        plat_store_save(OBJ_SETTINGS, &set, SET_SIZE_025);
        return 1;
    }
    if (!strcmp(cmd, "save026")) {           /* (0.27) the settings as 0.26 saved them: no faders */
        settings_capture();
        plat_store_save(OBJ_SETTINGS, &set, SET_SIZE_026);
        return 1;
    }
    if (!strcmp(cmd, "save012") || !strcmp(cmd, "save016") || !strcmp(cmd, "save018") || !strcmp(cmd, "save021")) {   /* the settings as 0.12,
                                                                                                * 0.16, 0.18 saved them */
        settings_capture();
        plat_store_save(OBJ_SETTINGS, &set, !strcmp(cmd, "save012") ? SET_SIZE_012 :
                                            !strcmp(cmd, "save016") ? SET_SIZE_016 : !strcmp(cmd, "save018") ? SET_SIZE_018 : SET_SIZE_021);
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
    else if (!strcmp(what, "header_bpm"))    /* the tempo the header shows, and whether a clock in's (in colour) */
        *got = (int)ui.shown_bpm;
    else if (!strcmp(what, "header_ext"))
        *got = ui.shown_ext;
    else if (!strcmp(what, "pattern"))       /* the pattern playing, 0..15; the next (-1: none); unsaved edits */
        *got = seq.pattern;
    else if (!strcmp(what, "pattern_next"))
        *got = seq.next == SEQ_NO_PATTERN ? -1 : (int)seq.next;
    else if (!strcmp(what, "pattern_dirty"))
        *got = seq.edits != seq.saved;
    else if (!strcmp(what, "patterns_used"))  /* a bit for each pattern with a step on */
        *got = (int)patterns_used();
    else if (!strcmp(what, "bank_live"))     /* the bank's slots: patterns saved, free, to erase */
        *got = (int)bank_count(&bank, BANK_LIVE);
    else if (!strcmp(what, "bank_free"))
        *got = (int)bank_count(&bank, BANK_FREE);
    else if (!strcmp(what, "bank_stale"))
        *got = (int)bank_count(&bank, BANK_STALE);
    else if (!strcmp(what, "tune"))          /* the selected drum's TUNE; SHAPE's knobs (shape0..2) */
        *got = drum_tune[(track_sel - SYNTH_S_TRACKS) % SYNTH_D_TRACKS];
    else if (!strncmp(what, "shape", 5) && what[5] >= '0' && what[5] <= '2')
        *got = drum_shape[(track_sel - SYNTH_S_TRACKS) % SYNTH_D_TRACKS][what[5] - '0'];
    else if (!strcmp(what, "live_w"))        /* the live area's width (0: none), frames drawn alone; the splash */
        *got = ui.live_w;
    else if (!strcmp(what, "live_frames"))
        *got = (int)ui.live_frames;
    else if (!strcmp(what, "splash"))
        *got = ui.splash_until != 0;
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
    else if (!strcmp(what, "preset"))
        *got = preset_id[track_sel];
    else if (!strcmp(what, "preset_dirty"))
        *got = preset_dirty;
    else if (!strcmp(what, "preset_seq"))
        *got = (int)user_presets.seq;
    else if (!strcmp(what, "preset_pending"))
        *got = user_presets.n_pending;
    else if (!strcmp(what, "sound_page"))
        *got = ui.sound_page[track_sel];
    else if (!strcmp(what, "sound_pages"))
        *got = (int)sound_pages();
    else if (!strcmp(what, "dial_frames"))
        *got = (int)ui.dial_frames;
    else if (!strcmp(what, "grid_frames"))   /* SEQ's strip drawn alone (as it plays), and since its last frame, ms */
        *got = (int)ui.grid_frames;
    else if (!strcmp(what, "grid_since"))
        *got = (int)(plat_ms() - ui.grid_alone_at);
    else if (!strncmp(what, "param", 5))
        *got = (int)param_value(active_page, (uint32_t)atoi(what + 5) % 3u);
    else if (!strcmp(what, "fm_custom"))
        *got = !!(set.controls[track_sel].flags & GRAV_CUSTOM_FM);
    else if (!strcmp(what, "preset_grid"))
        *got = ui.preset_shown;
    else if (!strcmp(what, "preset_page"))
        *got = ui.preset_page;
    else if (!strncmp(what, "preset_used", 11))
        *got = preset_used(&user_presets, (uint32_t)atoi(what + 11));
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
        *got = (int)seq_track_swing(&seq.pat, track_sel);   /* (0.25: the selected track's) */
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
    else if (!strcmp(what, "rec_live"))      /* (0.24) recording live; the metronome's clicks begun; its setting */
        *got = ui.rec_live;
    else if (!strcmp(what, "clicks"))
        *got = (int)clicks;
    else if (!strcmp(what, "mute"))          /* (0.26) the mixer: the tracks muted (a bit each), the sequencer's too */
        *got = set.mute | seq.mute << 8;
    else if (!strcmp(what, "browser"))       /* (0.27) the presets' browser: open; showing all; its sounds; edited */
        *got = pb.open;
    else if (!strcmp(what, "browser_all"))
        *got = pb.all;
    else if (!strcmp(what, "browser_n"))
        *got = pb.n;
    else if (!strcmp(what, "preset_edited"))
        *got = ui.preset_edited >> track_sel & 1u;
    else if (!strcmp(what, "engine"))        /* the selected track's engine (GRAV_ENGINE_*) */
        *got = *track_engine(track_sel);
    else if (!strcmp(what, "terricons")) {   /* HOME's terricons drawn (a bit each), and those sounding */
        int m = 0;
        for (uint32_t t = 0; t < SEQ_TRACKS; t++)
            m |= track_used(t) << t;
        *got = m;
    } else if (!strcmp(what, "terricons_sounding")) {
        int m = 0;
        for (uint32_t t = 0; t < SEQ_TRACKS; t++)
            m |= (ui.level[t] > 0.02f) << t;
        *got = m;
    } else if (!strcmp(what, "terricons_heard")) {   /* (0.27.1) those that sounded since asked last */
        *got = ui.level_heard;
        ui.level_heard = 0;
    } else if (!strcmp(what, "solo"))          /* (0.26.1) the tracks soloed */
        *got = ui.solo;
    else if (!strcmp(what, "send"))          /* the selected track's SEND, LEVEL; the effect's RETURN (DRY/WET) */
        *got = set.send[track_sel];
    else if (!strcmp(what, "level"))
        *got = set.controls[track_sel].level;
    else if (!strcmp(what, "vol"))           /* (0.27) the selected track's fader */
        *got = set.vol[track_sel];
    else if (!strcmp(what, "fx_return"))
        *got = fx_params[FX_DRY_WET];
    else if (!strcmp(what, "meter"))         /* the selected track's meter, 0..256 */
        *got = ui.meter[track_sel];
    else if (!strcmp(what, "theme"))         /* (0.25.1) GLO's SCREEN: the palette chosen + 1 (0: the app's); shown */
        *got = set.theme;
    else if (!strcmp(what, "palette"))
        *got = (int)theme_palette();
    else if (!strcmp(what, "metro"))         /* METRO_*: 0 REC, 1 OFF, 2 COUNT */
        *got = set.metro;
    else if (!strcmp(what, "count_in"))      /* the count-in's steps still to come */
        *got = seq.count_in;
    else if (!strcmp(what, "track_swing"))   /* the selected track's own swing (0: the pattern's) */
        *got = TRK.swing;
    else if (!strcmp(what, "seq_skip"))      /* the selected track's step + 1 not played next (0: none) */
        *got = seq.skip[track_sel];
    else if (!strcmp(what, "seq_ahead")) {   /* a note now would go to the next step (late in the one playing) */
        int ahead;
        seq_quantize(&seq, track_sel, &ahead);
        *got = ahead;
    } else if (!strcmp(what, "seq_hits"))    /* the selected track's notes begun by the sequencer */
        *got = (int)seq.hits[track_sel];
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
    else if (!strcmp(what, "dirty"))         /* SAVE lit: the settings changed, or the pattern */
        *got = set_dirty || seq.edits != seq.saved;
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
