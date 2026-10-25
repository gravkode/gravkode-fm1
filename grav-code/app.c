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
} settings_t;
#define SET_SIZE_012 20                  /* the record as 0.12 saved it: up to reserved[] */
_Static_assert(sizeof(settings_t) == 32 && SET_SIZE_012 == __builtin_offsetof(settings_t, fx),
               "the settings record's layout");
#define VOICES_DEFAULT SYNTH_VOICES_MAX
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
    PAGE_DEBUG,                          /* REC: temporary dual-core diagnostics */
    PAGE_N
};
#define IS_FX_PAGE(p) ((p) >= PAGE_FX_GRAIN && (p) <= PAGE_FX_MIX)

static uint8_t active_page = PAGE_OSC;
static uint8_t fx_page = PAGE_FX_GRAIN;  /* the FX page shown last: FX goes back to it */
static const char *const PAGE_NAME[PAGE_N] = {"OSC", "MOD", "ENV", "GLO", "FX GRAIN", "FX SHAPE", "FX MIX", "DEBUG"};

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

static void queue_key_event(synth_event_type_t type, int32_t note, uint32_t velocity)
{
    synth_event_t event = {type, note, velocity};
    if (!synth_event_queue_push(&synth_queue, &event)) {
        synth_queue_overflows++;
        synth_emergency_panic++;
    }
}
static grav_engine_t grav_engine;

/* the effect (Beads): its knobs (FX_*), QUALITY, and EDIT's and SEL's states; the audio interrupt reads them */
static volatile int32_t fx_params[FX_N];
static volatile int32_t fx_quality;      /* GRAV_FX_* */
static volatile uint8_t fx_freeze;       /* EDIT: the recording held */
static volatile uint8_t fx_seed;         /* SEL held */
static volatile uint8_t fx_seed_auto = 1;   /* grains on their own (DENSITY's); SEL pressed: by hand, held 2 s: back */
#define SEED_AUTO_MS 2000u
static const char *const QUALITY_NAME[GRAV_FX_QUALITIES] = {"DIGITAL", "VINTAGE", "CLEAN TAPE", "DIRTY TAPE"};

/* Plaits parameters: 0..100% */
static volatile int32_t plaits_timbre = 65;
static volatile int32_t plaits_morph = 50;
static volatile int32_t plaits_harmonics = 75;

/* Parameters for future modulation and envelope DSP. */
static volatile int32_t mod_params[3] = {50, 0, 0};
static volatile int32_t env_params[3] = {10, 50, 30};

static const char *const PARAM_NAME[PAGE_N][3] = {
    {"TIMBRE", "MORPH", "HARMONICS"},
    {"LFO RATE", "LFO DEPTH", "LFO DEST"},
    {"ATTACK", "DECAY", "RELEASE"},
    {"VOICES", "VELOCITY", "VERSION"},
    {"DENSITY", "TIME", "SIZE"},
    {"SHAPE", "PITCH", "FEEDBACK"},
    {"DRY/WET", "REVERB", "QUALITY"}
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
        if ((st & 0xF0u) == 0x90u && d2) {
            synth_audio_mute = 0u;
            synth_note_on(&synth, (int32_t)d1, d2);
        }
        else if ((st & 0xF0u) == 0x80u || (st & 0xF0u) == 0x90u)
            synth_note_off(&synth, (int32_t)d1);
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
            synth_note_on(&synth, event.note, event.velocity);
            break;
        case SYNTH_EVENT_NOTE_OFF:
            synth_note_off(&synth, event.note);
            break;
        case SYNTH_EVENT_PANIC:
            synth_audio_mute = 1u;
            synth_panic(&synth);
            break;
        case SYNTH_EVENT_VOICES:             /* GLO: MONO or POLY (every voice let go) */
            synth_set_voices(&synth, (uint32_t)event.note);
            break;
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

    float timbre = (float)plaits_timbre * 0.01f;
    float morph = (float)plaits_morph * 0.01f;
    float lfo_value = lfo_process(&synth.mod_lfo);
    int32_t lfo_dest = mod_params[2] / 34;
    float pitch_offset = (lfo_dest == 2) ? lfo_value * 2.0f : 0.0f;

    if (lfo_dest == 0)
        timbre += lfo_value * 0.5f;
    else if (lfo_dest == 1)
        morph += lfo_value * 0.5f;

    if (timbre < 0.0f) timbre = 0.0f;
    if (timbre > 1.0f) timbre = 1.0f;
    if (morph < 0.0f) morph = 0.0f;
    if (morph > 1.0f) morph = 1.0f;

    grav_beads_params_t fx = {
        (float)fx_params[FX_DENSITY] * 0.01f, (float)fx_params[FX_TIME] * 0.01f,
        (float)fx_params[FX_SIZE] * 0.01f, (float)fx_params[FX_SHAPE] * 0.01f,
        (float)fx_params[FX_PITCH] * 0.01f,
        (float)fx_params[FX_FEEDBACK] * 0.01f, (float)fx_params[FX_DRY_WET] * 0.01f,
        (float)fx_params[FX_REVERB] * 0.01f,
        (uint8_t)fx_quality, fx_freeze, fx_seed, fx_seed_auto
    };

    grav_engine_render(
        &grav_engine,
        lr,
        n,
        &synth,
        set.engine_id,
        master_q12,
        timbre,
        morph,
        (float)plaits_harmonics / 100.0f,
        pitch_offset,
        &fx
    );

    lfo_advance(&synth.mod_lfo, n);

    if (synth_audio_mute || synth_ota_mute) {
        for (uint32_t i = 0; i < 2u * n; ++i)
            lr[i] = 0;
    }
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
    queue_key_event(SYNTH_EVENT_PANIC, 0, 0);
}

/* --------------------------------------------------------------- settings --- */
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
}

/* the knobs' parameters as they are now, into the settings record (SAVE writes the record) */
static void settings_capture(void)
{
    set.osc[0] = (uint8_t)plaits_timbre;
    set.osc[1] = (uint8_t)plaits_morph;
    set.osc[2] = (uint8_t)plaits_harmonics;
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

    if ((loaded == 8 || loaded == SET_SIZE_012 || loaded == (int)sizeof loaded_settings) &&
        loaded_settings.octave >= -2 && loaded_settings.octave <= 2) {

        if ((loaded == (int)sizeof loaded_settings || loaded == SET_SIZE_012) &&
            loaded_settings.magic == SET_MAGIC &&
            loaded_settings.engine_id < GRAV_ENGINE_COUNT) {
            int valid = 1;
            if (loaded == SET_SIZE_012)      /* (saved before 0.13, before the effect: its knobs at rest) */
                fx_defaults(&loaded_settings);
            for (int i = 0; i < 3; ++i) {
                if (loaded_settings.osc[i] > 100u ||
                    loaded_settings.mod[i] > 100u ||
                    loaded_settings.env[i] > 100u)
                    valid = 0;
            }
            for (int i = 0; i < FX_N; ++i)
                if (loaded_settings.fx[i] > 100u)
                    valid = 0;
            if (loaded_settings.voices > SYNTH_VOICES_MAX || loaded_settings.velocity_off > 1u ||
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
            set.engine_id = loaded_settings.engine_id;
            set_dirty = 1;
        } else if (loaded == 8 &&
                   loaded_settings.magic == OLD_SET_MAGIC &&
                   loaded_settings.engine_id <= 2u) {
            set.octave = loaded_settings.octave;
            set.engine_id = loaded_settings.engine_id == 2u ? 1u : 0u;
            set_dirty = 1;
        }
    }

    plaits_timbre = set.osc[0];
    plaits_morph = set.osc[1];
    plaits_harmonics = set.osc[2];

    for (int i = 0; i < 3; ++i) {
        mod_params[i] = set.mod[i];
        env_params[i] = set.env[i];
    }
    for (int i = 0; i < FX_N; ++i)
        fx_params[i] = set.fx[i];
    fx_quality = set.fx_quality;
    synth_set_voices(&synth, set.voices);   /* (before the audio starts: no queue needed) */
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
        text_cat(t, grav_engine_name(set.engine_id));
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

static const char *voices_text(uint32_t voices)   /* GLO VOICES: MONO, or how many */
{
    static const char *const T[] = {"MONO", "MONO", "2", "3", "4"};
    return T[voices <= SYNTH_VOICES_MAX ? voices : 1];
}

/* GLO's knobs: K1 VOICES (MONO, 2, 3, 4), K2 VELOCITY (right: on, the keys' own; left: off, fixed), K3 the version */
static void glo_turn(uint32_t index, int32_t e)
{
    if (index == 0u) {
        int32_t v = clamp((int32_t)set.voices + e, 1, (int32_t)SYNTH_VOICES_MAX);
        if (v != (int32_t)set.voices) {
            set.voices = (uint8_t)v;
            set_dirty = 1;
            queue_key_event(SYNTH_EVENT_VOICES, v, 0);   /* (the audio interrupt's to change) */
        }
        app_say("VOICES", voices_text(set.voices));
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
        return index == 0u ? &plaits_timbre : index == 1u ? &plaits_morph : &plaits_harmonics;
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
    if (page == PAGE_FX_SHAPE && index == 1u) {
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
            v = set.velocity_off ? FIXED_VELOCITY : touch_vel(k);   /* GLO VELOCITY: the keys' own, or fixed */
            ui.vel[k] = (uint8_t)v;
            ui.prs[k] = 0;
            ui.note[k] = (uint8_t)note;
            ui.last_key = (int32_t)k;
            queue_key_event(SYNTH_EVENT_NOTE_ON, note, v);
            plat_midi_send(0x09u | 0x90u << 8 | (uint32_t)note << 16 | v << 24, TO_USB);
        } else if (!(keys & bit) && (ui.keys & bit)) {
            note = ui.note[k];
            ui.prs[k] = 0;
            queue_key_event(SYNTH_EVENT_NOTE_OFF, note, 0);
            plat_midi_send(0x08u | 0x80u << 8 | (uint32_t)note << 16, TO_USB);
        } else if ((keys & bit) && touch_pressure(k) != ui.prs[k]) {   /* pressure: poly aftertouch */
            note = ui.note[k];
            ui.prs[k] = touch_pressure(k);
            ui.redraw = 1;
            plat_midi_send(0x0Au | 0xA0u << 8 | (uint32_t)note << 16 | (uint32_t)ui.prs[k] << 24, TO_USB);
        }
    }
    for (k = 0; k < NB; k++) {               /* a button pressed: its name, and what it does here */
        uint32_t bit = 1u << k;
        if (!(btn & bit) || (ui.btn & bit))
            continue;
        if (k == B_HOME || k == B_LFO || k == B_ENV || k == B_GLO || k == B_FX) {
            if (k == B_FX && IS_FX_PAGE(active_page))   /* FX again: the next of its pages */
                fx_page = active_page == PAGE_FX_MIX ? PAGE_FX_GRAIN : (uint8_t)(active_page + 1u);
            active_page = k == B_HOME ? PAGE_OSC :
                          k == B_LFO  ? PAGE_MOD :
                          k == B_ENV  ? PAGE_ENV :
                          k == B_GLO  ? PAGE_GLO : fx_page;
            app_say("PAGE", PAGE_NAME[active_page]);
            ui.redraw = 1;
        } else if (k == B_REC) {
            active_page = active_page == PAGE_DEBUG ? PAGE_OSC : PAGE_DEBUG;
            app_say("PAGE", PAGE_NAME[active_page]);
        } else if (k == B_EDIT) {            /* FREEZE: the effect's recording held, or let go */
            fx_freeze = !fx_freeze;
            app_say("FREEZE", fx_freeze ? "ON" : "OFF");
        } else if (k == B_SEL) {             /* SEED: a grain now, and grains by hand from here on */
            ui.seed_at = plat_ms();
            fx_seed = 1;
            app_say("SEED", fx_seed_auto ? "MANUAL" : "");
            fx_seed_auto = 0;
        } else if (k == B_OCTDN)
            octave_step(-1);
        else if (k == B_OCTUP)
            octave_step(1);
        else if (k == B_SAVE) {
            settings_capture();
            if (plat_store_save(OBJ_SETTINGS, &set, sizeof set) == 0) {
                set_dirty = 0;
                app_say("SAVED", "");
            } else {
                app_say("SAVE", "FAILED");
            }
        } else {
            app_say("BUTTON", BTN_NAME[k]);
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
        if ((k == EN_K1 || k == EN_K2 || k == EN_K3 || k == EN_K4) && active_page == PAGE_DEBUG) {
            if (k == EN_K1) {
                grav_dual_core = e > 0;
                app_say("CORES", grav_dual_core ? "2" : "1");
            } else if (k == EN_K2) {
                grav_debug_reset = 1;
                plat_audio_debug_reset();
                ui.debug_xruns_base = plat_xruns();
                app_say("DEBUG", "RESET");
            }
        } else if ((k == EN_K1 || k == EN_K2 || k == EN_K3) && active_page == PAGE_GLO) {
            glo_turn((uint32_t)(k - EN_K1), e);
        } else if (k == EN_K3 && active_page == PAGE_FX_MIX) {
            quality_turn(e);
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
            set.engine_id = (uint8_t)grav_engine_step(set.engine_id, e);   /* in Plaits' order */
            set_dirty = 1;
            app_say("WAVE", grav_engine_name(set.engine_id));
        } else {
            int_text(ui.turns[k], t);
            app_say(ENC_NAME[k], t);
        }
    }
    if (keys != ui.keys || btn != ui.btn)
        ui.redraw = 1;
    ui.keys = keys;
    ui.btn = btn;
    plat_glow(1);                            /* the unlit buttons glow, readable in the dark */
    {                                        /* lit: held, SAVE (unsaved), EDIT (frozen); FX (on) and SEL (AUTO) */
        uint32_t lit = btn | (set_dirty ? 1u << B_SAVE : 0u) | (fx_freeze ? 1u << B_EDIT : 0u);
        if (grav_beads_on())
            lit |= 1u << B_FX | (fx_seed_auto ? 1u << B_SEL : 0u);
        plat_leds(lit, keys);
    }
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
        int32_t fill = i == 0u ? (int32_t)set.voices * 68 / (int32_t)SYNTH_VOICES_MAX : set.velocity_off ? 0 : 68;
        cv_text(x, 62, &FONT_M, i == 0u ? voices_text(set.voices) : set.velocity_off ? "OFF" : "ON", C_WHITE);
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
    debug_number(8, 60, "FX", d.fx_us, 0);
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

    cv_text(8, 4, &FONT_S, grav_engine_name(set.engine_id), C_HI);
    if (fx_freeze)                           /* the effect frozen: on every page (and EDIT lit) */
        cell(172, 2, 60, "FREEZE", 1);

    if (active_page == PAGE_GLO)
        draw_glo();
    else if (active_page == PAGE_DEBUG)
        draw_debug();
    else
        draw_knobs();

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
    cv_text(112, 36, &FONT_XS, grav_engine_name(set.engine_id), C_HI);
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
    if (ui.redraw) {
        ui.redraw = 0;
        draw();
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
int app_host_cmd(const char *cmd, const char *a, const char *b, const char *c, const char *d)
{
    (void)a; (void)b; (void)c; (void)d;
    if (!strcmp(cmd, "debug_overload")) {
        app_overload();
        return 1;
    }
    if (strcmp(cmd, "save012"))
        return 0;
    settings_capture();
    plat_store_save(OBJ_SETTINGS, &set, SET_SIZE_012);
    return 1;
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
    else if (!strcmp(what, "render_voices"))
        *got = (int)grav_debug.active;
    else if (!strcmp(what, "shed"))
        *got = (int)grav_debug.shed;
    else if (!strcmp(what, "steals"))
        *got = (int)grav_debug.steals;
    else if (!strcmp(what, "wave"))
        *got = set.engine_id;
    else if (!strcmp(what, "engine_ram"))    /* bytes of its voice's RAM the engine's Init took */
        *got = (int)grav_plaits_ram_used();
    else if (!strcmp(what, "voices"))        /* GLO VOICES: 1 MONO, 2..4 POLY */
        *got = set.voices;
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
        *got = i == 0 ? plaits_timbre : i == 1 ? plaits_morph : plaits_harmonics;
    } else if (!strncmp(what, "mod", 3))
        *got = mod_params[atoi(what + 3) % 3];
    else if (!strncmp(what, "env", 3))
        *got = env_params[atoi(what + 3) % 3];
    else
        return 0;
    return 1;
}
#endif
