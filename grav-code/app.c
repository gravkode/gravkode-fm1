/* SPDX-License-Identifier: MPL-2.0
 * Copyright (C) 2026 Charles Vestal */
/* GRAV CODE: one voice on Plaits' engines (dsp/), with an envelope and an LFO (synth/). The keys play it with
 * velocity and go out over USB and Bluetooth MIDI (MIDI in plays it too); PRESET picks the engine, K1-K3 the page's
 * parameters (HOME: OSC, LFO: MOD, ENV: ENV), OCT- / OCT+ the octave, MASTER the volume, SAVE stores it all, REC
 * Bluetooth. Part of the unity build (device/unity.c, host/plat_host.c), after the platform: gfx.c and announce.c
 * are in scope. */
#include "fastmath.h"
#include "dsp/grav_engine.h"
#include "dsp/grav_plaits.h"
#include "synth/synth.h"
#include "synth/event_queue.h"

#define SET_MAGIC 0x47525639u
#define PREV_SET_MAGIC 0x54504C32u
#define OLD_SET_MAGIC 0x54504C31u            /* "TPL1": the settings record; another magic loads defaults */
typedef struct {
    uint32_t magic;
    int8_t octave;                       /* -2 .. +2 */
    uint8_t engine_id;                   /* GRAV_ENGINE_*: Plaits' Waveshaping, FM, Virtual Analog, Modal */
    uint8_t ble_off;                     /* Bluetooth switched off (REC held); 0, the default: on */
    uint8_t rsv[1];
    uint8_t osc[3];
    uint8_t mod[3];
    uint8_t env[3];
    uint8_t reserved[3];
} settings_t;
static settings_t set;
static uint8_t set_dirty;                /* changed since the last save: SAVE is lit */

static const char *const ENGINE_NAME[GRAV_ENGINE_COUNT] = {"PLAITS WS", "PLAITS FM", "PLAITS VA", "PLAITS MDL"};

/* Parameter pages: UI state only, independent of DSP. */
enum {
    PAGE_OSC = 0,
    PAGE_MOD,
    PAGE_ENV
};

static uint8_t active_page = PAGE_OSC;
static const char *const PAGE_NAME[] = {"OSC", "MOD", "ENV"};

/* the controls by their names in plat.h, in its order */
static const char *const BTN_NAME[NB] = {"FX", "SEL", "ENV", "LFO", "EDIT", "GLO", "HOME", "SAVE", "ARP", "SEQ",
                                         "PLAY", "REC", "OCT-", "OCT+"};
static const char *const ENC_NAME[NE] = {"SELECT", "ALGO", "PRESET", "K1", "K2", "K3", "K4"};

/* ------------------------------------------------------------------ voice --- */
/* one voice, the last note wins; the main loop and MIDI in set it, the audio interrupt plays it */
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

/* Plaits parameters: 0..100% */
static volatile int32_t plaits_timbre = 65;
static volatile int32_t plaits_morph = 50;
static volatile int32_t plaits_harmonics = 75;

/* Parameters for future modulation and envelope DSP. */
static volatile int32_t mod_params[3] = {50, 0, 0};
static volatile int32_t env_params[3] = {10, 50, 30};

static const char *const PARAM_NAME[3][3] = {
    {"TIMBRE", "MORPH", "HARMONICS"},
    {"LFO RATE", "LFO DEPTH", "LFO DEST"},
    {"ATTACK", "DECAY", "RELEASE"}
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

    envelope_set(
        &synth.amp_env,
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
        pitch_offset
    );

    lfo_advance(&synth.mod_lfo, n);

    if (synth_audio_mute || synth_ota_mute) {
        for (uint32_t i = 0; i < 2u * n; ++i)
            lr[i] = 0;
    }
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
static void settings_defaults(void)
{
    set.magic = SET_MAGIC;
    set.octave = 0;
    set.engine_id = 0;
    set.ble_off = 0;
    set.rsv[0] = 0;
    set.osc[0] = 65; set.osc[1] = 50; set.osc[2] = 75;
    set.mod[0] = 50; set.mod[1] = 0; set.mod[2] = 0;
    set.env[0] = 10; set.env[1] = 50; set.env[2] = 30;
    for (int i = 0; i < 3; ++i) set.reserved[i] = 0;
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

    if ((loaded == 8 || loaded == (int)sizeof loaded_settings) &&
        loaded_settings.octave >= -2 && loaded_settings.octave <= 2 &&
        loaded_settings.ble_off <= 1u) {

        if (loaded == (int)sizeof loaded_settings &&
            loaded_settings.magic == SET_MAGIC &&
            loaded_settings.engine_id < GRAV_ENGINE_COUNT) {
            int valid = 1;
            for (int i = 0; i < 3; ++i) {
                if (loaded_settings.osc[i] > 100u ||
                    loaded_settings.mod[i] > 100u ||
                    loaded_settings.env[i] > 100u)
                    valid = 0;
            }
            if (valid)
                set = loaded_settings;
        } else if (loaded == 8 &&
                   loaded_settings.magic == PREV_SET_MAGIC &&
                   loaded_settings.engine_id <= 2u) {
            set.octave = loaded_settings.octave;
            set.engine_id = loaded_settings.engine_id;
            set.ble_off = loaded_settings.ble_off;
            set_dirty = 1;
        } else if (loaded == 8 &&
                   loaded_settings.magic == OLD_SET_MAGIC &&
                   loaded_settings.engine_id <= 2u) {
            set.octave = loaded_settings.octave;
            set.engine_id = loaded_settings.engine_id == 2u ? 1u : 0u;
            set.ble_off = loaded_settings.ble_off;
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
    uint8_t looking;                     /* Bluetooth MIDI devices: REC pressed, the scan's list up (lib/btdev) */
    uint32_t rec_t;                      /* REC went down (0: not held, or its hold already acted) */
    int32_t turns[NE];                   /* detents since boot, per encoder */
    uint32_t turned_at[NE];              /* when each was last turned: it shows lit for a moment */
    uint32_t cpu, xruns, cpu_at;         /* the render's load (% of its time) and late renders, as last shown */
    uint8_t redraw;
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
        text_cat(t, ENGINE_NAME[set.engine_id]);
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

int template_ble_off(void) { return set.ble_off; }

/* REC held a second: Bluetooth off or on (kept with the settings, so it stays so at the next power-on) */
static void ble_toggle(void)
{
    set.ble_off = (uint8_t)!set.ble_off;
    set_dirty = 1;
    plat_ble_enable(!set.ble_off);
    app_say("BLUETOOTH", set.ble_off ? "OFF" : "ON");
}

/* REC: a Bluetooth MIDI device (FM1B_BLE_CENTRAL, lib/btdev): look for devices, then connect to the one ALGORITHM
 * picks, then let it go */
static void ble_device_say(void)
{
    char name[21];
    if (btdev_found(btdev_pick(), name, 0))
        app_say("DEVICE", name);
    else
        app_say("BLE", "SCANNING");
}
static void ble_device_button(void)
{
    int ev;
    if (!btdev_busy() && !ui.looking) {
        ui.looking = 1;
        app_say("BLE", "SCANNING");
        return;
    }
    ev = btdev_save();
    ui.looking = 0;
    app_say(ev == BTDEV_CONNECTING ? btdev_word(ev) : "BLE", ev == BTDEV_CONNECTING ? btdev_name() : btdev_word(ev));
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
            v = touch_vel(k);
            ui.vel[k] = (uint8_t)v;
            ui.prs[k] = 0;
            ui.note[k] = (uint8_t)note;
            ui.last_key = (int32_t)k;
            queue_key_event(SYNTH_EVENT_NOTE_ON, note, v);
            plat_midi_send(0x09u | 0x90u << 8 | (uint32_t)note << 16 | v << 24, TO_USB | TO_BLE);   /* BLE: if built with it */
        } else if (!(keys & bit) && (ui.keys & bit)) {
            note = ui.note[k];
            ui.prs[k] = 0;
            queue_key_event(SYNTH_EVENT_NOTE_OFF, note, 0);
            plat_midi_send(0x08u | 0x80u << 8 | (uint32_t)note << 16, TO_USB | TO_BLE);
        } else if ((keys & bit) && touch_pressure(k) != ui.prs[k]) {   /* pressure: poly aftertouch */
            note = ui.note[k];
            ui.prs[k] = touch_pressure(k);
            ui.redraw = 1;
            plat_midi_send(0x0Au | 0xA0u << 8 | (uint32_t)note << 16 | (uint32_t)ui.prs[k] << 24, TO_USB | TO_BLE);
        }
    }
    for (k = 0; k < NB; k++) {               /* a button pressed: its name, and what it does here */
        uint32_t bit = 1u << k;
        if (!(btn & bit) || (ui.btn & bit))
            continue;
        if (k == B_HOME || k == B_LFO || k == B_ENV) {
            active_page = k == B_HOME ? PAGE_OSC :
                          k == B_LFO  ? PAGE_MOD : PAGE_ENV;
            app_say("PAGE", PAGE_NAME[active_page]);
            ui.redraw = 1;
        } else if (k == B_OCTDN)
            octave_step(-1);
        else if (k == B_OCTUP)
            octave_step(1);
        else if (k == B_REC && FM1B_BLE)
            ui.rec_t = plat_ms();                /* a press: on release (a hold switches Bluetooth) */
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
    if (FM1B_BLE && ui.rec_t) {             /* REC: held a second, Bluetooth on / off; a press, a device */
        if (btn >> B_REC & 1u) {
            if (plat_ms() - ui.rec_t >= 1000u) {
                ui.rec_t = 0;
                ble_toggle();
            }
        } else {
            ui.rec_t = 0;
            if (FM1B_BLE_CENTRAL && !set.ble_off)
                ble_device_button();
            else
                app_say("BUTTON", "REC");
        }
    }
    for (k = 0; k < NE; k++) {               /* an encoder turned: its count; PRESET also changes the wave */
        char t[12];
        if ((e = plat_enc((int)k)) == 0)
            continue;
        ui.turns[k] += e;
        ui.turned_at[k] = plat_ms();
        ui.redraw = 1;
        if (k == EN_ALGO && FM1B_BLE_CENTRAL && ui.looking) {
            btdev_choose(btdev_pick() + e);
            ble_device_say();
        } else if (k == EN_K1 || k == EN_K2 || k == EN_K3) {
            uint32_t index = (uint32_t)(k - EN_K1);
            volatile int32_t *value;

            if (active_page == PAGE_OSC) {
                if (index == 0u)
                    value = (int32_t *)&plaits_timbre;
                else if (index == 1u)
                    value = (int32_t *)&plaits_morph;
                else
                    value = (int32_t *)&plaits_harmonics;
            } else if (active_page == PAGE_MOD) {
                value = &mod_params[index];
            } else {
                value = &env_params[index];
            }

            int32_t previous = *value;
            *value = clamp_percent(previous + e);
            if (*value != previous)
                set_dirty = 1;
            int_text(*value, t);
            app_say(PARAM_NAME[active_page][index], t);
        } else if (k == EN_PRESET) {
            set.engine_id = (uint8_t)((set.engine_id + GRAV_ENGINE_COUNT + e % GRAV_ENGINE_COUNT) % GRAV_ENGINE_COUNT);
            set_dirty = 1;
            app_say("WAVE", ENGINE_NAME[set.engine_id]);
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
    plat_leds(btn | (set_dirty ? 1u << B_SAVE : 0u), keys);
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

    cv_text(8, 4, &FONT_S, ENGINE_NAME[set.engine_id], C_HI);

    for (i = 0; i < 3u; i++) {
        int32_t value;
        int32_t x = 8 + (int32_t)i * 78;

        if (active_page == PAGE_OSC) {
            value = i == 0u ? plaits_timbre :
                    i == 1u ? plaits_morph : plaits_harmonics;
        } else if (active_page == PAGE_MOD) {
            value = mod_params[i];
        } else {
            value = env_params[i];
        }

        cv_text(x, 42, &FONT_XS, PARAM_NAME[active_page][i], C_GRAY);
        int_text(value, t);
        cv_text(x, 62, &FONT_M, t, C_WHITE);

        cv_rect(x, 100, 68, 3, C_LINE);
        if (value > 0)
            cv_rect(x, 100, value * 68 / 100, 3, C_HI);
    }

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
    cv_text(112, 36, &FONT_XS, ENGINE_NAME[set.engine_id], C_HI);
    if (midi_count) {                        /* the last MIDI message in, and where from */
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
    static uint32_t last_pct, lit_until, last_ble, last_midi;
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
    if (FM1B_BLE_CENTRAL) {                  /* looking: a device found is named at once (REC connects to it) */
        static int found;
        int n = ui.looking ? btdev_count() : 0, ev;
        btdev_page(ui.looking && !set.ble_off);
        if (n > found && btdev_pick() < n)
            ble_device_say();
        found = n;
        while ((ev = btdev_event()) != BTDEV_NONE)   /* a device connected, given up or lost */
            if (ev == BTDEV_CONNECTED || ev == BTDEV_NO_ANSWER || ev == BTDEV_LOST)
                app_say("DEVICE", btdev_word(ev));
    }
    if (now - ui.cpu_at >= 500u) {           /* the load, twice a second */
        ui.cpu_at = now;
        if (plat_cpu_pct() != ui.cpu || plat_xruns() != ui.xruns) {
            ui.cpu = plat_cpu_pct();
            ui.xruns = plat_xruns();
            ui.redraw = 1;
        }
    }
    if (midi_count != last_midi) {
        last_midi = midi_count;
        ui.redraw = 1;
    }
    if (pct != last_pct || (uint32_t)(plat_ble_connected() | plat_ble_device() << 1) != last_ble ||
        (ui.msg_until && now >= ui.msg_until)) {
        last_pct = pct;
        last_ble = (uint32_t)(plat_ble_connected() | plat_ble_device() << 1);
        if (now >= ui.msg_until)
            ui.msg_until = 0;
        ui.redraw = 1;
    }
    if (ui.redraw) {
        ui.redraw = 0;
        draw();
    }
}

#ifdef PLAT_HOST
int app_expect(const char *what, int *got)
{
    if (!strcmp(what, "octave"))
        *got = set.octave;
    else if (!strcmp(what, "wave"))
        *got = set.engine_id;
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
