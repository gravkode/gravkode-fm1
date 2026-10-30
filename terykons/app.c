/* SPDX-License-Identifier: MPL-2.0
 * Copyright (C) 2026 Charles Vestal */
/* Terykons: up to four voices on Plaits' engines (dsp/), with an envelope and an LFO (synth/), through Beads (the
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
#if defined PLAT_USB_TRACKS
_Static_assert(PLAT_USB_TRACKS == SYNTH_TRACKS, "USB audio MULTI: a channel for each track (fm1.toml usb_audio_tracks)");
#endif

_Static_assert(APP_BLOCK <= 256u, "dual-core voice buffers fit APP_BLOCK");

/* (0.32) the effect's knobs, 0..100: HALL's AMOUNT, TIME, TONE; TAPE's DIFFUSE, MOD (its QUALITY apart); ECHO's
 * AMOUNT, TIME (a note value: 0..GRAV_ECHO_TIMES - 1, dsp/grav_fx.h), FEEDBACK. Until 0.31, Beads': DENSITY, TIME,
 * SIZE, SHAPE, PITCH, FEEDBACK, DRY/WET, REVERB (fx_layout 0) */
enum { FX_HALL, FX_HALL_TIME, FX_TONE, FX_DIFFUSE, FX_MOD, FX_ECHO, FX_ECHO_TIME, FX_FEEDBACK, FX_N };
static const uint8_t FX_DEFAULT[FX_N] = {0, 40, 55, 70, 60, 0, 7, 35};   /* (both AMOUNTs 0: off; ECHO at 1/8.) */
#define FX_LAYOUT 1u                         /* fx_layout: the knobs as 0.32 has them */
#define FX_OLD_REVERB 7u                     /* (Beads' REVERB, as saved before: HALL's AMOUNT now) */

#define SET_MAGIC 0x47525639u
/* GLO's METRONOME: clicks while recording live; OFF; COUNT: and a bar's count-in when recording starts from a stop */
enum { METRO_REC, METRO_OFF, METRO_COUNT };
enum { MIDI_IN_SELECTED, MIDI_IN_CHANNELS };   /* (0.39) GLO's MIDI, IN: any channel the track selected; or a track's
                                                * channel that track (the others the one selected) */
enum { CLOCK_OFF, CLOCK_IN, CLOCK_OUT, CLOCK_BOTH };   /* and CLOCK (with Start, Continue, Stop): in the knob's order */
/* (0.28) GLO's SCREEN, K2 SKY: HOME's sky. DAY (as it comes) a blue one, its sun flaring at each kick, clouds drifting
 * at the tempo; NIGHT a moon, stars twinkling at each hi-hat; OFF black, as before */
enum { SKY_NIGHT = 1, SKY_OFF = 2, SKY_DAY = 3 };   /* (0.30.2: NIGHT as it comes; 0, as saved before, is NIGHT) */
static const char *const SKY_NAME[4] = {"NIGHT", "NIGHT", "OFF", "DAY"};
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
    uint8_t sky;                         /* (0.29) GLO's SCREEN, K2: HOME's sky, SKY_*: NIGHT (1; 0 as saved before),
                                          * OFF (2), DAY (3) */
    uint8_t fx[FX_N];                    /* 0.13: the effect's knobs; in a record saved before (20 bytes): defaults */
    uint8_t fx_quality;                  /* GRAV_FX_* (dsp/grav_fx.h) */
    uint8_t fx_layout;                   /* (0.32) FX_LAYOUT; 0 (as saved before): Beads' knobs */
    uint8_t fx_reserved[2];
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
    /* 0.37: each synth track's LFO and envelope (mod[] and env[] above keep S1's, for the record's older readers) */
    uint8_t trk_mod[SYNTH_S_TRACKS][3];
    uint8_t trk_env[SYNTH_S_TRACKS][3];
    /* 0.39: MIDI (GLO's MIDI page): each track's channel (1..16; 0 OFF: no MIDI out, none in by channel), IN
     * (MIDI_IN_*), CLOCK (CLOCK_*) */
    uint8_t midi_ch[SYNTH_TRACKS];
    uint8_t midi_in;
    uint8_t midi_clock;
    uint8_t usb_audio;                   /* (0.42, a byte reserved in 0.39..0.41: 0) GLO's USB: AUDIO, 0 STEREO, 1 MULTI */
    uint8_t midi_reserved;
} settings_t;
#define SET_SIZE_012 20                  /* the record as 0.12 saved it: up to sky (a byte reserved then) */
#define SET_SIZE_013 32                  /* as 0.13..0.15 saved it: up to fx_reserved[] (one sound) */
#define SET_SIZE_016 56                  /* as 0.16 saved it: up to trk_reserved[] (no drums) */
#define SET_SIZE_017 72                  /* as 0.17 saved it: up to drum_osc[] (the tempo the pattern's) */
#define SET_SIZE_018 76                  /* as 0.18 saved it: up to metro, then reserved (the drums on Plaits'
                                          * three knobs) */
#define SET_SIZE_021 92
#define SET_SIZE_025 1404                /* as 0.21..0.25 saved it: up to the mixer (every track sent whole, none muted) */
#define SET_SIZE_026 1420                /* as 0.26 saved it: up to the faders (each at 100) */
#define SET_SIZE_036 1428                /* as 0.27..0.36 saved it: up to the tracks' LFOs and envelopes (one for all) */
#define SET_SIZE_037 1452                /* as 0.37..0.38 saved it: up to MIDI's (channels 1..8, any in, clock both) */
_Static_assert(sizeof(settings_t) == 1464 && SET_SIZE_037 == __builtin_offsetof(settings_t, midi_ch) &&
               SET_SIZE_036 == __builtin_offsetof(settings_t, trk_mod) &&
               SET_SIZE_026 == __builtin_offsetof(settings_t, vol) &&
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
#define TAP_TAPS 5u                       /* (0.38) tap tempo: the last five taps, four beats, averaged */
#define TAP_GAP_MS 2500u                 /* a pause longer: a new count (30 BPM is 2000 ms a beat) */
static settings_t set;
static settings_t set_stored;            /* as the flash has it: SAVE writes the settings only when they differ */
static uint8_t set_dirty;                /* changed since the last save: SAVE is lit (or the pattern unsaved) */
static bank_t bank;                      /* the patterns in the flash (seq/bank.h) */
static int8_t copy_from = -1, clip_from = -1;   /* (0.40) the pattern copied (being, and done); -1: none, or not
                                                * known (since a restart, the copy kept but not its number) */
/* (0.41) a chain: HOME held, white keys one after another: the patterns in that order, round (0.41.1: each once, a
 * key again not added; stopping ends it). pos: the one playing (-1: the first, still queued); turns: the sequencer's
 * pattern ends seen; build: the keys while HOME is held (two or more make the chain when it is let go) */
#define CHAIN_MAX 16u
static struct {
    uint8_t n, at[CHAIN_MAX];
    int8_t pos;
    uint8_t playing;
    uint32_t turns;
    uint8_t bn, build[CHAIN_MAX];
} chain;
static uint32_t used_mask, used_seq;     /* the bank's patterns with a step on, as of its save used_seq (patterns_used) */

/* Parameter pages: UI state only, independent of DSP. */
enum {
    PAGE_OSC = 0,
    PAGE_MOD,
    PAGE_ENV,
    PAGE_GLO,                            /* the firmware and its global settings: VOICES, VELOCITY */
    PAGE_FX_HALL,                        /* (0.32) the effect (FX, again TAPE, again ECHO): the reverb's AMOUNT, TIME,
                                          * TONE */
    PAGE_FX_TAPE,                        /* QUALITY, DIFFUSE, MOD */
    PAGE_FX_ECHO,                        /* the delay's AMOUNT, TIME, FEEDBACK */
    PAGE_DEBUG,                          /* GLO again: dual-core diagnostics */
    PAGE_SEQ,                            /* HOME: the sequence (HOME again: OSC, the sound) */
    PAGE_SHAPE,                          /* a drum track's: HOME after its DRUM page (OSC's): SHAPE's knobs */
    PAGE_SOUND,
    PAGE_OUTPUT,
    PAGE_SCREEN,                         /* (0.25.1) GLO again: THEME, the screen's colours (GLO again: DEBUG) */
    PAGE_MIXER,                          /* (0.26) SEL: the eight tracks' LEVEL, SEND, mutes; the effect's RETURN */
    PAGE_ARP,                            /* (0.34) ARP: the selected track's arpeggiator: MODE, RATE, OCTAVES */
    PAGE_ARP_2,                          /* ARP again: GATE, HOLD */
    PAGE_TEMPO,                          /* (0.36.2) HOME again (from HOME's terricons): TEMPO, SWING, LENGTH; again
                                          * the sound's pages */
    PAGE_MIDI,                           /* (0.39) GLO again: the selected track's CHANNEL, IN, CLOCK; again USB */
    PAGE_USB,                            /* (0.42) GLO again: USB AUDIO, STEREO or MULTI; again SCREEN */
    PAGE_N
};
#define IS_FX_PAGE(p) ((p) >= PAGE_FX_HALL && (p) <= PAGE_FX_ECHO)
#define IS_ARP_PAGE(p) ((p) == PAGE_ARP || (p) == PAGE_ARP_2)
#define IS_SOUND_PAGE(p) ((p) == PAGE_OSC || (p) == PAGE_SHAPE || (p) == PAGE_SOUND || (p) == PAGE_OUTPUT)

static uint8_t active_page = PAGE_SEQ;
static uint8_t fx_page = PAGE_FX_HALL;   /* the FX page shown last: FX goes back to it (0.32: HALL first) */
static uint8_t home_page = PAGE_SEQ;     /* HOME's (SEQ or OSC) and GLO's (GLO or DEBUG) shown last */
static uint8_t glo_page = PAGE_GLO;
static uint8_t arp_page = PAGE_ARP;      /* (0.34) the ARP page shown last */
static const char *const PAGE_NAME[PAGE_N] = {"OSC", "MOD", "ENV", "GLO", "HALL", "TAPE", "ECHO", "DEBUG",
                                              "SEQ", "SHAPE", "DETAIL", "OUTPUT", "SCREEN", "MIXER", "ARP",
                                              "ARP 2", "TEMPO", "MIDI", "USB"};
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
static uint8_t seq_midi_ch[SYNTH_TRACKS] = {0, 1, 2, 3, 4, 5, 6, 7};   /* (0.39) the channel they went out on (0xFF:
                                                                         * none, the track OFF) */
static uint32_t seq_midi_owed;

/* the effect (0.32: HALL, TAPE, ECHO): its knobs (FX_*) and QUALITY; the audio interrupt reads them */
static volatile int32_t fx_params[FX_N];
static volatile int32_t fx_quality;      /* GRAV_FX_* */
static const char *const QUALITY_NAME[GRAV_FX_QUALITIES] = {"DIGITAL", "VINTAGE", "CLEAN TAPE", "DIRTY TAPE"};

/* Plaits' three knobs (OSC), each track's: 0..100% */
static volatile int32_t osc_params[SYNTH_TRACKS][3];
static volatile int32_t drum_tune[SYNTH_D_TRACKS];          /* the drums' TUNE (50: the note; a half semitone each) */
static volatile int32_t drum_shape[SYNTH_D_TRACKS][3];      /* and SHAPE's knobs (dsp/grav_drums.h), 0..100 */
static volatile uint8_t track_sel;       /* the track selected (SELECT): the keys, MIDI in and the pages play it */
/* (0.37) each synth track's LFO and envelope, part of its sound (its preset's): MOD's LFO RATE, LFO DEPTH, LFO DEST
 * and ENV's ATTACK, DECAY, RELEASE, 0..100 (DEST a third of the knob each). Until 0.36 one of each for every track.
 * The drum tracks have none: their drums envelope themselves */
static volatile int32_t trk_mod[SYNTH_S_TRACKS][3];
static volatile int32_t trk_env[SYNTH_S_TRACKS][3];
static const uint8_t MOD_DEFAULT[3] = PRESET_LFO_DEFAULT, ENV_DEFAULT[3] = PRESET_ENV_DEFAULT;
static lfo_t trk_lfo[SYNTH_S_TRACKS];
static volatile int32_t no_param;            /* (MOD's and ENV's knobs on a drum track: nothing) */
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
    } else {                                 /* (0.37) its LFO and envelope */
        p->mod_set = PRESET_MOD;
        for (uint32_t i = 0; i < 3; i++) {
            p->lfo[i] = (uint8_t)trk_mod[t][i];
            p->env[i] = (uint8_t)trk_env[t][i];
        }
    }
}

static const char *const LFO_DEST_NAME[3] = {"TIMBRE", "MORPH", "PITCH"};
/* Keep the saved 0..100 representation compatible with 0.14.0 and earlier. */
static uint32_t lfo_destination(int32_t value) { return (uint32_t)clamp(value / 34, 0, 2); }

static const char *const PARAM_NAME[PAGE_N][3] = {
    {"TIMBRE", "MORPH", "HARMONICS"},
    {"LFO RATE", "LFO DEPTH", "LFO DEST"},
    {"ATTACK", "DECAY", "RELEASE"},
    {"VOICES", "VELOCITY", "METRONOME"},
    {"AMOUNT", "TIME", "TONE"},
    {"QUALITY", "DIFFUSE", "MOD"},
    {"AMOUNT", "TIME", "FEEDBACK"},
    {"", "", ""},
    {"TEMPO", "SWING", "LENGTH"},
    {"PUNCH", "SWEEP", "DRIVE"},
    [PAGE_SCREEN] = {"THEME", "SKY", ""},
    [PAGE_MIXER] = {"VOL", "SEND", "RETURN"},
    [PAGE_ARP] = {"MODE", "RATE", "OCTAVES"},
    [PAGE_ARP_2] = {"GATE", "HOLD", ""},
    [PAGE_TEMPO] = {"TEMPO", "SWING", "LENGTH"},
    [PAGE_MIDI] = {"CHANNEL", "IN", "CLOCK"},
    [PAGE_USB] = {"AUDIO", "", ""}
};
static const char *const MIDI_IN_NAME[2] = {"SELECTED", "CHANNELS"};
static const char *const CLOCK_NAME[4] = {"OFF", "IN", "OUT", "IN+OUT"};

/* (0.34) the arpeggiator: a synth track's, its settings its sound's (grav_controls_t.arp; 0 as sounds were before:
 * off). arp[0]: MODE (bits 0..2, SEQ_ARP_*), OCTAVES - 1 (bits 3..4), HOLD (bit 5); arp[1]: RATE, ECHO's note
 * values, its index + 1 (0: a 16th); arp[2]: GATE, 5..100 % of RATE (0: 50) */
static const char *const ARP_MODE_NAME[SEQ_ARP_MODES] = {"OFF", "UP", "DOWN", "UP-DOWN", "RANDOM", "AS PLAYED",
                                                         "CHORD"};
static const uint8_t ARP_TICKS[GRAV_ECHO_TIMES] = {3, 4, 6, 8, 9, 12, 16, 18, 24, 32, 36, 48};   /* (24 a quarter) */
#define ARP_RATE_16TH 2u
static uint32_t arp_mode(uint32_t t) { return SYNTH_IS_DRUM(t) ? SEQ_ARP_OFF : set.controls[t].arp[0] & 7u; }
static uint32_t arp_octaves(uint32_t t) { return 1u + (set.controls[t].arp[0] >> 3 & 3u); }
static uint32_t arp_hold(uint32_t t) { return set.controls[t].arp[0] >> 5 & 1u; }
static uint32_t arp_rate(uint32_t t) { return set.controls[t].arp[1] ? set.controls[t].arp[1] - 1u : ARP_RATE_16TH; }
static uint32_t arp_gate(uint32_t t) { return set.controls[t].arp[2] ? set.controls[t].arp[2] : 50u; }

/* (0.35) parameter locks: each track's knobs as its steps lock them (SEQ_EV_LOCK, in the audio interrupt: the step's
 * locks, or none, until the track's next), and as the steps held in LOCK mode have them (the main loop: heard while
 * held, over the others), over the track's own */
static struct {
    volatile uint16_t mask;              /* the knobs locked, a bit each (SEQ_LK_*) */
    volatile uint8_t val[SEQ_LK_PARAMS];
} lock_seq[SYNTH_TRACKS], lock_heard[SYNTH_TRACKS];

static int32_t locked(uint32_t t, uint32_t p, int32_t own)
{
    if (lock_heard[t].mask >> p & 1u)
        return lock_heard[t].val[p];
    if (lock_seq[t].mask >> p & 1u)
        return lock_seq[t].val[p];
    return own;
}

static int32_t lock_own(uint32_t t, uint32_t p)   /* a lockable knob's own value, the track's */
{
    const uint32_t d = (t - SYNTH_S_TRACKS) % SYNTH_D_TRACKS;
    const uint32_t s = t % SYNTH_S_TRACKS;
    return p <= SEQ_LK_OSC2 ? osc_params[t][p] : p == SEQ_LK_TUNE ? drum_tune[d] :
           p <= SEQ_LK_SHAPE2 ? drum_shape[d][p - SEQ_LK_SHAPE0] : p == SEQ_LK_VOL ? set.vol[t] :
           p == SEQ_LK_SEND ? set.send[t] : p <= SEQ_LK_RELEASE ? trk_env[s][p - SEQ_LK_ATTACK] :
           trk_mod[s][p - SEQ_LK_LFO_RATE];   /* (0.37: a synth track's ENV, LFO) */
}

static int lock_param(uint32_t page, uint32_t index)   /* a page's knob: its lock (SEQ_LK_*), -1 if it has none */
{
    if (page == PAGE_OSC)                    /* (a drum's DRUM page: TUNE, then its knobs 1 and 0) */
        return SYNTH_IS_DRUM(track_sel) ? (index == 0u ? SEQ_LK_TUNE : index == 1u ? SEQ_LK_OSC1 : SEQ_LK_OSC0)
                                        : (int)(SEQ_LK_OSC0 + index);
    if (page == PAGE_SHAPE && SYNTH_IS_DRUM(track_sel))
        return (int)(SEQ_LK_SHAPE0 + index);
    if (page == PAGE_MIXER && index < 2u)
        return index ? SEQ_LK_SEND : SEQ_LK_VOL;
    if (page == PAGE_ENV && !SYNTH_IS_DRUM(track_sel))   /* (0.37) a synth track's ATTACK, DECAY, RELEASE */
        return (int)(SEQ_LK_ATTACK + index);
    if (page == PAGE_MOD && !SYNTH_IS_DRUM(track_sel) && index < 2u)   /* and LFO RATE, LFO DEPTH */
        return (int)(SEQ_LK_LFO_RATE + index);
    return -1;
}

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

/* (0.39) GLO's MIDI: a track's channel (0..15; -1 OFF), and whether the clock goes out or is followed */
static int midi_out_ch(uint32_t track)
{
    const uint32_t c = set.midi_ch[track % SYNTH_TRACKS];
    return c ? (int)c - 1 : -1;
}

static int clock_out(void) { return set.midi_clock == CLOCK_OUT || set.midi_clock == CLOCK_BOTH; }
static int clock_in(void) { return set.midi_clock == CLOCK_IN || set.midi_clock == CLOCK_BOTH; }

/* the tracks a note in on channel ch plays (a bit each): IN SELECTED, the one selected; CHANNELS, every track on ch,
 * or (none on it) the one selected */
static uint32_t midi_in_tracks(uint32_t ch)
{
    uint32_t to = 0;
    if (set.midi_in == MIDI_IN_CHANNELS)
        for (uint32_t t = 0; t < SYNTH_TRACKS; t++)
            if (set.midi_ch[t] == ch + 1u)
                to |= 1u << t;
    return to ? to : 1u << track_sel;
}

/* the notes in held (audio interrupt): where each went, so its note-off goes there too, whatever IN, a channel or the
 * track selected became meanwhile; bit 8 of to (MIDI_HELD_REC): recorded (into the track selected then) */
#define MIDI_HELD_N 32u
#define MIDI_HELD_REC (1u << SYNTH_TRACKS)
static uint8_t midi_held_ch[MIDI_HELD_N], midi_held_note[MIDI_HELD_N];
static uint16_t midi_held_to[MIDI_HELD_N];

static void midi_held_put(uint32_t ch, uint32_t note, uint32_t to)
{
    uint32_t free = MIDI_HELD_N;
    for (uint32_t i = 0; i < MIDI_HELD_N; i++) {
        if (midi_held_to[i] && midi_held_ch[i] == ch && midi_held_note[i] == note) {   /* (again: one note-off for both) */
            midi_held_to[i] |= (uint16_t)to;
            return;
        }
        if (!midi_held_to[i] && free == MIDI_HELD_N)
            free = i;
    }
    if (free < MIDI_HELD_N) {                /* (full: its note-off goes as a note would go then) */
        midi_held_ch[free] = (uint8_t)ch;
        midi_held_note[free] = (uint8_t)note;
        midi_held_to[free] = (uint16_t)to;
    }
}

static uint32_t midi_held_take(uint32_t ch, uint32_t note)
{
    for (uint32_t i = 0; i < MIDI_HELD_N; i++)
        if (midi_held_to[i] && midi_held_ch[i] == ch && midi_held_note[i] == note) {
            const uint32_t to = midi_held_to[i];
            midi_held_to[i] = 0;
            return to;
        }
    const uint32_t to = midi_in_tracks(ch);
    return to | (to >> track_sel & 1u ? MIDI_HELD_REC : 0u);
}

static void midi_in(void)                    /* audio interrupt: the packets that came in */
{
    uint32_t pkt;
    int port;
    while (plat_midi_in(&pkt, &port)) {
        uint32_t st = (pkt >> 8) & 0xFFu, d1 = (pkt >> 16) & 0x7Fu, d2 = pkt >> 24, ch = st & 0x0Fu;
        if (st != 0xF8u && st != 0xFEu) {    /* (clock and active sensing would flood the screen) */
            midi_last = pkt;
            midi_last_port = (uint32_t)port;
            midi_count++;
        }
        if ((st & 0xF0u) == 0x90u && d2) {   /* the tracks IN gives it (or their arps); recording live: recorded, if
                                              * the track selected plays it */
            const uint32_t to = midi_in_tracks(ch), rec = to >> track_sel & 1u;
            synth_audio_mute = 0u;
            for (uint32_t t = 0; t < SYNTH_TRACKS; t++)
                if (to >> t & 1u && !seq_arp_note(&seq, t, (uint8_t)d1, (uint8_t)d2))
                    synth_track_note_on(&synth, t, (int32_t)d1, d2);
            midi_held_put(ch, d1, to | (rec ? MIDI_HELD_REC : 0u));
            if (rec)
                rec_midi_put(d1, d2);
        }
        else if ((st & 0xF0u) == 0x80u || (st & 0xF0u) == 0x90u) {
            const uint32_t to = midi_held_take(ch, d1);
            for (uint32_t t = 0; t < SYNTH_TRACKS; t++)
                if (to >> t & 1u && !seq_arp_note(&seq, t, (uint8_t)d1, 0))
                    synth_track_note_off(&synth, t, (int32_t)d1);
            if (to & MIDI_HELD_REC)
                rec_midi_put(d1, 0);
        }
        else if (st == 0xF8u || st == 0xFAu || st == 0xFBu || st == 0xFCu) {   /* clock, start, continue, stop: the
                                                                              * sequencer follows them (CLOCK IN) */
            if (!clock_in())
                continue;
            if (st == 0xF8u)
                seq_midi_clock(&seq);
            else if (st == 0xFCu)
                seq.want = SEQ_WANT_STOP;
            else
                seq_midi_start(&seq, st == 0xFBu);
        }
        else if ((st & 0xF0u) == 0xC0u)      /* program change, any channel: 0..15 a pattern */
            midi_program = d1 + 1u;
    }
}

/* (0.39) a track's channel changed with notes of it still out: those off, on the channel they went out on */
static void seq_midi_flush(uint32_t track)   /* audio interrupt */
{
    for (uint32_t at = 0; at < 16u; at++)
        for (uint32_t b = 0; b < 8u; b++)
            if (seq_midi_sent[track][at] >> b & 1u) {
                plat_midi_send(0x08u | (0x80u | seq_midi_ch[track]) << 8 | (at << 3 | b) << 16, TO_USB);
                seq_midi_owed--;
            }
    memset(seq_midi_sent[track], 0, sizeof seq_midi_sent[track]);
}

/* a sequencer event, in the audio interrupt: to its track's voices, and out over USB MIDI on the track's channel
 * (GLO's MIDI, 0.39; OFF: none) while there is room, keeping room for the note-offs owed */
static void seq_apply(const seq_event_t *e)
{
    const uint32_t bit = 1u << (e->note & 7u), at = e->note >> 3, track = e->track % SYNTH_TRACKS;
    switch (e->type) {
    case SEQ_EV_ON: {
        const int ch = midi_out_ch(track);
        synth_audio_mute = 0u;
        synth_track_note_on(&synth, track, e->note, e->vel);
        if (ch != (int)seq_midi_ch[track]) {  /* (its channel changed: the notes still out off where they went) */
            seq_midi_flush(track);
            seq_midi_ch[track] = (uint8_t)(ch < 0 ? 0xFF : ch);
        }
        if (ch >= 0 && !(seq_midi_sent[track][at] & bit) && plat_midi_out_room(TO_USB) > seq_midi_owed + 2u) {
            plat_midi_send(0x09u | (0x90u | (uint32_t)ch) << 8 | (uint32_t)e->note << 16 | (uint32_t)e->vel << 24,
                           TO_USB);
            seq_midi_sent[track][at] |= (uint8_t)bit;
            seq_midi_owed++;
        }
        break;
    }
    case SEQ_EV_OFF:
        synth_track_note_off(&synth, track, e->note);
        if (seq_midi_sent[track][at] & bit) {   /* (on the channel its note-on went out on) */
            plat_midi_send(0x08u | (0x80u | seq_midi_ch[track]) << 8 | (uint32_t)e->note << 16, TO_USB);
            seq_midi_sent[track][at] &= (uint8_t)~bit;
            seq_midi_owed--;
        }
        break;
    case SEQ_EV_LOCK: {                      /* (0.35) a step's locks (or none): the track's knobs until its next */
        uint8_t v[SEQ_LK_PARAMS];
        const uint32_t m = seq_lock_mask(&seq.pat, track, e->note, v);
        for (uint32_t i = 0; i < SEQ_LK_PARAMS; i++)
            if (m >> i & 1u)
                lock_seq[track].val[i] = v[i];
        lock_seq[track].mask = (uint16_t)m;
        break;
    }
    case SEQ_EV_CLOCK:
    case SEQ_EV_START:
    case SEQ_EV_STOP:
        if (clock_out() && plat_midi_out_room(TO_USB) > seq_midi_owed) {   /* (GLO's MIDI: CLOCK OUT, 0.39) */
            const uint32_t st = e->type == SEQ_EV_CLOCK ? 0xF8u : e->type == SEQ_EV_START ? 0xFAu : 0xFCu;
            plat_midi_send(0x0Fu | st << 8, TO_USB);
        }
        break;
    default:
        break;
    }
}

static void render_span(int32_t *lr, uint32_t n, uint32_t master_q12, const grav_fx_params_t *fx);

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
    if (!seq.playing)                        /* (0.35) stopped: the steps' locks gone, the tracks' own knobs */
        for (uint32_t t = 0; t < SYNTH_TRACKS; t++)
            lock_seq[t].mask = 0;
    for (uint32_t t = 0; t < SYNTH_S_TRACKS; t++) {   /* (0.34) the arps, as the tracks' sounds have them */
        seq_arp_t *a = &seq.arp[t];
        a->rate = ARP_TICKS[arp_rate(t) % GRAV_ECHO_TIMES];
        a->octaves = (uint8_t)arp_octaves(t);
        a->gate = (uint8_t)arp_gate(t);
        a->hold = (uint8_t)arp_hold(t);
        a->mode = (uint8_t)arp_mode(t);
    }


    while (synth_event_queue_pop(&synth_queue, &event)) {
        switch (event.type) {
        case SYNTH_EVENT_NOTE_ON:            /* (0.34: to the track's arp, if it is on) */
            synth_audio_mute = 0u;
            if (!seq_arp_note(&seq, event.track, (uint8_t)clamp(event.note, 0, 127), (uint8_t)event.velocity))
                synth_track_note_on(&synth, event.track, event.note, event.velocity);
            break;
        case SYNTH_EVENT_NOTE_OFF:
            if (!seq_arp_note(&seq, event.track, (uint8_t)clamp(event.note, 0, 127), 0))
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

    const grav_fx_params_t fx = {
        .hall = (float)fx_params[FX_HALL] * 0.01f, .time = (float)fx_params[FX_HALL_TIME] * 0.01f,
        .tone = (float)fx_params[FX_TONE] * 0.01f, .diffuse = (float)fx_params[FX_DIFFUSE] * 0.01f,
        .mod = (float)fx_params[FX_MOD] * 0.01f, .echo = (float)fx_params[FX_ECHO] * 0.01f,
        .feedback = (float)fx_params[FX_FEEDBACK] * 0.01f, .echo_time = (uint8_t)fx_params[FX_ECHO_TIME],
        .quality = (uint8_t)fx_quality, .bpm = (float)seq_bpm(&seq)
    };

    /* the sequencer's events in this block: the sound rendered in spans between them, each applied where it falls
     * (on a 24-sample grid), not at the block's start */
    seq_event_t ev[SEQ_EVENTS];
    const uint32_t ne = seq_block(&seq, n, ev, SEQ_EVENTS);
    uint32_t pos = 0, k = 0;
#ifdef PLAT_USB_TRACKS
    int16_t *const trk = plat_usb_tracks();  /* (0.42) USB audio MULTI, recording: each track on its own too */
#else
    int16_t *const trk = 0;
#endif
    while (pos < n) {
        while (k < ne && ev[k].at <= pos)
            seq_apply(&ev[k++]);
        const uint32_t end = k < ne && ev[k].at < n ? ev[k].at : n;
        grav_track_out = trk ? trk + SYNTH_TRACKS * pos : 0;
        render_span(lr + 2u * pos, end - pos, master_q12, &fx);
        pos = end;
    }
    grav_track_out = 0;
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
        if (trk)
            memset(trk, 0, SYNTH_TRACKS * n * sizeof *trk);
    }
}

/* n samples of the voices (their LFO, the effect, MASTER), from where the last span left them */
static void render_span(int32_t *lr, uint32_t n, uint32_t master_q12, const grav_fx_params_t *fx)
{
    grav_sound_t sound[SYNTH_TRACKS];

    for (uint32_t t = 0; t < SYNTH_TRACKS; t++) {   /* each track's sound; (0.37) a synth track's own LFO and envelope,
                                                     * its steps' locks over them (at each span: a lock heard from its
                                                     * step's note on) */
        const int drum = SYNTH_IS_DRUM(t);
        float lfo_value = 0.0f;
        uint32_t lfo_dest = 3u;
        if (!drum) {
            lfo_set(&trk_lfo[t], 0.05f + (float)locked(t, SEQ_LK_LFO_RATE, trk_mod[t][0]) * 0.1995f,
                    (float)locked(t, SEQ_LK_LFO_DEPTH, trk_mod[t][1]) * 0.01f);
            lfo_value = lfo_process(&trk_lfo[t]);
            lfo_dest = lfo_destination(trk_mod[t][2]);
            synth_track_envelopes(&synth, t, (float)locked(t, SEQ_LK_ATTACK, trk_env[t][0]) * 0.02f,
                                  (float)locked(t, SEQ_LK_DECAY, trk_env[t][1]) * 0.02f, 0.7f,
                                  (float)locked(t, SEQ_LK_RELEASE, trk_env[t][2]) * 0.04f, 44100.0f);
        } else                               /* (a drum's voice: its outer envelope as one came before) */
            synth_track_envelopes(&synth, t, (float)ENV_DEFAULT[0] * 0.02f, (float)ENV_DEFAULT[1] * 0.02f, 0.7f,
                                  (float)ENV_DEFAULT[2] * 0.04f, 44100.0f);
        const uint32_t d = (t - SYNTH_S_TRACKS) % SYNTH_D_TRACKS;   /* (0.35: each knob as its steps lock it) */
        float timbre = (float)locked(t, SEQ_LK_OSC0, osc_params[t][0]) * 0.01f;
        float morph = (float)locked(t, SEQ_LK_OSC1, osc_params[t][1]) * 0.01f;
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
        sound[t].pitch = drum ? (float)(locked(t, SEQ_LK_TUNE, drum_tune[d]) - 50) * 0.5f :
                         lfo_dest == 2u ? lfo_value * 2.0f : 0.0f;
        sound[t].timbre = timbre;            /* (a drum: TONE) */
        sound[t].morph = morph;              /* (a drum: DECAY) */
        sound[t].harmonics = (float)locked(t, SEQ_LK_OSC2, osc_params[t][2]) / 100.0f;
        sound[t].send = (float)locked(t, SEQ_LK_SEND, set.send[t]) * 0.01f;   /* (0.26) the mixer's SEND */
        {
            const int32_t vol = locked(t, SEQ_LK_VOL, set.vol[t]);
            sound[t].gain = (float)(vol * vol) * 0.0001f;   /* (0.27) its fader, squared (100: 1, exactly) */
        }
        for (uint32_t i = 0; i < 3; i++)
            sound[t].shape[i] = drum ? (float)locked(t, SEQ_LK_SHAPE0 + i, drum_shape[d][i]) * 0.01f : 0.0f;
    }

    grav_engine_render_tracks(&grav_engine, lr, n, &synth, master_q12, sound, 0.0f, fx);

    for (uint32_t t = 0; t < SYNTH_S_TRACKS; t++)
        lfo_advance(&trk_lfo[t], n);
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
    s->fx_layout = FX_LAYOUT;
    for (int i = 0; i < 2; ++i)
        s->fx_reserved[i] = 0;
}

static void midi_defaults(settings_t *s)    /* (0.39) as before: S1..S4 on 1..4, D1..D4 on 5..8; any in; clock both */
{
    for (uint32_t t = 0; t < SYNTH_TRACKS; t++)
        s->midi_ch[t] = (uint8_t)(t + 1u);
    s->midi_in = MIDI_IN_SELECTED;
    s->midi_clock = CLOCK_BOTH;
    s->usb_audio = s->midi_reserved = 0;
}

static void settings_defaults(void)
{
    set.magic = SET_MAGIC;
    midi_defaults(&set);
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
    for (uint32_t t = 0; t < SYNTH_S_TRACKS; t++) {   /* (0.37) */
        memcpy(set.trk_mod[t], MOD_DEFAULT, 3);
        memcpy(set.trk_env[t], ENV_DEFAULT, 3);
    }
    set.voices = VOICES_DEFAULT;
    set.velocity_off = 0;
    set.sky = SKY_NIGHT;
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
    for (uint32_t t = 0; t < SYNTH_S_TRACKS; t++)   /* (0.37) each track's; S1's where older readers look */
        for (uint32_t i = 0; i < 3; i++) {
            set.trk_mod[t][i] = (uint8_t)trk_mod[t][i];
            set.trk_env[t][i] = (uint8_t)trk_env[t][i];
        }
    memcpy(set.mod, set.trk_mod[0], 3);
    memcpy(set.env, set.trk_env[0], 3);
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
           n == SET_SIZE_021 || n == SET_SIZE_025 || n == SET_SIZE_026 || n == SET_SIZE_036 ||
           n == SET_SIZE_037 || n == (int)sizeof(settings_t);
}

/* a record an older version saved (loaded: its size): what it lacks at its defaults */
static void settings_upgrade(settings_t *s, int loaded)
{
    if (loaded <= SET_SIZE_021)              /* (before 0.22: no 6-op FM controls) */
        for (uint32_t t = 0; t < SYNTH_TRACKS; t++) grav_controls_init(&s->controls[t]);
    if (loaded == SET_SIZE_012)              /* (before 0.13, before the effect: its knobs at rest) */
        fx_defaults(s);
    if (s->fx_layout != FX_LAYOUT) {         /* (before 0.32, Beads' knobs: its REVERB HALL's AMOUNT, the rest at
                                              * rest, QUALITY kept) */
        const uint8_t reverb = s->fx[FX_OLD_REVERB], quality = s->fx_quality;
        fx_defaults(s);
        s->fx[FX_HALL] = reverb;
        s->fx_quality = quality;
    }
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
    if (loaded <= SET_SIZE_036)              /* (before 0.37: one LFO and envelope for all: each track takes them) */
        for (uint32_t t = 0; t < SYNTH_S_TRACKS; t++) {
            memcpy(s->trk_mod[t], s->mod, 3);
            memcpy(s->trk_env[t], s->env, 3);
        }
    if (loaded <= SET_SIZE_037)              /* (before 0.39: MIDI as it was, the tracks on 1..8) */
        midi_defaults(s);
    if (s->usb_audio > 1u)                   /* (0.42) */
        s->usb_audio = 0;
    if (s->theme > NPALETTES)                /* (a byte reserved before 0.25.1) */
        s->theme = 0;
    if (s->sky == 0 || s->sky > SKY_DAY)     /* (a byte reserved before 0.29; 0.29..0.30.1 kept DAY, their default, as
                                              * 0: NIGHT now, as it comes) */
        s->sky = SKY_NIGHT;
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
        if (s->trk_engine[t] >= GRAV_ENGINE_COUNT || s->trk_voices[t] > 4u || !percent_ok(s->trk_osc[t], 3) ||
            !percent_ok(s->trk_mod[t], 3) || !percent_ok(s->trk_env[t], 3))
            return 0;
    }
    for (uint32_t t = 0; t < SYNTH_TRACKS; t++)   /* (0.39) */
        if (s->midi_ch[t] > 16u)
            return 0;
    if (s->midi_in > MIDI_IN_CHANNELS || s->midi_clock > CLOCK_BOTH)
        return 0;
    return voices <= SYNTH_POLY_VOICES && percent_ok(s->osc, 3) && percent_ok(s->mod, 3) && percent_ok(s->env, 3) &&
           percent_ok(s->fx, FX_N) && s->fx[FX_ECHO_TIME] < GRAV_ECHO_TIMES && s->voices <= SYNTH_POLY_VOICES &&
           s->velocity_off <= 1u && s->fx_quality < GRAV_FX_QUALITIES;
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

    for (uint32_t t = 0; t < SYNTH_S_TRACKS; t++) {   /* (0.37) each track's LFO and envelope */
        for (uint32_t i = 0; i < 3; i++) {
            trk_mod[t][i] = set.trk_mod[t][i];
            trk_env[t][i] = set.trk_env[t][i];
        }
        lfo_init(&trk_lfo[t], 44100.0f);
    }
    for (int i = 0; i < FX_N; ++i)
        fx_params[i] = set.fx[i];
    fx_quality = set.fx_quality;
    synth_set_tracks(&synth, set.trk_voices);   /* (before the audio starts: no queue needed) */
#ifdef PLAT_USB_TRACKS
    plat_usb_audio_multi(set.usb_audio);     /* (0.42; before USB starts: presented so from the first) */
#endif

    seq_init(&seq);
    seq.bpm = set.bpm;
    seq.mute = set.mute;                     /* (0.26) the mixer's mutes */
    bank_init(&bank);                        /* the patterns; their stale copies erased now, before the sound */
    while (bank_tidy(&bank))
        ;
    copy_from = clip_from = -1;              /* (0.40: a copy kept, its number not) */
    if (bank_count(&bank, BANK_LIVE) == (bank_pattern(&bank, BANK_CLIP) ? 1u : 0u)) {   /* (the copy is no pattern) */ /* none yet: 0.17's pattern and before (OBJ_SEQ, OBJ_DRUMS) is pattern 1 */
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
    int32_t turns[NE];                   /* detents since boot, per encoder */
    uint32_t turned_at[NE];              /* when each was last turned: it shows lit for a moment */
    uint32_t cpu, xruns, cpu_at;         /* the render's load (% of its time) and late renders, as last shown */
    uint8_t redraw;
    uint32_t debug_at, debug_xruns_base;
    /* the sequencer's (seq) */
    uint8_t step_mode;                   /* SEQ: the white keys are the steps of view_page (on SEQ's page alone):
                                          * 1 every track, 2 (SEQ again, 0.31) the selected track alone */
    uint8_t roll_lo;                     /* (0.31) the piano roll's lowest note (0xFF: found again) */
    uint32_t sweep_at;                   /* (0.31) SAVE tapped, when: the white keys light first to last */
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
    uint8_t key_ch[NKEYS];               /* (0.39) and the MIDI channel its note-on went out on (its note-off there) */
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
    uint32_t key_midi;                   /* (0.34) the keys whose notes went out over MIDI (not to an arp: its notes go) */
    uint8_t logo;                        /* (0.36.1) the logo at power-on still shown (until LOGO_MS, or a touch) */
    uint32_t arp_sig, arp_at;            /* (0.34) the ARP page: what its ladder showed, when it last changed; */
    uint8_t arp_band;                    /* its band's (held, HOLD) */
    uint8_t lock_mode;                   /* (0.35) EDIT: LOCK mode: the white keys the steps on every page, the knobs
                                          * lock the ones held (recording live: the one playing) */
    uint8_t edit_down, edit_used;        /* EDIT held; a step's locks cleared meanwhile (no mode change) */
    uint16_t lock_held;                  /* the view page's steps held in LOCK mode, a bit each */
    uint32_t key_lock;                   /* the keys pressed as LOCK's steps (let go as such) */
    uint32_t key_mix;                    /* (0.26) the keys pressed on the mixer (mutes, tracks: let go as such) */
    uint32_t key_tap;                    /* (0.38) the keys tapped on TEMPO (let go as such) */
    uint32_t tap_at[TAP_TAPS];           /* the last taps, oldest first (ms) */
    uint8_t tap_n;                       /* how many of them count */
    uint8_t mixer_from;                  /* the page SEL opened the mixer on (SEL again: back to it) */
    uint8_t sel_down;                    /* SEL held (let go: the mixer) */
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
    uint8_t copy_req, bank_copy;         /* (0.40) HOME + OCT-: the pattern to copy (into BANK_CLIP); its copy being
                                          * written */
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
static void track_mod_set(uint32_t t, const uint8_t *lfo, const uint8_t *env)   /* (0.37) a synth track's LFO and
                                                                                 * envelope */
{
    if (SYNTH_IS_DRUM(t))
        return;
    for (uint32_t i = 0; i < 3; i++) {
        trk_mod[t][i] = lfo[i];
        trk_env[t][i] = env[i];
    }
}

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
    if (!p.drum && p.mod_set == PRESET_MOD)   /* (0.37) its LFO and envelope (one saved before: the track's stay) */
        track_mod_set(t, p.lfo, p.env);
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
    uint8_t engine, edited, osc[3], tune, shape[3], lfo[3], env[3];   /* the sound as it opened (OCT-) */
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
    for (uint32_t i = 0; i < 3 && !drum; i++) {   /* (0.37) */
        pb.lfo[i] = (uint8_t)trk_mod[t][i];
        pb.env[i] = (uint8_t)trk_env[t][i];
    }
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
    track_mod_set(t, pb.lfo, pb.env);
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

static void sky_turn(int32_t e)              /* (0.28) SCREEN's K2: OFF, DAY, NIGHT, left to right */
{
    static const uint8_t ORDER[3] = {SKY_OFF, SKY_DAY, SKY_NIGHT};
    const int32_t at = set.sky == SKY_OFF ? 0 : set.sky == SKY_DAY ? 1 : 2;
    const uint8_t sky = ORDER[clamp(at + (e > 0 ? 1 : -1), 0, 2)];
    if (sky != set.sky) {
        set.sky = sky;
        set_dirty = 1;
    }
    app_say("SKY", SKY_NAME[set.sky]);
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

/* (0.42) GLO's USB: K1 AUDIO, STEREO (the mix to the computer on 1-2, and its sound through the FM-1) or MULTI (the mix
 * on 1-2 and each track on 3..10, dry; its sound through the FM-1 as ever). USB reconnects (about a second) */
static void usb_turn(uint32_t index, int32_t e)
{
    if (index)
        return;
#ifdef PLAT_USB_TRACKS
    const uint8_t multi = e > 0;
    if (multi != set.usb_audio) {
        set.usb_audio = multi;
        set_dirty = 1;
        plat_usb_audio_multi(multi);
    }
#endif
    app_say("USB AUDIO", set.usb_audio ? "MULTI" : "STEREO");
}

static void channel_text(uint32_t c, char *t)   /* (0.39) a track's MIDI channel: 1..16, or OFF */
{
    t[0] = 0;
    if (c)
        int_text((int32_t)c, t);
    else
        text_cat(t, "OFF");
}

/* (0.39) GLO's MIDI: K1 the selected track's CHANNEL (OFF, 1..16), K2 IN (SELECTED, CHANNELS), K3 CLOCK (OFF, IN, OUT,
 * IN+OUT). CLOCK OUT off while the song plays by its own: a stop sent; CLOCK IN off while it follows one: stopped */
static void midi_turn(uint32_t index, int32_t e)
{
    char a[24], t[8];
    if (index == 0u) {
        const uint8_t c = (uint8_t)clamp((int32_t)set.midi_ch[track_sel] + e, 0, 16);
        if (c != set.midi_ch[track_sel]) {
            set.midi_ch[track_sel] = c;
            set_dirty = 1;
        }
        a[0] = 0;
        text_cat(a, TRACK_NAME[track_sel]);
        text_cat(a, " CHANNEL");
        channel_text(c, t);
        app_say(a, t);
    } else if (index == 1u) {
        const uint8_t in = e > 0 ? MIDI_IN_CHANNELS : MIDI_IN_SELECTED;
        if (in != set.midi_in) {
            set.midi_in = in;
            set_dirty = 1;
        }
        app_say("MIDI IN", MIDI_IN_NAME[set.midi_in]);
    } else {
        const int out = clock_out(), in = clock_in();
        const uint8_t m = (uint8_t)clamp((int32_t)set.midi_clock + (e > 0 ? 1 : -1), CLOCK_OFF, CLOCK_BOTH);
        if (m != set.midi_clock) {
            set.midi_clock = m;
            set_dirty = 1;
        }
        if (out && !clock_out() && seq.playing && !seq.ext)
            plat_midi_send(0x0Fu | 0xFCu << 8, TO_USB);
        if (in && !clock_in() && seq.ext)
            seq.want = SEQ_WANT_STOP;
        app_say("CLOCK", CLOCK_NAME[set.midi_clock]);
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
    if (page == PAGE_MOD)                    /* (0.37) the selected track's (a drum's: none) */
        return SYNTH_IS_DRUM(track_sel) ? &no_param : &trk_mod[track_sel][index % 3u];
    if (page == PAGE_ENV)
        return SYNTH_IS_DRUM(track_sel) ? &no_param : &trk_env[track_sel][index % 3u];
    static const uint8_t FX_SLOT[3][3] = {{FX_HALL, FX_HALL_TIME, FX_TONE}, {FX_N, FX_DIFFUSE, FX_MOD},
                                          {FX_ECHO, FX_ECHO_TIME, FX_FEEDBACK}};
    const uint32_t slot = FX_SLOT[(page - PAGE_FX_HALL) % 3u][index % 3u];
    return slot < FX_N ? &fx_params[slot] : &fx_quality;   /* (TAPE's K1: QUALITY) */
}

static int32_t param_value(uint32_t page, uint32_t index)
{
    if (page == PAGE_FX_TAPE && index == 0) return fx_quality;
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
    return (page == PAGE_OUTPUT && index == 0u && !SYNTH_IS_DRUM(track_sel)) ||
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
    } else if (page == PAGE_FX_TAPE && index == 0) {
        static const char *const quality[] = {"DIGITAL","VINTAGE","CLEAN","DIRTY"};
        t[0] = 0; text_cat(t, quality[value % GRAV_FX_QUALITIES]);
    } else if (page == PAGE_FX_ECHO && index == 1) {   /* (0.32) ECHO's TIME: a note value */
        t[0] = 0; text_cat(t, grav_echo_time_name((uint32_t)value));
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

/* (0.34) the ARP pages' knobs: the selected synth track's arp, its sound's (marked * as its other edits) */
static void arp_turn(uint32_t index, int32_t e)
{
    const uint32_t t = track_sel;
    if (SYNTH_IS_DRUM(t)) {
        app_say("ARP", "S1-S4 ONLY");
        return;
    }
    grav_controls_t c = set.controls[t];
    uint32_t mode = c.arp[0] & 7u, octaves = c.arp[0] >> 3 & 3u, hold = c.arp[0] >> 5 & 1u;
    uint32_t rate = arp_rate(t), gate = arp_gate(t);
    if (active_page == PAGE_ARP) {
        if (index == 0u)
            mode = (uint32_t)clamp((int32_t)mode + e, 0, SEQ_ARP_MODES - 1);
        else if (index == 1u)
            rate = (uint32_t)clamp((int32_t)rate + e, 0, GRAV_ECHO_TIMES - 1);
        else
            octaves = (uint32_t)clamp((int32_t)octaves + e, 0, 3);
    } else if (index == 0u)
        gate = (uint32_t)clamp((int32_t)gate + e, 5, 100);
    else if (index == 1u)
        hold = e > 0;
    else
        return;
    c.arp[0] = (uint8_t)(mode | octaves << 3 | hold << 5);
    c.arp[1] = (uint8_t)(rate + 1u);
    c.arp[2] = (uint8_t)gate;
    if (memcmp(c.arp, set.controls[t].arp, sizeof c.arp)) {
        const uint32_t f = plat_lock();
        memcpy(set.controls[t].arp, c.arp, sizeof c.arp);
        plat_unlock(f);
        ui.preset_edited |= (uint8_t)(1u << t);
        set_dirty = 1;
    }
    char v[12];
    v[0] = 0;
    if (active_page == PAGE_ARP && index == 0u)
        text_cat(v, ARP_MODE_NAME[mode]);
    else if (active_page == PAGE_ARP && index == 1u)
        text_cat(v, grav_echo_time_name(rate));
    else if (active_page == PAGE_ARP)
        int_text((int32_t)octaves + 1, v);
    else if (index == 0u) {
        int_text((int32_t)gate, v);
        text_cat(v, "%");
    } else
        text_cat(v, hold ? "ON" : "OFF");
    app_say(PARAM_NAME[active_page][index], v);
}

/* TAPE's K1: QUALITY, from DIGITAL to DIRTY TAPE (Beads' media) */
static void quality_turn(int32_t e)
{
    int32_t q = clamp(fx_quality + e, 0, GRAV_FX_QUALITIES - 1);
    if (q != fx_quality) {
        fx_quality = q;
        set_dirty = 1;
    }
    app_say("QUALITY", QUALITY_NAME[fx_quality]);
}

static int fx_wanted(void)               /* the effect on: HALL's AMOUNT or ECHO's up */
{
    return fx_params[FX_HALL] > 0 || fx_params[FX_ECHO] > 0;
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

/* (0.35) LOCK mode (EDIT): the white keys the steps of the view page (OCT- / OCT+ the others) on every page; a step
 * held, the knobs that can lock (OSC's, a drum's DRUM and SHAPE, the mixer's VOL and SEND) lock it, its locks heard
 * while it is held; recording live, they lock the step playing (the knobs' moves recorded). A step off can hold locks
 * too: its locks alone, no note. EDIT held and a white key: that step's locks cleared */
static uint32_t lock_first(void)            /* the first step held */
{
    return ui.view_page * SEQ_PAGE + (uint32_t)__builtin_ctz(ui.lock_held ? ui.lock_held : 1u);
}

static void lock_hear(void)                  /* the step held first: its locks heard (none held: the others again) */
{
    for (uint32_t t = 0; t < SYNTH_TRACKS; t++)
        if (t != track_sel || !ui.lock_held)
            lock_heard[t].mask = 0;
    if (!ui.lock_held)
        return;
    uint8_t v[SEQ_LK_PARAMS];
    const uint32_t m = seq_lock_mask(&seq.pat, track_sel, lock_first(), v);
    for (uint32_t i = 0; i < SEQ_LK_PARAMS; i++)
        if (m >> i & 1u)
            lock_heard[track_sel].val[i] = v[i];
    lock_heard[track_sel].mask = (uint16_t)m;
}

static void lock_mode_toggle(void)
{
    ui.lock_mode = !ui.lock_mode;
    ui.lock_held = 0;
    lock_hear();
    app_say("LOCK", ui.lock_mode ? "ON" : "OFF");
    ui.redraw = 1;
}

static void lock_clear_step(uint32_t s)
{
    char t[8];
    if (!seq_lock_mask(&seq.pat, track_sel, s, 0))
        return;
    uint32_t f = plat_lock();
    seq_lock_clear(&seq.pat, track_sel, (int32_t)s);
    seq.edits++;
    plat_unlock(f);
    lock_hear();
    int_text((int32_t)s + 1, t);
    app_say("LOCKS CLEARED", t);
    ui.redraw = 1;
}

static void lock_key(uint32_t i, int down)
{
    const uint32_t s = ui.view_page * SEQ_PAGE + i;
    char t[16];
    if (down) {
        ui.lock_held |= (uint16_t)(1u << i);
        if (active_page == PAGE_SEQ && ui.step_mode)   /* (step mode: its NOTE, VELOCITY, LENGTH on the knobs) */
            ui.step_held = (int8_t)s;
        int_text((int32_t)s + 1, t);
        const uint32_t m = seq_lock_mask(&seq.pat, track_sel, s, 0);
        if (m) {
            char n[4];
            int_text(__builtin_popcount(m), n);
            text_cat(t, " LOCKS ");
            text_cat(t, n);
        }
        app_say("STEP", t);
    } else {
        ui.lock_held &= (uint16_t)~(1u << i);
        if (ui.step_held == (int8_t)s)
            ui.step_held = -1;
    }
    lock_hear();
    ui.redraw = 1;
}

/* a knob turned in LOCK mode: its lock on each step held (from its value there, or the track's own), or recording
 * live on the step playing (heard at once) */
static void lock_turn(uint32_t p, uint32_t index, int32_t e)
{
    const uint32_t t = track_sel;
    const int32_t own = lock_own(t, p);
    int32_t v = own, full = 0;
    char a[24], b[16];
    uint32_t f = plat_lock();
    if (ui.lock_held) {
        for (uint32_t i = 0; i < SEQ_PAGE; i++)
            if (ui.lock_held >> i & 1u) {
                const uint32_t s = ui.view_page * SEQ_PAGE + i;
                const int cur = seq_lock_get(&seq.pat, t, s, p);
                v = clamp((cur >= 0 ? cur : own) + e, 0, 100);
                full |= seq_lock_set(&seq.pat, t, s, p, v) < 0;
            }
    } else {
        const int32_t s = seq.step[t];
        if (s < 0) {
            plat_unlock(f);
            return;
        }
        const int cur = seq_lock_get(&seq.pat, t, (uint32_t)s, p);
        v = clamp((cur >= 0 ? cur : own) + e, 0, 100);
        full = seq_lock_set(&seq.pat, t, (uint32_t)s, p, v) < 0;
        lock_seq[t].val[p] = (uint8_t)v;     /* (heard now, as the step plays) */
        lock_seq[t].mask |= (uint16_t)(1u << p);
    }
    seq.edits++;
    plat_unlock(f);
    lock_hear();
    if (full) {
        app_say("LOCKS", "FULL");
        return;
    }
    a[0] = 0;
    text_cat(a, "LOCK ");
    if (active_page == PAGE_OSC || active_page == PAGE_SHAPE) {
        text_cat(a, param_name(active_page, index));
        param_text(active_page, index, v, b);
    } else {                                 /* (the mixer's VOL, SEND) */
        text_cat(a, PARAM_NAME[active_page][index]);
        int_text(v, b);
        if (index)
            text_cat(b, "%");
    }
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
            lock_clear_step(s);              /* (0.35: a step taken off, its locks with it) */
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
static void pattern_queue(uint32_t n)        /* the next one (the one playing: none) */
{
    uint32_t f = plat_lock();
    seq.next = n == seq.pattern ? SEQ_NO_PATTERN : (uint8_t)n;
    seq.next_ready = 0;
    plat_unlock(f);
}

static void pattern_pick(int32_t n)
{
    char t[8];
    n = clamp(n, 0, (int32_t)BANK_PATTERNS - 1);
    chain.n = 0;                             /* (0.41: one pattern picked by hand, the chain gone) */
    pattern_queue((uint32_t)n);
    ui.patterns_until = plat_ms() + 1500u;
    ui.redraw = 1;
    int_text(n + 1, t);
    if (seq.playing)
        app_say(seq.next != SEQ_NO_PATTERN ? "NEXT PATTERN" : "PATTERN", t);
}

/* (0.40) HOME held: OCT- copies the pattern playing (into the bank's copy slot, BANK_CLIP: kept across a restart),
 * OCT+ pastes the copy into it, REC clears it. A paste or a clear is heard at once, and saved as any edit */
static void pattern_text(int32_t n, char *t)    /* "PATTERN 3" (n < 0: "PATTERN") */
{
    char k[8];
    t[0] = 0;
    text_cat(t, "PATTERN");
    if (n >= 0) {
        int_text(n + 1, k);
        text_cat(t, " ");
        text_cat(t, k);
    }
}

static void pattern_copy(void)
{
    char t[16];
    ui.copy_req = 1;
    copy_from = (int8_t)seq.pattern;
    pattern_text(seq.pattern, t);
    app_say("COPY", t);
    ui.redraw = 1;
}

static void pattern_replaced(void)           /* (the tracks' locks heard so far: none, the new steps bring theirs) */
{
    for (uint32_t t = 0; t < SYNTH_TRACKS; t++)
        lock_seq[t].mask = 0;
    seq.edits++;
    ui.step_held = -1;
    ui.redraw = 1;
}

static void pattern_paste(void)
{
    char t[16];
    if (ui.copy_req || ui.bank_copy) {       /* (the copy still on its way into the flash: a moment) */
        app_say("PASTE", "WAIT");
        return;
    }
    const seq_pattern_t *src = bank_pattern(&bank, BANK_CLIP);
    if (!src) {
        app_say("PASTE", "NOTHING COPIED");
        return;
    }
    uint32_t f = plat_lock();
    seq_replace(&seq, src);
    pattern_replaced();
    plat_unlock(f);
    pattern_text(clip_from, t);
    app_say("PASTE", t);
}

static void pattern_clear(void)
{
    char t[16];
    uint32_t f = plat_lock();
    seq_replace(&seq, 0);
    pattern_replaced();
    plat_unlock(f);
    pattern_text(seq.pattern, t);
    app_say("CLEAR", t);
}

/* (0.41) HOME let go after two or more white keys: the chain, from the first (picked as the keys went: stopped, in
 * at once; playing, queued) */
static void chain_start(void)
{
    char t[3 * CHAIN_MAX + 1], k[4];
    memcpy(chain.at, chain.build, chain.bn);
    chain.n = chain.bn;
    chain.playing = seq.playing;
    uint32_t f = plat_lock();
    chain.turns = seq.turns;
    chain.pos = !seq.playing || (seq.pattern == chain.at[0] && seq.next == SEQ_NO_PATTERN) ? 0 : -1;
    plat_unlock(f);
    t[0] = 0;
    for (uint32_t i = 0; i < chain.n; i++) {
        int_text(chain.at[i] + 1, k);
        if (i)
            text_cat(t, " ");
        text_cat(t, k);
    }
    app_say("CHAIN", t);
    ui.redraw = 1;
}

/* each frame: at each pattern's end the chain's next one has come in; the one after it queued while playing;
 * stopped, the chain over (0.41.1; before, back to its first): the pattern it was on stays, nothing queued */
static void chain_update(void)
{
    uint32_t f = plat_lock();
    const uint32_t turns = seq.turns, next = seq.next, playing = seq.playing;
    plat_unlock(f);
    if (!chain.n) {
        chain.turns = turns;
        chain.playing = (uint8_t)playing;
        return;
    }
    if (chain.playing && !playing) {         /* stopped: the chain over */
        chain.n = 0;
        chain.playing = 0;
        pattern_queue(seq.pattern);          /* (its next one no longer queued) */
        app_say("CHAIN", "OFF");
        ui.redraw = 1;
        return;
    }
    chain.playing = (uint8_t)playing;
    if (turns != chain.turns) {              /* an end: the next one in (unless it could not come in yet) */
        if (next == SEQ_NO_PATTERN)
            chain.pos = (int8_t)((chain.pos + (int32_t)(turns - chain.turns)) % (int32_t)chain.n);
        chain.turns = turns;
        ui.redraw = 1;
    }
    if (playing && chain.pos >= 0) {
        const uint32_t want = chain.at[((uint32_t)chain.pos + 1u) % chain.n];
        if (want != next && !(want == seq.pattern && next == SEQ_NO_PATTERN))
            pattern_queue(want);
    }
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
    chain_update();                          /* (0.41) */
    if (midi_program) {                      /* a program change in: 1..16 the patterns */
        const uint32_t program = midi_program - 1u;
        midi_program = 0;
        if (program < BANK_PATTERNS)
            pattern_pick((int32_t)program);
    }
    if (bank_busy(&bank)) {
        const int r = bank_step(&bank, &seq.pat, 8u);   /* (each page a moment with interrupts off) */
        if (r > 0 && ui.bank_copy) {         /* (0.40) the copy: ready to paste */
            ui.bank_copy = 0;
            clip_from = copy_from;
            ui.redraw = 1;
        } else if (r > 0) {
            seq.saved = ui.save_gen;
            ui.redraw = 1;
        } else if (r < 0 && ui.bank_copy) {
            ui.bank_copy = 0;
            app_say("COPY", "FAILED");
        } else if (r < 0) {
            ui.save_req = 0;
            app_say("SAVE", "FAILED");
        }
    } else if (ui.copy_req) {                /* (0.40) HOME + OCT-: the pattern as it is, into the copy's slot */
        if (bank_begin(&bank, BANK_CLIP) == 0) {
            ui.copy_req = 0;
            ui.bank_copy = 1;
            ui.full_said = 0;
        } else if (!seq.playing) {
            bank_tidy(&bank);                /* stopped: room made now */
        } else if (!ui.full_said) {
            ui.full_said = 1;
            app_say("COPY", "STOP TO COPY");
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
        if (seq.next != SEQ_NO_PATTERN && !seq.playing && !(chain.n && chain.playing)) {   /* stopped: the next one in
                                                       * now (a chain's: not before it has seen the stop, 0.41.1) */
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
        ui.roll_lo = 0xFF;
        ui.lock_held = 0;                    /* (0.35) */
        lock_hear();
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
        fx_params[FX_HALL] = clamp_percent(fx_params[FX_HALL] + e);   /* (0.32: HALL's AMOUNT; DRY/WET before) */
        set_dirty = 1;
        int_text(fx_params[FX_HALL], t);
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
    seq_lock_clear(&seq.pat, track_sel, -1); /* (0.35: its locks too) */
    seq.skip[track_sel] = 0;
    seq.edits++;
    plat_unlock(f);
    ui.step_held = -1;
    app_say("CLEAR", TRACK_NAME[track_sel]);
}

static uint32_t page_button(uint32_t page)   /* the button that opens a page */
{
    return page == PAGE_SEQ || page == PAGE_TEMPO || IS_SOUND_PAGE(page) ? B_HOME : page == PAGE_MOD ? B_LFO :
           page == PAGE_ENV ? B_ENV :
           page == PAGE_GLO || page == PAGE_MIDI || page == PAGE_USB || page == PAGE_SCREEN || page == PAGE_DEBUG ? B_GLO :
           page == PAGE_MIXER ? B_SEL :
           IS_ARP_PAGE(page) ? B_ARP : B_FX;
}

/* its blink's period: the first of a button's pages slow, the next faster (FX: HALL, TAPE, ECHO) */
static uint32_t page_period(uint32_t page)
{
    return page == PAGE_DEBUG ? 125u :       /* (GLO's fifth, 0.42) */
           page == PAGE_USB ? 375u :         /* (GLO's third, 0.42) */
           page == PAGE_TEMPO || page == PAGE_MIDI || page == PAGE_FX_TAPE || page == PAGE_ARP_2 ? 500u :
           page == PAGE_OSC || page == PAGE_FX_ECHO || page == PAGE_SHAPE || page == PAGE_SCREEN ? 250u : 1000u;
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
        if (st >= 0 && (TRK.step[st].on || seq_lock_mask(&seq.pat, track_sel, (uint32_t)st, 0))) {
            uint32_t f = plat_lock();
            TRK.step[st].on = 0;
            seq_lock_clear(&seq.pat, track_sel, st);   /* (0.35: its locks too) */
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

#define SWEEP_MS 35u                         /* (0.31) SAVE's sweep: a white key every 35 ms, then all 250 ms more */
#define SWEEP_HOLD 250u

static uint32_t save_sweep(uint32_t now)     /* the white keys SAVE's sweep lights now, first to last (0: none) */
{
    const uint32_t since = now - ui.sweep_at, n = since / SWEEP_MS + 1u;
    if (!ui.sweep_at || since >= SWEEP_MS * 16u + SWEEP_HOLD)
        return 0;
    return n < 16u ? n : 16u;
}

static void lock_lights(key_lights_t *l, uint32_t now)   /* (0.35) LOCK: a step held or locked lit, one on mid, the
                                                           * rest dim; black 1..8 the tracks, the selected lit */
{
    for (uint32_t i = 0; i < SEQ_PAGE; i++) {
        const uint32_t s = ui.view_page * SEQ_PAGE + i, bit = 1u << WHITE_KEYS[i];
        if ((ui.lock_held >> i & 1u) || seq_lock_mask(&seq.pat, track_sel, s, 0))
            l->lit |= bit;
        else if (TRK.step[s].on && s < TRK.length)
            l->mid |= bit;
        else
            l->dim |= bit;
    }
    for (uint32_t t = 0; t < SYNTH_TRACKS; t++) {
        const uint32_t bit = 1u << BLACK_KEYS[t];
        if (t == track_sel || now - ui.hit_at[t] < 80u)
            l->lit |= bit;
        else
            l->mid |= bit;
    }
}

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
 * unsaved, SEQ in step mode, FX with the effect on) and the keys */
static void lights(uint32_t btn, uint32_t keys)
{
    const uint32_t now = plat_ms(), play = 1u << B_PLAY;
    rec_follow(now);
    seq_follow(now);
    uint32_t lit = (btn & ~play) | (set_dirty || preset_dirty || seq.edits != seq.saved ? 1u << B_SAVE : 0u) |
                   (ui.step_mode ? 1u << B_SEQ : 0u), mid = 0;
    if (grav_fx_on() && !IS_FX_PAGE(active_page))
        lit |= 1u << B_FX;
    if (seq_arp_on(&seq, track_sel) && !IS_ARP_PAGE(active_page))   /* (0.34) the selected track's arp on */
        lit |= 1u << B_ARP;
    if (ui.lock_mode)                        /* (0.35) LOCK mode */
        lit |= 1u << B_EDIT;
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
    if (ui.preset_down || ui.home_down || ui.lock_mode || ui.step_mode || active_page == PAGE_MIXER) {
        k.lit = k.mid = 0;
        if (ui.preset_down)
            slot_lights(&k);
        else if (ui.home_down)
            pattern_lights(&k, now);
        else if (ui.lock_mode)
            lock_lights(&k, now);
        else if (ui.step_mode)
            step_lights(&k, now);
        else
            mixer_lights(&k, now);
    }
    {
        const uint32_t n = save_sweep(now);
        for (uint32_t i = 0; n && i < 16u; i++) {   /* (0.31) SAVE tapped: lit first to last, then all a moment */
            const uint32_t bit = 1u << WHITE_KEYS[i];
            k.lit &= ~bit;
            k.mid &= ~bit;
            k.dim &= ~bit;
            if (i < n)
                k.lit |= bit;
            else
                k.dim |= bit;
        }
        if (!n)
            ui.sweep_at = 0;
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
        fx_page = active_page == PAGE_FX_HALL ? PAGE_FX_TAPE : active_page == PAGE_FX_TAPE ? PAGE_FX_ECHO :
                  PAGE_FX_HALL;           /* (0.32: HALL, TAPE, ECHO) */
    if (k == B_HOME && ui.step_mode) {       /* from step mode (every track): HOME's own view, the one track */
        show_home();
        app_say("HOME", TRACK_NAME[track_sel]);
        return;
    }
    if (k == B_HOME) {                       /* (0.36.2) HOME's terricons, TEMPO, the sound's pages, round */
        if (active_page == PAGE_SEQ) active_page = home_page = PAGE_TEMPO;
        else if (active_page == PAGE_TEMPO) sound_activate(0);
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
        glo_page = active_page == PAGE_GLO ? PAGE_MIDI : active_page == PAGE_MIDI ? PAGE_USB :   /* (0.39, 0.42) */
                   active_page == PAGE_USB ? PAGE_SCREEN : active_page == PAGE_SCREEN ? PAGE_DEBUG :
                   active_page == PAGE_DEBUG ? PAGE_GLO : glo_page;
    if (k == B_ARP && IS_ARP_PAGE(active_page))   /* (0.34) ARP again: its other page */
        arp_page = active_page == PAGE_ARP ? PAGE_ARP_2 : PAGE_ARP;
    active_page = k == B_LFO  ? PAGE_MOD :
                  k == B_ENV  ? PAGE_ENV :
                  k == B_GLO  ? glo_page :
                  k == B_ARP  ? arp_page : fx_page;
    app_say("PAGE", page_name(active_page));
    ui.redraw = 1;
}

/* the keys pressed as something other than notes (a preset slot, a pattern, a mute or a solo, a step or a track):
 * let go as such, whatever the mode is by then; no pressure sent, no chord */
static uint32_t keys_taken(void)
{
    return ui.key_preset | ui.key_pat | ui.key_mix | ui.key_step | ui.key_lock | ui.key_tap;
}

/* (0.38) a key on TEMPO: a tap. The tempo is the last taps' (up to TAP_TAPS: four beats) average; a pause longer than
 * TAP_GAP_MS starts a new count. Following a MIDI clock in, the clock leads: no tap. */
static void tempo_tap(void)
{
    const uint32_t now = plat_ms();
    char t[12];
    if (seq.ext) {
        app_say("TEMPO", "MIDI CLOCK");
        return;
    }
    if (ui.tap_n && now - ui.tap_at[ui.tap_n - 1u] > TAP_GAP_MS)
        ui.tap_n = 0;
    if (ui.tap_n == TAP_TAPS) {              /* (the oldest out; a loop: the device's C library declares no memmove) */
        for (uint32_t i = 1; i < TAP_TAPS; i++)
            ui.tap_at[i - 1u] = ui.tap_at[i];
        ui.tap_n--;
    }
    ui.tap_at[ui.tap_n++] = now;
    if (ui.tap_n < 2u) {
        app_say("TAP", "AGAIN");
        return;
    }
    const uint32_t span = now - ui.tap_at[0], beats = ui.tap_n - 1u;
    const uint32_t bpm = span ? (60000u * beats + span / 2u) / span : SEQ_BPM_MAX;
    seq.bpm = (uint16_t)clamp((int32_t)bpm, SEQ_BPM_MIN, SEQ_BPM_MAX);
    set_dirty = 1;
    ui.turned_at[EN_K1] = now;               /* (TEMPO's knob lit, easing to it, as turned) */
    ui.redraw = 1;
    int_text(seq.bpm, t);
    app_say("TAP TEMPO", t);
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
        if (w >= 0 && !chain.bn)             /* the first: that pattern, as before */
            pattern_pick(w);
        int have = 0;                        /* (0.41.1: each pattern once) */
        for (uint32_t i = 0; i < chain.bn; i++)
            have |= chain.build[i] == w;
        if (w >= 0 && chain.bn < CHAIN_MAX && !have) {   /* (0.41) and the next ones after it: a chain, once HOME is
                                                          * let go */
            chain.build[chain.bn++] = (uint8_t)w;
            ui.redraw = 1;
        }
    } else if (ui.edit_down && w >= 0) {     /* (0.35) EDIT held, a white key: that step's locks cleared */
        ui.key_lock |= bit;
        ui.edit_used = 1;
        lock_clear_step(ui.view_page * SEQ_PAGE + (uint32_t)w);
    } else if (ui.lock_mode) {               /* (0.35) LOCK: a step held (white), a track (black 1..8) */
        ui.key_lock |= bit;
        if (w >= 0)
            lock_key((uint32_t)w, 1);
        else if (b >= 0 && b < (int)SYNTH_TRACKS)
            track_select((uint32_t)b);
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
    } else if (active_page == PAGE_TEMPO) {  /* (0.38) TEMPO: every key taps it (none plays) */
        ui.key_tap |= bit;
        tempo_tap();
    } else {                                 /* a note: key 0 is F3 (MIDI 53), the octave as it is now */
        const int32_t note = 53 + (int32_t)k + 12 * set.octave;
        const uint32_t v = set.velocity_off ? FIXED_VELOCITY : touch_vel(k);   /* GLO VELOCITY: the keys', or fixed */
        ui.vel[k] = (uint8_t)v;
        ui.prs[k] = 0;
        ui.note[k] = (uint8_t)note;
        ui.last_key = (int32_t)k;
        ui.key_track[k] = track_sel;         /* (its note-off to the same track, whatever is selected then) */
        queue_key_event(SYNTH_EVENT_NOTE_ON, note, v, track_sel);
        if (seq_arp_on(&seq, track_sel) || midi_out_ch(track_sel) < 0)   /* (0.34: the arp's notes go out, as it
                                                                          * plays them; 0.39: a track OFF, none) */
            ui.key_midi &= ~bit;
        else {
            ui.key_ch[k] = (uint8_t)midi_out_ch(track_sel);   /* its channel (GLO's MIDI) */
            plat_midi_send(0x09u | (0x90u | ui.key_ch[k]) << 8 | (uint32_t)note << 16 | v << 24, TO_USB);
            ui.key_midi |= bit;
        }
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
    if (ui.key_lock & bit) {                 /* (0.35) a LOCK step, or one cleared: let go as such */
        const int w = key_index(WHITE_KEYS, 16, k);
        ui.key_lock &= ~bit;
        if (w >= 0 && ui.lock_held >> w & 1u)
            lock_key((uint32_t)w, 0);
    } else if (ui.key_step & bit) {          /* (a step: let go as one) */
        const int w = key_index(WHITE_KEYS, 16, k);
        ui.key_step &= ~bit;
        if (w >= 0)
            step_key((uint32_t)w, 0);
    } else if (keys_taken() & bit) {
        ui.key_preset &= ~bit;
        ui.key_pat &= ~bit;
        ui.key_mix &= ~bit;
        ui.key_tap &= ~bit;
    } else {
        const int32_t note = ui.note[k];
        ui.prs[k] = 0;
        queue_key_event(SYNTH_EVENT_NOTE_OFF, note, 0, ui.key_track[k]);
        if (ui.key_midi & bit)
            plat_midi_send(0x08u | (0x80u | ui.key_ch[k]) << 8 | (uint32_t)note << 16, TO_USB);
        ui.key_midi &= ~bit;
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
        else if ((keys & bit) && !(keys_taken() & bit) && (ui.key_midi & bit) && touch_pressure(k) != ui.prs[k]) {
            ui.prs[k] = touch_pressure(k);
            plat_midi_send(0x0Au | (0xA0u | ui.key_ch[k]) << 8 | (uint32_t)ui.note[k] << 16 |
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
    } else if (ui.step_mode || ui.lock_mode) {   /* (0.35: LOCK's steps too) */
        char t[4];
        ui.view_page = (uint8_t)clamp((int32_t)ui.view_page + e, 0, SEQ_PAGES - 1);
        ui.lock_held = 0;
        lock_hear();
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
    if (ui.home_down && (k == B_OCTDN || k == B_OCTUP || k == B_REC)) {   /* (0.40) HOME held: the pattern's COPY,
                                                                         * PASTE, CLEAR (no page change after) */
        ui.home_used = 1;
        if (k == B_OCTDN)
            pattern_copy();
        else if (k == B_OCTUP)
            pattern_paste();
        else
            pattern_clear();
        return;
    }
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
        chain.bn = 0;                        /* (0.41) */
        ui.sound_nav = 0;
        ui.home_at = plat_ms();
        break;
    case B_LFO:
    case B_ENV:
    case B_GLO:
    case B_FX:
    case B_ARP:                              /* (0.34) */
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
    case B_EDIT:                             /* (0.35) let go: LOCK mode on or off; held, a white key clears a step's
                                              * locks (0.32..0.34 free; FREEZE until then) */
        ui.edit_down = 1;
        ui.edit_used = 0;
        break;
    case B_SEL:                              /* (let go: the mixer, from every page, 0.26.2) */
        ui.sel_down = 1;
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
            ui.sweep_at = plat_ms() | 1u;    /* (0.31) the white keys light first to last */
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
    ui.step_mode = ui.step_mode == 0 ? 1u : ui.step_mode == 1 ? 2u : 0u;   /* (0.31: every track, the track alone,
                                                                         * back) */
    ui.step_held = -1;
    if (ui.step_mode == 1) {
        ui.rec = 0;
        ui.seq_from = active_page;
        ui.seq_from_home = home_page;
        active_page = home_page = PAGE_SEQ;
    } else if (ui.step_mode == 2)
        ui.roll_lo = 0xFF;
    else if (IS_SOUND_PAGE(ui.seq_from))     /* (the track's own sound page: it may be another track now) */
        sound_activate(ui.sound_page[track_sel]);
    else {
        active_page = ui.seq_from;
        home_page = ui.seq_from_home;
    }
    if (ui.step_mode == 2)
        app_say(TRACK_NAME[track_sel], SYNTH_IS_DRUM(track_sel) ? "STEPS" : "NOTES");
    else
        app_say("STEP", ui.step_mode ? "ON" : "OFF");
    ui.redraw = 1;
}

/* SEL let go: the mixer (again: back to the page it was opened on). (Held on an FX page until 0.31: SEED, the grains
 * by hand) */
static void sel_follow(uint32_t btn)
{
    if (ui.sel_down && !(btn & 1u << B_SEL)) {
        ui.sel_down = 0;
        if (active_page == PAGE_MIXER)
            active_page = ui.mixer_from;
        else {
            ui.mixer_from = active_page;
            active_page = PAGE_MIXER;
        }
        app_say("PAGE", page_name(active_page));
        ui.redraw = 1;
    }
}

/* the buttons let go: SAVE, HOME (its page, unless it picked a pattern), SEQ, SEL */
static void buttons_let_go(uint32_t btn)
{
    if (ui.preset_down && !(btn & 1u << B_SAVE))
        save_let_go();
    if (ui.home_down && !(btn & 1u << B_HOME)) {
        ui.home_down = 0;
        if (chain.bn >= 2u)                  /* (0.41) two or more patterns picked: the chain */
            chain_start();
        chain.bn = 0;
        if (ui.home_shown)
            ui.redraw = 1;
        ui.home_shown = 0;
        if (!ui.home_used)
            page_open(B_HOME);
    }
    if (ui.seq_down && !(btn & 1u << B_SEQ))
        seq_let_go();
    if (ui.edit_down && !(btn & 1u << B_EDIT)) {   /* (0.35) */
        ui.edit_down = 0;
        if (!ui.edit_used)
            lock_mode_toggle();
    }
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
    if (SYNTH_IS_DRUM(track_sel)) {          /* (0.37) */
        app_say("LFO", "S1-S4 ONLY");
        return;
    }
    volatile int32_t *dest = &trk_mod[track_sel][2];
    const uint32_t destination = (uint32_t)clamp((int32_t)lfo_destination(*dest) + e, 0, 2);
    const int32_t value = (int32_t)destination * 34;
    if (value != *dest) {
        *dest = value;
        set_dirty = 1;
        ui.preset_edited |= (uint8_t)(1u << track_sel);   /* (0.37: the sound's) */
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
    if ((active_page == PAGE_MOD || active_page == PAGE_ENV) && SYNTH_IS_DRUM(track_sel)) {   /* (0.37) */
        app_say(active_page == PAGE_MOD ? "LFO" : "ENV", "S1-S4 ONLY");
        return;
    }
    volatile int32_t *value = param_of(active_page, index);
    const int32_t previous = *value;
    char t[12];
    *value = active_page == PAGE_FX_ECHO && index == 1u ? clamp(previous + e, 0, GRAV_ECHO_TIMES - 1)   /* (0.32) */
                                                        : clamp_percent(previous + e);
    if (*value != previous) {
        set_dirty = 1;
        if (active_page == PAGE_OSC || active_page == PAGE_SHAPE || active_page == PAGE_MOD ||
            active_page == PAGE_ENV)         /* (0.37: the LFO and the envelope the sound's too) */
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
    else if (knob && ui.lock_mode && (ui.lock_held || (ui.rec && seq.playing)) &&
             lock_param(active_page, index) >= 0)   /* (0.35) a lock: on the steps held, or the one playing */
        lock_turn((uint32_t)lock_param(active_page, index), index, e);
    else if (knob && ui.lock_mode && ui.lock_held)
        app_say("LOCK", "NOT ON THIS PAGE");
    else if ((knob || k == EN_K4) && active_page == PAGE_DEBUG)
        debug_turn(index, e);
    else if (knob && (active_page == PAGE_SEQ || active_page == PAGE_TEMPO))
        seq_turn(index, e);
    else if (k == EN_SELECT)
        select_turn(e);
    else if (knob && active_page == PAGE_MIXER)
        mixer_turn(index, e);
    else if (knob && active_page == PAGE_SCREEN) {
        if (index == 0u)                     /* (K3: none) */
            theme_turn(e);
        else if (index == 1u)
            sky_turn(e);
    } else if (knob && active_page == PAGE_GLO)
        glo_turn(index, e);
    else if (knob && active_page == PAGE_MIDI)
        midi_turn(index, e);
    else if (knob && active_page == PAGE_USB)
        usb_turn(index, e);
    else if (k == EN_K1 && active_page == PAGE_FX_TAPE)
        quality_turn(e);
    else if (knob && IS_ARP_PAGE(active_page))   /* (0.34) */
        arp_turn(index, e);
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
    } else if (active_page == PAGE_MOD && i == 2) { v = (int32_t)lfo_destination(*param_of(PAGE_MOD, 2)); max = 2; }
    else if (active_page == PAGE_FX_TAPE && i == 0) { v = fx_quality; max = GRAV_FX_QUALITIES - 1; }
    else if (active_page == PAGE_FX_ECHO && i == 1) max = GRAV_ECHO_TIMES - 1;
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
        (active_page == PAGE_MOD && i == 2) || (active_page == PAGE_FX_TAPE && i == 0) ||
        (active_page == PAGE_FX_ECHO && i == 1)) return "";
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
    uint8_t locked;                      /* (0.35) LOCK mode: the value the step held locks it to */
    const char *hint;
} knob_t;

static void knob_own(uint32_t i, knob_t *k)
{
    k->hint = "";
    k->centred = 0;
    k->inherit = 0;
    k->value[0] = 0;
    if (active_page == PAGE_SEQ || active_page == PAGE_TEMPO) {   /* (0.36.2: TEMPO's page, HOME's next, the same) */
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
    if (IS_ARP_PAGE(active_page)) {          /* (0.34) the selected track's arp (a drum track has none) */
        const uint32_t t = track_sel;
        k->name = PARAM_NAME[active_page][i];
        if (SYNTH_IS_DRUM(t) || (active_page == PAGE_ARP_2 && i == 2u)) {
            k->frac = KNOB_NONE;
            return;
        }
        if (active_page == PAGE_ARP) {
            if (i == 0u) {
                text_cat(k->value, ARP_MODE_NAME[arp_mode(t)]);
                k->frac = (int32_t)arp_mode(t) * 25600 / (int32_t)(SEQ_ARP_MODES - 1u);
            } else if (i == 1u) {
                text_cat(k->value, grav_echo_time_name(arp_rate(t)));
                k->frac = (int32_t)arp_rate(t) * 25600 / (int32_t)(GRAV_ECHO_TIMES - 1u);
            } else {
                int_text((int32_t)arp_octaves(t), k->value);
                k->frac = ((int32_t)arp_octaves(t) - 1) * 25600 / 3;
            }
        } else if (i == 0u) {
            int_text((int32_t)arp_gate(t), k->value);
            text_cat(k->value, "%");
            k->frac = ((int32_t)arp_gate(t) - 5) * 25600 / 95;
        } else {
            text_cat(k->value, arp_hold(t) ? "ON" : "OFF");
            k->frac = arp_hold(t) ? 25600 : 0;
        }
        return;
    }
    if ((active_page == PAGE_MOD || active_page == PAGE_ENV) && SYNTH_IS_DRUM(track_sel)) {   /* (0.37) none */
        k->name = PARAM_NAME[active_page][i];
        k->frac = KNOB_NONE;
        return;
    }
    if (active_page == PAGE_MIXER) {         /* the selected track's VOL (0.27) and SEND, the effect's RETURN */
        const int32_t v = i == 0u ? set.vol[track_sel] : i == 1u ? set.send[track_sel] : fx_params[FX_HALL];
        k->name = PARAM_NAME[PAGE_MIXER][i];
        int_text(v, k->value);
        if (i)
            text_cat(k->value, "%");
        k->frac = v * 256;
        return;
    }
    if (active_page == PAGE_SCREEN) {        /* THEME, SKY (0.28) */
        k->name = PARAM_NAME[PAGE_SCREEN][i];
        if (i == 1u) {
            text_cat(k->value, SKY_NAME[set.sky]);
            k->frac = set.sky == SKY_OFF ? 0 : set.sky == SKY_DAY ? 12800 : 25600;
            return;
        }
        if (i) {
            k->frac = KNOB_NONE;
            return;
        }
        text_cat(k->value, PALETTES[theme_palette()].name);
        k->frac = (int32_t)theme_at() * 25600 / (int32_t)(THEMES - 1u);
        return;
    }
    if (active_page == PAGE_USB) {           /* (0.42) K1 AUDIO; K2, K3 none */
        k->name = PARAM_NAME[PAGE_USB][i];
        if (i) {
            k->frac = KNOB_NONE;
            return;
        }
        text_cat(k->value, set.usb_audio ? "MULTI" : "STEREO");
        k->frac = set.usb_audio ? 25600 : 0;
        return;
    }
    if (active_page == PAGE_MIDI) {          /* (0.39) */
        k->name = PARAM_NAME[PAGE_MIDI][i];
        if (i == 0u) {
            channel_text(set.midi_ch[track_sel], k->value);
            k->frac = (int32_t)set.midi_ch[track_sel] * 25600 / 16;
        } else if (i == 1u) {
            text_cat(k->value, MIDI_IN_NAME[set.midi_in]);
            k->frac = set.midi_in == MIDI_IN_CHANNELS ? 25600 : 0;
        } else {
            text_cat(k->value, CLOCK_NAME[set.midi_clock]);
            k->frac = (int32_t)set.midi_clock * 25600 / CLOCK_BOTH;
        }
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

/* a knob as the page shows it: its own value; in LOCK mode with a step held, the step's lock of it (marked locked) */
static void knob_get(uint32_t i, knob_t *k)
{
    knob_own(i, k);
    k->locked = 0;
    const int p = lock_param(active_page, i);
    if (!ui.lock_mode || !ui.lock_held || p < 0 || k->frac < 0)
        return;
    const int v = seq_lock_get(&seq.pat, track_sel, lock_first(), (uint32_t)p);
    if (v < 0)
        return;
    k->locked = 1;
    k->frac = v * 256;
    k->value[0] = 0;
    if (active_page == PAGE_MIXER) {
        int_text(v, k->value);
        if (i)
            text_cat(k->value, "%");
    } else
        param_text(active_page, i, v, k->value);
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
    *h = active_page == PAGE_GLO || active_page == PAGE_MIDI || active_page == PAGE_USB || active_page == PAGE_SCREEN ?
         84 : 96;
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
        cv_text(x - text_w(&FONT_XS, k.name) / 2, y0, &FONT_XS, k.name, hot || k.locked ? C_HI : C_GRAY);
        if (k.locked)                        /* (0.35) locked: a mark before its name */
            cv_rect(x - text_w(&FONT_XS, k.name) / 2 - 6, y0 + 5, 3, 3, C_HI);
        dial_face(x, y, r + knob_grow(i, full), &k, i, hot, 3, 5);
        const felucca_font_t *font = text_w(&FONT_S, k.value) <= 70 ? &FONT_S : &FONT_XS;
        cv_text(x - text_w(font, k.value) / 2, y + r + 6, font, k.value, k.locked ? C_HI : k.frac >= 0 ? C_WHITE : C_GRAY);
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
        if (k.frac == KNOB_NONE)             /* (0.34: none, as ARP 2's K3) */
            continue;
        const int32_t x = 18 + (int32_t)i * 80 - xoffset, y = y0 + 19;
        const int hot = knob_since(i) < 450u;
        ui.dial_hot |= (uint8_t)(hot << i);
        dial_face(x, y, 10 + knob_grow(i, 2), &k, i, hot, 2, 4);
        cv_text(x + 17, y0 + 2, &FONT_T, k.name, hot || k.locked ? C_HI : C_GRAY);
        if (k.locked)                        /* (0.35) locked: a mark after its name */
            cv_rect(x + 17 + text_w(&FONT_T, k.name) + 3, y0 + 6, 3, 3, C_HI);
        const felucca_font_t *font = text_w(&FONT_S, k.value) <= 46 ? &FONT_S :
                                     text_w(&FONT_XS, k.value) <= 54 ? &FONT_XS : &FONT_T;   /* (CLEAN TAPE) */
        cv_text(x + 17, y0 + (font == &FONT_T ? 18 : 16), font, k.value, k.locked ? C_HI : k.inherit ? C_GRAY : C_WHITE);
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
    if ((active_page == PAGE_MOD || active_page == PAGE_ENV) && SYNTH_IS_DRUM(track_sel)) {   /* (0.37) */
        const char *m = active_page == PAGE_MOD ? "THE LFO IS S1-S4'S" : "A DRUM ENVELOPES ITSELF";
        cv_text(120 - text_w(&FONT_XS, m) / 2, 80, &FONT_XS, m, C_DIM);
        return;
    }
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
          IS_ARP_PAGE(active_page) || active_page == PAGE_TEMPO ||
          active_page == PAGE_SEQ || active_page == PAGE_GLO || active_page == PAGE_MIDI || active_page == PAGE_USB ||
          active_page == PAGE_SCREEN ||
          active_page == PAGE_MIXER) ||
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
    if (active_page == PAGE_SCREEN)          /* (THEME and SKY: K3 none) */
        ui.dial_dirty &= 3u;
    if (active_page == PAGE_USB)             /* (AUDIO alone) */
        ui.dial_dirty &= 1u;
    if (!ui.dial_dirty) return 0;
    /* One 80-wide tile of the row per frame (~2.5 ms SPI), even when a preset changes all three values at once. */
    uint32_t dial = ui.dial_next;
    for (uint32_t n = 0; n < 3; n++, dial = (dial + 1u) % 3u)
        if (ui.dial_dirty & 1u << dial) break;
    ui.dial_next = (uint8_t)((dial + 1u) % 3u);
    if (active_page == PAGE_SEQ && !ui.step_mode)   /* (HOME's terricons: no knobs drawn) */
        return 0;
    if (active_page == PAGE_SEQ || active_page == PAGE_MIXER || IS_FX_PAGE(active_page) || IS_ARP_PAGE(active_page)) {   /* (small: SEQ's at the
                                                                   * page's top, 0.28; the mixer's and, 0.33, the
                                                                   * effect's in the bottom band) */
        cv_begin(80, CHIP_H, C_BLACK);
        draw_chips(0, 1u << dial, (int32_t)dial * 80);
        cv_blit(dial * 80, active_page == PAGE_SEQ ? 36u : 180u + CHIP_Y);
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
#define STRIP_Y 22                           /* the mixer's strip, redrawn as it plays: from here to the page's end */
#define SEQ_STRIP_Y 40                       /* (0.28) SEQ's: under its knobs (at the page's top, CHIP_H high) */
#define GRID_Y 42                            /* SEQ: the tracks' rows, 15 apart (the drums' 4 lower), to 164: on into
                                              * the page's foot (0.30.1: two canvases, as HOME; the band one line) */
#define GRID_DY 15
#define CELL_H 13
#define GRID_DRUMS 4
#define POS_Y 167                            /* the pages */

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
 * ground line; each its height and place, the overlapping ones drawn from the tallest (furthest) to the lowest. Spoil
 * heaps as the Donbas has them: their tops flat, a terrace on each slope, gullies down them */
#define HOME_FOOT 28                         /* (0.29.2) HOME's scene goes on under the page, 28 rows more (two canvases:
                                              * 144 rows at most each); its band, 32 high, under it */
#define HOME_BAND_Y (180 + HOME_FOOT)
#define TC_BASE 160                          /* the ground line (in the scene, from the page's top) */
#define TC_HALF 44                           /* half a terricon's foot */
#define TC_N 22                              /* points up each slope (2 TC_N + 1 along it) */
static const uint8_t TC_X[SEQ_TRACKS] = {26, 58, 92, 124, 152, 180, 206, 230};   /* its middle's x */
static const uint8_t TC_H[SEQ_TRACKS] = {108, 88, 99, 78, 69, 78, 56, 62};       /* its height (to its point, cut) */
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

/* the ground under a line through n points left to right, down to the base, in colour c (what is behind it hidden) */
static void ground_under(const int16_t *x, const int16_t *y, uint32_t n, int32_t base, uint16_t c)
{
    for (uint32_t i = 0; i + 1u < n; i++)
        for (int32_t col = x[i]; col < x[i + 1u]; col++) {
            const int32_t yy = y[i] + (y[i + 1u] - y[i]) * (col - x[i]) / (x[i + 1u] - x[i]);
            cv_rect(col, yy, 1, base - yy, c);
        }
}

static uint16_t sky_at(int32_t y, int32_t base)   /* the sky's colour on row y: one colour (0.29.2; 0.29 darker up;
                                                   * 0.30 by day a lighter blue) */
{
    (void)y;
    (void)base;
    return set.sky == SKY_DAY ? RGB(92, 152, 228) : RGB(2, 3, 10);   /* (0.30.2: by day a light blue again) */
}

static float sky_flare(uint32_t look, uint32_t now, float ms)   /* the last hit of a track of this look, fading */
{
    float f = 0.0f;
    for (uint32_t t = 0; t < SEQ_TRACKS; t++)
        if (track_look(*track_engine(t)) == look && !(seq.mute >> t & 1u) && now - ui.hit_at[t] < 600u)
            f = fm_maxf(f, fm_expf(-(float)(now - ui.hit_at[t]) / ms));
    return f;
}

/* a disc of light at (cx, cy): r its core in colour c, then its glow over the sky, fading out over g pixels (a: its
 * strength, 0..1); a moon's dark part: none of it where it is inside the disc (dx, dy) of radius rd */
static void sky_light(int32_t cx, int32_t cy, float r, float g, uint16_t c, uint16_t glow, float a, int32_t base,
                      int32_t dx, int32_t dy, float rd)
{
    const int32_t e = (int32_t)(r + g) + 1;
    for (int32_t y = cy - e; y <= cy + e; y++) {
        if (y < 0 || y >= base)
            continue;
        const uint16_t sky = sky_at(y, base);
        for (int32_t x = cx - e; x <= cx + e; x++) {
            const float fx = (float)(x - cx), fy = (float)(y - cy), d = fm_sqrtf(fx * fx + fy * fy);
            if (d > r + g)
                continue;
            if (d <= r) {
                const float ox = fx - (float)dx, oy = fy - (float)dy;
                if (rd <= 0.0f || ox * ox + oy * oy > rd * rd)
                    cv_rect(x, y, 1, 1, c);
                continue;
            }
            const float k = 1.0f - (d - r) / g;
            cv_rect(x, y, 1, 1, mix565(sky, glow, (int32_t)(256.0f * a * k * k)));
        }
    }
}

/* (0.29.1) the moon, at the top right: a crescent, its light shimmering, its glow breathing, a ripple of light going
 * out from it now and again. (0.29.3) A clean crescent: its glow by its lit edge alone, its dark part the sky */
static void draw_moon(int32_t cx, int32_t cy, int32_t base, float time)
{
    const float r = 12.0f, rc = 11.0f, g = 14.0f + 3.0f * fm_sinf(time * 0.6f);   /* (rc: the disc taken out of it, at
                                                                               * (6, -4) from its middle) */
    const float a = 0.22f + 0.08f * fm_sinf(time * 0.9f) + 0.05f * fm_sinf(time * 2.3f);
    float ring = time * 5.0f;
    ring -= fm_floorf(ring / 19.0f) * 19.0f;
    const uint16_t lit = mix565(RGB(225, 222, 190), RGB(255, 251, 230), (int32_t)(128.0f + 127.0f * fm_sinf(time * 1.3f)));
    const int32_t e = (int32_t)(r + g) + 2;
    for (int32_t y = cy - e; y <= cy + e; y++) {
        if (y < 0 || y >= base)
            continue;
        const uint16_t sky = sky_at(y, base);
        for (int32_t x = cx - e; x <= cx + e; x++) {
            const float fx = (float)(x - cx), fy = (float)(y - cy), ox = fx - 6.0f, oy = fy + 4.0f;
            const float d1 = fm_sqrtf(fx * fx + fy * fy), d2 = fm_sqrtf(ox * ox + oy * oy);
            if (d1 <= r) {                   /* the crescent, softer towards its dark side; its dark part the sky's */
                if (d2 > rc)
                    cv_rect(x, y, 1, 1, mix565(sky, lit, (int32_t)(256.0f * fm_minf(1.0f, (d2 - rc) * 0.7f + 0.35f))));
                continue;
            }
            if (d1 - r > g)
                continue;
            /* the glow: as lit as the disc's edge nearest it (none by its dark part) */
            const float qx = fx / d1 * r - 6.0f, qy = fy / d1 * r + 4.0f;
            const float lit_edge = fm_clampf((fm_sqrtf(qx * qx + qy * qy) - rc) * 0.25f + 0.3f, 0.0f, 1.0f);
            const float k = 1.0f - (d1 - r) / g, w = d1 - r - ring;
            const float glow = (a * k * k + 0.12f * fm_expf(-w * w / 3.0f) * (1.0f - ring / 19.0f)) * lit_edge;
            cv_rect(x, y, 1, 1, mix565(sky, RGB(230, 230, 205), (int32_t)(256.0f * fm_minf(1.0f, glow))));
        }
    }
}

/* the canvas's pixel at (x, y), or black off it (what a see-through thing is drawn over) */
static uint16_t cv_get(int32_t x, int32_t y)
{
    y += cv_oy;
    if ((uint32_t)x >= cv_w || y < cv_y0 || y >= cv_y1)
        return C_BLACK;
    return swap16(cv_px[(uint32_t)y * cv_w + (uint32_t)x]);
}

/* (0.30) a cloud, soft and see-through: eleven puffs of different sizes, each densest in its middle, thinning to its
 * edge; the sky shows through it (three quarters of it white at most), its top bright, its underside a little greyer
 * and bluer, as a lit cloud's. (x0, y0): its first puff's middle; sc its size; n its shape (each cloud its own) */
static void draw_cloud(float x0, float y0, float sc, int32_t base, uint32_t n)
{
    static const int8_t PUFF[11][3] = {{0, 2, 7}, {7, -4, 8}, {15, -8, 10}, {24, -9, 11}, {33, -5, 9}, {40, 0, 7},
                                       {6, 5, 7}, {15, 5, 8}, {25, 4, 8}, {34, 4, 7}, {20, -1, 10}};
    float cx[11], cy[11], r2[11];
    for (uint32_t k = 0; k < 11u; k++) {     /* (each cloud its puffs a little apart: its own shape) */
        const float r = (float)PUFF[k][2] * sc * (0.85f + 0.3f * ic_rnd(n * 31u + k));
        cx[k] = x0 + (float)PUFF[k][0] * sc;
        cy[k] = y0 + (float)PUFF[k][1] * sc;
        r2[k] = r * r;
    }
    const int32_t xa = (int32_t)(x0 - 10.0f * sc), xb = (int32_t)(x0 + 50.0f * sc);
    const int32_t ya = (int32_t)(y0 - 22.0f * sc), yb = (int32_t)(y0 + 13.0f * sc);
    for (int32_t y = ya; y <= yb; y++) {
        if (y < 0 || y >= base)
            continue;
        const float v = fm_clampf(((float)y - (y0 - 20.0f * sc)) / (32.0f * sc), 0.0f, 1.0f);   /* (0 its top, 1 its foot) */
        const uint16_t tint = mix565(RGB(250, 252, 255), RGB(176, 192, 222), (int32_t)(256.0f * v * v));
        for (int32_t x = xa; x <= xb; x++) {
            float dens = 0.0f;
            for (uint32_t k = 0; k < 11u; k++) {
                const float dx = (float)x - cx[k], dy = (float)y - cy[k], f = 1.0f - (dx * dx + dy * dy) / r2[k];
                if (f > dens)
                    dens = f;
            }
            if (dens <= 0.0f)
                continue;
            const float a = fm_minf(1.0f, dens * 2.2f) * 0.78f;   /* (soft at its edge, the sky through it) */
            cv_rect(x, y, 1, 1, mix565(cv_get(x, y), tint, (int32_t)(256.0f * a)));   /* (over what is there:
                                                                              * the sun shows through) */
        }
    }
}

static void draw_sky(int32_t base, int32_t dy, uint32_t now)
{
    if (set.sky == SKY_OFF)
        return;
    for (int32_t y = 0; y < base; y++)
        cv_rect(0, y, 240, 1, sky_at(y, base));
    const float time = (float)(now % 600000u) * 0.001f;
    if (set.sky == SKY_DAY) {
        const float kick = sky_flare(LOOK_KICK, now, 120.0f);   /* the sun, flaring at each kick */
        sky_light(196, 26 + dy, 11.0f + 2.0f * kick, 15.0f, RGB(255, 226, 110), RGB(255, 220, 90), 0.45f + 0.4f * kick, base,
                  0, 0, 0.0f);
        static const uint8_t AT[3] = {0, 90, 170}, Y[3] = {36, 50, 28}, SCALE[3] = {100, 80, 70};   /* (low in the
                                                                                   * sky, over the heaps' tops) */
        const float speed = 3.0f * (float)seq_bpm(&seq) / 60.0f;   /* the clouds: slowly, at the tempo's pace (0.30.1) */
        for (uint32_t c = 0; c < 3u; c++) {
            const float sc = (float)SCALE[c] * 0.01f;
            float pos = (float)AT[c] + time * speed * sc;
            pos -= fm_floorf(pos / 300.0f) * 300.0f;
            draw_cloud((float)pos - 40.0f, (float)(Y[c] + dy), sc, base, c);
        }
    } else {
        const float hat = sky_flare(LOOK_HAT, now, 90.0f);
        for (uint32_t i = 0; i < 56u; i++) { /* the stars (0.29.1): each comes out, shines a few seconds, fades and
                                              * comes out again elsewhere; twinkling, the brighter flashing at each
                                              * hi-hat; white, some bluish, some warm */
            const float period = 5.0f + 5.0f * ic_rnd(i * 3u + 1u);
            float ph = time / period + ic_rnd(i * 3u + 2u);
            const uint32_t cycle = (uint32_t)ph;
            ph -= (float)cycle;
            const float life = ph < 0.2f ? ph * 5.0f : ph < 0.7f ? 1.0f : ph < 0.9f ? (0.9f - ph) * 5.0f : 0.0f;
            if (life <= 0.0f)
                continue;
            const uint32_t h = ic_hash(i * 977u + cycle * 131u);
            const int32_t x = (int32_t)(h % 240u), y = 2 + (int32_t)((h >> 8) % 110u) + dy;
            if (y >= base - 4)
                continue;
            const int bright = i % 5u == 0u;
            const float tw = fm_minf(1.0f, ((0.7f + 0.3f * fm_sinf(time * 3.0f + (float)i * 1.7f)) +
                                            (bright ? hat * 0.8f : 0.0f)) * life);
            const uint16_t star = i % 7u == 3u ? RGB(200, 220, 255) : i % 7u == 5u ? RGB(255, 235, 200) : RGB(255, 255, 240);
            if (bright) {                    /* (a cross of light) */
                const uint16_t d = mix565(sky_at(y, base), star, (int32_t)(110.0f * tw));
                cv_rect(x - 1, y, 3, 1, d);
                cv_rect(x, y - 1, 1, 3, d);
            }
            cv_rect(x, y, 1, 1, mix565(sky_at(y, base), star, (int32_t)(256.0f * tw)));
        }
        {                                    /* now and then a shooting star, down to the left */
            const uint32_t n = (uint32_t)(time / 11.0f);
            const float f0 = (time - (float)n * 11.0f) / 0.7f;
            if (f0 < 1.0f) {
                const uint32_t h = ic_hash(n * 7919u + 17u);
                const float x0 = 60.0f + (float)(h % 150u), y0 = 4.0f + (float)((h >> 8) % 24u) + (float)dy;
                for (int32_t k = 0; k < 12; k++) {
                    const float f = f0 - (float)k * 0.03f;
                    const int32_t x = (int32_t)(x0 - f * 70.0f), y = (int32_t)(y0 + f * 30.0f);
                    if (f < 0.0f)
                        break;
                    if (y >= 0 && y < base)
                        cv_rect(x, y, 1, 1, mix565(sky_at(y, base), C_WHITE, 230 - k * 18));
                }
            }
        }
        draw_moon(196, 26 + dy, base, time);
    }
}

/* the terricons' ridge on a ground line at base, each h_pct % of its height (TC_H) and half its foot half wide
 * (HOME's: TC_HALF), from the furthest. A track with no step on in its pattern has none; one silent (muted, or another
 * soloed) is grey and still; one quiet is its colour, dim and still; one sounding lives: its slopes bend with its
 * sound (its level, followed), in its engine's way (a kick a strong, quick bulge, and its peak jumps as it hits; a
 * hi-hat small, quick teeth; a pad slow waves). The selected track's is thicker, white at its core; on HOME a marker
 * and its name over its peak. (0.33) Over the effect's lake (lake): a track with no step a grey heap, still; no
 * marker; and each heap's reflection under the ground line, its outline mirrored, half as high, on every other row,
 * dim, swayed sideways by sway px */
static float LAKE_SIN[256];                  /* (the lake's) a turn in 256 steps */
static void draw_ridge(int32_t base, int32_t h_pct, int32_t half, uint32_t now, int lake, float sway)
{
    const float time = (float)(now % 600000u) * 0.001f, scale = (float)h_pct * 0.01f;
    const int day = !lake && set.sky == SKY_DAY;
    const uint16_t earth = day ? RGB(18, 16, 22) : C_BLACK;   /* (the heaps' black, by day dark earth) */
    int16_t px[2 * TC_N + 1], py[2 * TC_N + 1];
    for (uint32_t pass = 0; pass < (lake ? 2u : 1u); pass++)   /* (the lake's: the heaps, then their reflections,
                                                                 * the feet's glow cut between) */
    for (uint32_t o = 0; o < SEQ_TRACKS; o++) {
        const uint32_t t = TC_ORDER[o];
        const int used = track_used(t);
        if (pass && !o)
            cv_rect(0, base + 1, 240, 2, C_BLACK);
        if (!used && !lake)                  /* empty: no terricon */
            continue;
        const int muted = !used || seq.mute >> t & 1u, sel = t == track_sel;
        const uint32_t look = track_look(*track_engine(t)), since = now - ui.hit_at[t];
        const int drum_hit = !muted && look <= LOOK_HAT && since < 400u;
        float amp = muted ? 0.0f : ui.level[t];
        if (drum_hit)                        /* a drum's hit: a swell as it begins, its peak up a moment */
            amp += 0.6f * fm_expf(-(float)since / 90.0f);
        if (amp > 1.2f)
            amp = 1.2f;
        const float h = (float)TC_H[t] * scale * (drum_hit ? 1.0f + 0.18f * fm_expf(-(float)since / 120.0f) : 1.0f);
        const float depth = 1.3f * scale * TRACK_LOOK[look].depth * (0.55f + 0.45f * (float)TC_H[t] / 96.0f) * amp;
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
            float y = (float)base - h * (1.0f - au) * (1.0f - 0.06f * u * u);
            if (y < (float)base - h * 0.88f)  /* its top, flat */
                y = (float)base - h * 0.88f;
            if (au > 0.55f && au < 0.7f && y > (float)base - h * 0.45f + 2.0f)   /* a terrace on each slope */
                y = (float)base - h * 0.45f + 2.0f;
            px[i] = (int16_t)(TC_X[t] + (int32_t)((float)half * u + (u < 0.0f ? -0.5f : 0.5f)));
            py[i] = (int16_t)(y - bend + 0.5f);
        }
        if (pass) {                          /* its reflection: the outline mirrored, half as high, every other row */
            const uint16_t c = muted ? mix565(C_BLACK, C_DIM, 150) : mix565(C_BLACK, TRACK_HUE[t], sel ? 170 : 120);
            for (uint32_t i = 0; i < 2u * TC_N; i++) {
                const int32_t y0 = base + (base - py[i]) / 2, y1 = base + (base - py[i + 1u]) / 2;
                const int32_t x0 = px[i], x1 = px[i + 1u];
                const int32_t n = (x1 > x0 ? x1 - x0 : x0 - x1) + (y1 > y0 ? y1 - y0 : y0 - y1);
                for (int32_t k = 0; k <= n; k++) {
                    const int32_t y = y0 + (n ? (y1 - y0) * k / n : 0);
                    if (y <= base || (y - base) & 1)
                        continue;
                    const float w = sway * LAKE_SIN[(uint32_t)(time * 81.5f + (float)y * 28.5f + (float)t * 40.0f) & 255u];
                    cv_glow(x0 + (n ? (x1 - x0) * k / n : 0) + (int32_t)(w + (w < 0.0f ? -0.5f : 0.5f)), y, c);
                }
            }
            continue;
        }
        ground_under(px, py, 2u * TC_N + 1u, base, earth);
        for (int32_t k = -2; k <= 2; k++)    /* gullies down its slopes */
            if (k) {
                const uint32_t at = (uint32_t)((float)TC_N * (1.0f + 0.12f * (float)k) + 0.5f);
                cv_line(px[at], py[at] + 3, TC_X[t] + (int32_t)(0.42f * (float)k * (float)half), base - 1,
                        mix565(earth, day ? RGB(120, 114, 108) : muted ? C_DIM : TRACK_HUE[t], 70));
            }
        if (muted)                           /* silent: grey, still */
            thick_line(px, py, 2u * TC_N + 1u, C_DIM, sel ? 2 : 1);
        else if (amp < 0.02f)                /* quiet: its colour, dim (by day, dark: 0.29.1) */
            thick_line(px, py, 2u * TC_N + 1u, mix565(earth, TRACK_HUE[t], day ? (sel ? 120 : 70) : sel ? 200 : 120),
                       sel ? 2 : 1);
        else {                               /* sounding: a glow under, brighter the louder; its colour on it */
            thick_line(px, py, 2u * TC_N + 1u,
                       mix565(earth, TRACK_HUE[t], day ? 20 + (int32_t)(amp * 45.0f) : 40 + (int32_t)(amp * 90.0f)),
                       sel ? 5 : 4);
            thick_line(px, py, 2u * TC_N + 1u, day ? mix565(earth, TRACK_HUE[t], 100 + (int32_t)(fm_minf(amp, 1.0f) * 70.0f))
                                                   : TRACK_HUE[t], 2);
        }
        if (sel) {                           /* the selected track's: a white core; on HOME over its peak a marker,
                                              * its name */
            const int32_t tx = px[TC_N], ty = py[TC_N];
            if (!muted)
                thick_line(px, py, 2u * TC_N + 1u, day ? mix565(earth, C_WHITE, 170) : C_WHITE, 1);
            if (lake)
                continue;
            for (int32_t k = 0; k < 6; k++)  /* (a shadow under it first: readable over a cloud) */
                cv_rect(tx - k + 1, ty - 4 - k, 2 * k + 1, 1, C_BLACK);
            for (int32_t k = 0; k < 6; k++)
                cv_rect(tx - k, ty - 5 - k, 2 * k + 1, 1, C_WHITE);
            cv_text(tx - text_w(&FONT_T, TRACK_NAME[t]) / 2 + 1, ty - 23, &FONT_T, TRACK_NAME[t], C_BLACK);
            cv_text(tx - text_w(&FONT_T, TRACK_NAME[t]) / 2, ty - 24, &FONT_T, TRACK_NAME[t], C_WHITE);
        }
    }
}

/* HOME: the terricons, under the sky (draw_ridge), on the ground; the selected track's level at the top right */
static void draw_terricons(int32_t dy, uint32_t now)
{
    const int32_t base = TC_BASE + dy;
    draw_sky(base, dy, now);
    draw_ridge(base, 100, TC_HALF, now, 0, 0.0f);
    cv_rect(0, base + 1, 240, 144, set.sky == SKY_DAY ? RGB(26, 24, 28) : C_BLACK);   /* the ground (the feet's glow
                                                                                     * cut) */
    cv_rect(0, base, 240, 1, C_LINE);
}

#ifdef PLAT_HOST
#define LOGO_MS 0u                           /* (the simulator: the page at once, the tests' times as they were) */
#else
#define LOGO_MS 2200u                        /* (0.36.1) the logo held until then, ms since power-on */
#endif

/* (0.36) the splash at power-on (plat.h APP_SPLASH): the logo, five terricons in ink before a teal sun on paper,
 * TERYKONS under them, the version under those (0.36.1), kept until LOGO_MS (app_frame). It is drawn before the settings and the theme are read: its own colours. Two bands of the
 * canvas (144 rows at most each) */
static void splash_heap(int16_t *px, int16_t *py, int32_t x, int32_t base, float h, int32_t half)
{                                            /* a terricon's outline, still: draw_ridge's shape */
    for (uint32_t i = 0; i <= 2u * TC_N; i++) {
        const float u = (float)i / (float)TC_N - 1.0f, au = u < 0.0f ? -u : u;
        float y = (float)base - h * (1.0f - au) * (1.0f - 0.06f * u * u);
        if (y < (float)base - h * 0.88f)
            y = (float)base - h * 0.88f;
        if (au > 0.55f && au < 0.7f && y > (float)base - h * 0.45f + 2.0f)
            y = (float)base - h * 0.45f + 2.0f;
        px[i] = (int16_t)(x + (int32_t)((float)half * u + (u < 0.0f ? -0.5f : 0.5f)));
        py[i] = (int16_t)(y + 0.5f);
    }
}

static void splash_band(int32_t dy)          /* (dy: the band's top row less the screen's) */
{
    static const int8_t X[5] = {-60, -32, 0, 30, 58};
    static const uint8_t H[5] = {40, 60, 82, 56, 36}, ORDER[5] = {2, 1, 3, 0, 4};   /* (the tallest first) */
    const uint16_t paper = RGB(250, 244, 232), ink = RGB(36, 32, 40), sun = RGB(60, 210, 185);
    const int32_t base = 134 + dy;
    int16_t px[2 * TC_N + 1], py[2 * TC_N + 1];
    const uint32_t n = 2u * TC_N + 1u;
    cv_rect(0, 0, 240, 240, paper);
    for (int32_t y = -40; y <= 40; y++) {   /* the sun */
        const int32_t w = (int32_t)fm_sqrtf((float)(1600 - y * y));
        cv_rect(120 - w, 84 + dy + y, 2 * w + 1, 1, sun);
    }
    for (uint32_t o = 0; o < 5u; o++) {      /* the heaps, their gullies a paler ink */
        const uint32_t i = ORDER[o];
        const int32_t half = 26 + H[i] / 6;
        splash_heap(px, py, 120 + X[i], base, (float)H[i], half);
        ground_under(px, py, n, base, ink);
        for (int32_t k = -2; k <= 2; k++)
            if (k) {
                const uint32_t at = (uint32_t)((float)TC_N * (1.0f + 0.12f * (float)k) + 0.5f);
                cv_line(px[at], py[at] + 4, 120 + X[i] + (int32_t)(0.42f * (float)k * (float)half), base - 1,
                        mix565(ink, paper, 60));
            }
        thick_line(px, py, n, ink, 2);
    }
    cv_rect(30, base, 180, 2, ink);
    cv_text(120 - text_w(&FONT_L, "TERYKONS") / 2, 146 + dy, &FONT_L, "TERYKONS", ink);
    cv_text(120 - text_w(&FONT_XS, "SYNTHESIZER FOR THE M-VAVE FM-1") / 2, 184 + dy, &FONT_XS,
            "SYNTHESIZER FOR THE M-VAVE FM-1", RGB(150, 136, 118));
    {                                        /* (0.36.1) the version */
        const int32_t x = 120 - (text_w(&FONT_XS, "VERSION ") + text_w(&FONT_XS, APP_VERSION)) / 2;
        cv_text(x, 206 + dy, &FONT_XS, "VERSION", RGB(186, 172, 152));
        cv_text(x + text_w(&FONT_XS, "VERSION "), 206 + dy, &FONT_XS, APP_VERSION, ink);
    }
}

void app_splash(void)
{
    cv_begin(240, 144, RGB(250, 244, 232));
    splash_band(0);
    cv_blit(0, 0);
    cv_begin(240, 96, RGB(250, 244, 232));
    splash_band(-144);
    cv_blit(0, 144);
}

/* the selected track's level, six segments in its colour: in HOME's bottom band, at its right (0.29.1) */
#define LEVEL_X 192                          /* (0.29.3: under CPU, their edges in line) */
#define LEVEL_Y 21
static void draw_level(int32_t x, int32_t y)
{
    const int32_t lv = (int32_t)(ui.level[track_sel] * 6.0f + 0.5f);
    for (int32_t k = 0; k < 6; k++)
        cv_rect(x + k * 7, y, 5, 8, k < lv ? TRACK_HUE[track_sel] : C_LINE);
}

/* (0.33) the effect's pages (HALL, TAPE, ECHO): a lake at night under HOME's terricons, smaller (draw_ridge), moving
 * with their tracks as they do there, mirrored in it. What goes into the effect drops into the water, a white ring as strong as it is loud; ECHO's
 * repeats follow it, TIME apart, rings of the theme's colour, each weaker by FEEDBACK; HALL's tail ripples on while
 * it sounds, fainter rings, as many as DIFFUSE makes them, brighter as TONE is (darker and softer when not), carried
 * further as TIME is longer. MOD sways the rings and, while the water moves, the terricons' reflections; QUALITY shows as
 * the medium: VINTAGE's rings on a 2-pixel grid, CLEAN TAPE's wavering, DIRTY TAPE's broken up (2-pixel blocks 3
 * apart) over a grain. The page's own part is the brighter: HALL's tail, ECHO's repeats (TAPE: both). The drops and
 * the tail as the effect hears them (grav_fx_meters), the repeats from the drops: nothing sent, still water, nothing
 * redrawn */
#define LAKE_Y 24                            /* the scene's top in the page, its name over it: redrawn from here */
#define LAKE_HORIZON 86                      /* the shore: the ridge's ground line */
#define LAKE_H_PCT 48                        /* the terricons' height, HOME's in % */
#define LAKE_HALF 32                         /* half a terricon's foot (HOME's 44) */
#define LAKE_CY 116                          /* the rings' centre */
#define LAKE_RINGS 96
#define LAKE_TRAINS 8
#define LAKE_SPEED 0.052f                    /* a ring's growth, px a ms */
#define LAKE_LIFE 2850u                      /* ms: by then it has gone off the screen (150 px) */
enum { RING_DROP = 1, RING_ECHO, RING_HALL };
static struct {
    uint32_t at[LAKE_RINGS];                 /* when each ring began */
    uint8_t amp[LAKE_RINGS], kind[LAKE_RINGS];   /* its strength (255 the most), RING_* (0: none) */
    uint8_t next;                            /* the slot the next one takes (the oldest's) */
    uint8_t drop_slot;                       /* the last drop's: it grows while its onset rises */
    struct {                                 /* a drop's echoes to come: */
        uint32_t at;                         /* the drop's time */
        float a, g;                          /* the first repeat's strength against the drop's, the next's against it */
        uint8_t k, slot;                     /* the next repeat (1 the first); the drop's ring */
    } train[LAKE_TRAINS];
    uint8_t train_next;
    float in, hall, ref;                     /* the levels followed; ref the drops' (the tail seen against it) */
    uint32_t drop_at, ripple_at, heard;      /* the last drop, the tail's next ripple; the last frame */
    uint32_t drops, echoes, ripples;         /* rings begun (the tests) */
    uint8_t shown;                           /* the water was moving at the last frame drawn */
} lake;

static uint32_t lake_ring(uint32_t kind, float a, uint32_t at)
{
    const uint32_t i = lake.next;
    lake.next = (uint8_t)((i + 1u) % LAKE_RINGS);
    lake.at[i] = at;
    lake.amp[i] = (uint8_t)(fm_minf(a, 1.0f) * 255.0f + 0.5f);
    lake.kind[i] = (uint8_t)kind;
    return i;
}

/* a drop: what is sent rises well over its slow follower (which takes ~80 ms to climb to it: a rise over a few frames
 * is still one). Its ring grows while the level rises still, 40 ms at least and up to 200 (a chord's notes, a slow
 * attack: one drop), the follower held up with it: the next drop only once it has fallen (over ~150 ms) and risen
 * again. Its echoes set going: the first as ECHO's AMOUNT sends (its square, seen to the 0.25th), each next weaker
 * by FEEDBACK's loop (a tape's a little over, as grav_fx.cpp; seen to the 0.35th) */
static void lake_drop(float p, uint32_t now, float dt, int shown)
{
    const float a = p / (p + 0.03f);
    if (now - lake.drop_at < 200u && (now - lake.drop_at < 40u || p >= lake.in)) {
        if (lake.kind[lake.drop_slot] == RING_DROP && lake.at[lake.drop_slot] == lake.drop_at &&
            a * 255.0f > (float)lake.amp[lake.drop_slot])
            lake.amp[lake.drop_slot] = (uint8_t)(fm_minf(a, 1.0f) * 255.0f);
        lake.in = fm_maxf(lake.in, p);
    } else if (p > 0.003f && p > 1.5f * lake.in + 0.002f && now - lake.drop_at >= 60u) {
        lake.drop_at = now;
        lake.in = p;
        if (shown) {
            const float e = (float)fx_params[FX_ECHO] * 0.01f;
            const float fb = (float)fx_params[FX_FEEDBACK] * (fx_quality >= GRAV_FX_CLEAN_TAPE ? 0.0104f : 0.0097f);
            lake.drop_slot = (uint8_t)lake_ring(RING_DROP, a, now);
            lake.drops++;
            if (e > 0.0f) {
                const uint32_t n = lake.train_next;
                lake.train_next = (uint8_t)((n + 1u) % LAKE_TRAINS);
                lake.train[n].at = now;
                lake.train[n].a = fm_sqrtf(e);
                lake.train[n].g = fb > 0.0f ? fm_powf(fm_minf(fb, 0.99f), 0.35f) : 0.0f;
                lake.train[n].k = 1;
                lake.train[n].slot = lake.drop_slot;
            }
        }
    } else
        lake.in = p > lake.in ? lake.in + (p - lake.in) * (1.0f - fm_expf(-dt / 80.0f)) : lake.in * fm_expf(-dt / 150.0f);
}

/* the drops' echoes come due: TIME apart (as the effect plays it: fitted to its line), each a ring from its own
 * time; a train ends once its repeats are too faint to see */
static void lake_echoes(uint32_t now, int shown)
{
    const float ms = grav_echo_heard((uint32_t)fx_params[FX_ECHO_TIME], (float)seq_bpm(&seq)) * 1000.0f;
    for (uint32_t n = 0; n < LAKE_TRAINS; n++) {
        if (lake.train[n].a <= 0.0f)
            continue;
        while (lake.train[n].k < 200u) {
            const uint32_t due = lake.train[n].at + (uint32_t)((float)lake.train[n].k * ms + 0.5f);
            if ((int32_t)(now - due) < 0)
                break;
            const uint32_t d = lake.train[n].slot;   /* (the drop's strength, once it has grown) */
            const float drop = lake.kind[d] == RING_DROP && lake.at[d] == lake.train[n].at ? (float)lake.amp[d] / 255.0f : 0.5f;
            const float a = fm_sqrtf(drop) * lake.train[n].a;
            if (a < 0.04f || !shown) {
                lake.train[n].a = 0.0f;
                break;
            }
            if (now - due < LAKE_LIFE) {
                lake_ring(RING_ECHO, a, due);
                lake.echoes++;
            }
            lake.train[n].a *= lake.train[n].g;
            lake.train[n].k++;
        }
    }
}

/* the effect's levels since the last frame (taken every frame, whatever the page: none left over): the drops, their
 * echoes; HALL's tail a ripple every 1 / (2 + 6 DIFFUSE) s while it sounds, as strong as it is against the drops.
 * Rings begin only on the effect's pages, nothing over them */
static void lake_listen(uint32_t now)
{
    float in, hall;
    grav_fx_meters(&in, &hall);
    const float dt = (float)(now - lake.heard);
    lake.heard = now;
    const int shown = IS_FX_PAGE(active_page) && !list_open() && !ui.splash_until;
    lake.ref = fm_maxf(in, lake.ref * fm_expf(-dt / 3000.0f));
    lake_drop(in, now, dt, shown);
    lake_echoes(now, shown);
    lake.hall = fm_maxf(hall, lake.hall * fm_expf(-dt / 60.0f));
    if ((int32_t)(now - lake.ripple_at) >= 0) {
        lake.ripple_at = now + 1000u / (2u + 6u * (uint32_t)fx_params[FX_DIFFUSE] / 100u);
        const float a = 0.6f * fm_sqrtf(fm_minf(lake.hall / fm_maxf(lake.ref, 0.02f), 1.0f));
        if (shown && a >= 0.04f) {
            lake_ring(RING_HALL, a, now);
            lake.ripples++;
        }
    }
}

static int lake_moving(uint32_t now)         /* a ring still on the water */
{
    for (uint32_t i = 0; i < LAKE_RINGS; i++)
        if (lake.kind[i] && now - lake.at[i] < LAKE_LIFE)
            return 1;
    return 0;
}

static int32_t lake_snap(float v, int32_t s)  /* to the medium's steps of s pixels */
{
    return s > 1 ? (int32_t)(v / (float)s + 0.5f) * s : (int32_t)(v + 0.5f);
}

/* the lake, its rows from LAKE_Y down to the page's foot (dy: the canvas's top row less the page's) */
static void draw_lake(int32_t dy, uint32_t now)
{
    const float time = (float)(now % 600000u) * 0.001f;
    const float mod = (float)fx_params[FX_MOD] * 0.01f, tone = (float)fx_params[FX_TONE] * 0.01f;
    const float reach = 35.0f + 1.3f * (float)fx_params[FX_HALL_TIME];
    const uint32_t q = (uint32_t)fx_quality;
    const int32_t hy = LAKE_HORIZON + dy, cy = LAKE_CY + dy, step = q == GRAV_FX_VINTAGE ? 2 : q == GRAV_FX_DIRTY_TAPE ? 3 : 1;
    const int32_t block = q == GRAV_FX_DIRTY_TAPE ? 2 : 1;
    const float hall_k = active_page == PAGE_FX_HALL ? 1.0f : active_page == PAGE_FX_TAPE ? 0.8f : 0.4f;
    const float echo_k = active_page == PAGE_FX_ECHO ? 1.0f : active_page == PAGE_FX_TAPE ? 0.85f : 0.55f;
    const uint16_t hall_c = mix565(C_AMB, C_WHITE, (int32_t)(tone * 154.0f));
    float stir = 0.0f;                       /* how much the water moves: its rings' strength */
    for (uint32_t i = 0; i < LAKE_RINGS; i++)
        if (lake.kind[i] && now - lake.at[i] < LAKE_LIFE)
            stir += (float)lake.amp[i] * (1.0f / 255.0f) * fm_expf(-(float)(now - lake.at[i]) * (1.0f / 900.0f));
    stir = fm_minf(stir, 1.0f);
    if (!LAKE_SIN[64])
        for (uint32_t i = 0; i < 256u; i++)
            LAKE_SIN[i] = fm_sinf((float)i * (6.2831853f / 256.0f));
    /* the ridge: HOME's terricons, smaller, moving as they do; their reflections, swaying with MOD while the water
     * moves; the shore */
    draw_ridge(hy, LAKE_H_PCT, LAKE_HALF, now, 1, mod * 2.5f * stir);
    cv_rect(0, hy, 240, 1, C_LINE);
    /* the rings, the newest first, up to a frame's points */
    int32_t budget = 12000;
    for (uint32_t n = 0; n < LAKE_RINGS && budget > 0; n++) {
        const uint32_t i = (lake.next + LAKE_RINGS - 1u - n) % LAKE_RINGS, age = now - lake.at[i];
        if (!lake.kind[i] || age >= LAKE_LIFE)
            continue;
        const float r = 2.0f + (float)age * LAKE_SPEED;
        const int hall = lake.kind[i] == RING_HALL, echo = lake.kind[i] == RING_ECHO;   /* (the tail's fading as far
                                                       * as TIME carries it; a drop's soon, near; an echo's further, a
                                                       * row thicker) */
        const float k = (float)lake.amp[i] * (1.0f / 255.0f) * fm_expf(-r / (hall ? reach : echo ? 110.0f : 70.0f)) *
                        (hall ? hall_k : echo ? echo_k : 0.85f);
        if (k < 0.03f)
            continue;
        const uint16_t base = lake.kind[i] == RING_DROP ? C_WHITE : lake.kind[i] == RING_ECHO ? C_HI : hall_c;
        const uint16_t c = mix565(C_BLACK, base, (int32_t)(fm_minf(k, 1.0f) * 256.0f)), soft = mix565(C_BLACK, c, 128);
        const uint32_t pts = r * 4.4f < 24.0f ? 24u : (uint32_t)(r * 4.4f);   /* (about its perimeter: whole) */
        /* round it by turning a point: cos, sin a step at a time (and three times as fast, from its own phase, for
         * MOD's sway of its radius) */
        const float d = 6.2831853f / (float)pts, cd = fm_cosf(d), sd = fm_sinf(d), cd3 = fm_cosf(3.0f * d),
                    sd3 = fm_sinf(3.0f * d), ph = time * 2.2f + (float)i * 0.37f;
        float c1 = 1.0f, s1 = 0.0f, c3 = fm_cosf(ph), s3 = fm_sinf(ph);
        budget -= (int32_t)pts * block * block;
        for (uint32_t j = 0; j < pts; j++) {
            const float w = r * (1.0f + 0.07f * mod * s3);
            const float fx = 120.0f + w * c1;
            float fy = (float)cy + 0.3f * w * s1;
            if (q == GRAV_FX_CLEAN_TAPE)
                fy += LAKE_SIN[(uint32_t)(time * 244.5f + fx * 2.04f) & 255u] * mod * 0.8f;
            const int32_t x = lake_snap(fx, q == GRAV_FX_VINTAGE ? 1 : step), y = lake_snap(fy, step);
            if (y > hy + 1) {
                for (int32_t by = 0; by < block; by++)   /* (DIRTY TAPE: coarse pixels, broken up) */
                    for (int32_t bx = 0; bx < block; bx++)
                        cv_glow(x + bx, y + by, c);
                if (echo)
                    cv_glow(x, y + block, c);
                else if (tone < 0.45f)       /* dark: softer, a row thicker */
                    cv_glow(x, y + block, soft);
            }
            const float c1n = c1 * cd - s1 * sd, c3n = c3 * cd3 - s3 * sd3;
            s1 = s1 * cd + c1 * sd;
            c1 = c1n;
            s3 = s3 * cd3 + c3 * sd3;
            c3 = c3n;
        }
    }
    if (q == GRAV_FX_DIRTY_TAPE && stir > 0.05f)   /* DIRTY TAPE's grain on the moving water */
        for (uint32_t i = 0; i < 60u; i++) {
            const uint32_t h = ic_hash(now * 131u + i * 977u);
            cv_glow((int32_t)(h % 240u), hy + 2 + (int32_t)((h >> 8) % (uint32_t)(144 - LAKE_HORIZON - 2)), C_DIM);
        }
    cv_text(8, 24 + dy, &FONT_XS, page_name(active_page), C_HI);   /* (over the sky, as every page has it) */
}

/* (0.34) the ARP pages: the selected track's arp as a ladder, its order across (a column a note, as RATE plays them;
 * CHORD: a column an octave, its notes stacked), each note a bar as high as its pitch, in the track's colour; the
 * note playing white, fading, a line down from it. Nothing held: the last chord's ladder (a C major one's), dim, and
 * how to fill it;
 * MODE OFF: dimmer still. A drum track has none. Redrawn alone as it plays (arp_frame) */
#define ARP_Y 40                             /* the ladder's top in the page: redrawn from here */
static void draw_arp_ladder(int32_t dy, uint32_t now)
{
    const uint32_t t = track_sel;
    const int32_t top = ARP_Y + 6 + dy, bottom = ARP_Y + 82 + dy;
    if (SYNTH_IS_DRUM(t)) {
        cv_text(120 - text_w(&FONT_XS, "NO ARP ON A DRUM TRACK") / 2, ARP_Y + 34 + dy, &FONT_XS, "NO ARP ON A DRUM TRACK",
                C_DIM);
        return;
    }
    seq_arp_t a = seq.arp[t];
    const uint32_t mode = arp_mode(t);
    const int ghost = a.n == 0u;
    if (ghost) {                             /* (nothing held: the last chord played, its shape; a C major one) */
        static const uint8_t TRIAD[3] = {60, 64, 67};
        a.n = (uint8_t)(ui.last_n > 1u ? ui.last_n : 3u);
        for (uint32_t k = 0; k < a.n && k < SEQ_NOTES; k++)
            a.note[k] = ui.last_n > 1u ? ui.last_chord[k] : TRIAD[k];
    }
    a.mode = (uint8_t)(mode == SEQ_ARP_OFF || mode == SEQ_ARP_RANDOM || mode == SEQ_ARP_CHORD ? SEQ_ARP_UP : mode);
    a.octaves = (uint8_t)arp_octaves(t);
    uint8_t order[SEQ_ARP_ORDER];
    const uint32_t n = seq_arp_order(&a, order), cols = mode == SEQ_ARP_CHORD ? a.octaves : n;
    if (!n)
        return;
    int32_t lo = 127, hi = 0;
    for (uint32_t i = 0; i < n; i++) {
        lo = order[i] < lo ? order[i] : lo;
        hi = order[i] > hi ? order[i] : hi;
    }
    const int32_t range = hi - lo < 12 ? 12 : hi - lo;
    int32_t at = -1;                         /* the column playing */
    if (!ghost && mode != SEQ_ARP_OFF && a.last != 0xFFu) {
        if (mode == SEQ_ARP_CHORD)
            at = (int32_t)((a.pos + a.octaves - 1u) % a.octaves);
        else if (mode == SEQ_ARP_RANDOM) {
            for (uint32_t i = 0; i < n && at < 0; i++)
                if (order[i] == a.last)
                    at = (int32_t)i;
        } else
            at = (int32_t)((a.pos + n - 1u) % n);
    }
    const int32_t w = 224 / (int32_t)cols > 14 ? 14 : 224 / (int32_t)cols, x0 = 8 + (224 - w * (int32_t)cols) / 2;
    const int32_t flash = flash256(now - ui.arp_at, 250u);
    const uint16_t hue = TRACK_HUE[t], base = ghost || mode == SEQ_ARP_OFF ? (mode == SEQ_ARP_OFF ? C_LINE : C_DIM) :
                                                                             mix565(C_BLACK, hue, 150);
    for (int32_t c = 0; c < (int32_t)cols; c++) {
        const int32_t x = x0 + c * w;
        cv_rect(x + w / 2, bottom + 4, 1, c % 4 ? 2 : 4, C_LINE);   /* (a tick a note, longer each fourth) */
        if (c == at)                         /* (the column playing: a line down from it) */
            cv_rect(x + w / 2, top, 1, bottom - top + 2, mix565(C_BLACK, hue, 60 + flash / 3));
        for (uint32_t k = 0; k < (mode == SEQ_ARP_CHORD ? a.n : 1u); k++) {
            const int32_t note = mode == SEQ_ARP_CHORD ? (int32_t)a.note[k] + 12 * c : (int32_t)order[c];
            const int32_t y = bottom - (note - lo) * (bottom - top) / range;
            cv_rect(x + 1, y - 1, w - 2 > 2 ? w - 2 : 2, 3, c == at ? mix565(hue, C_WHITE, 120 + flash / 2) : base);
        }
    }
    {                                        /* the lowest note and the highest, at the left */
        char b[6];
        note_name(lo, b);
        cv_text(x0 - 2 - text_w(&FONT_T, b) < 0 ? 0 : x0 - 2 - text_w(&FONT_T, b), bottom - 5, &FONT_T, b, C_DIM);
        note_name(lo + (hi - lo), b);
        if (hi > lo)
            cv_text(x0 - 2 - text_w(&FONT_T, b) < 0 ? 0 : x0 - 2 - text_w(&FONT_T, b), top - 6, &FONT_T, b, C_DIM);
    }
    if (mode == SEQ_ARP_OFF || ghost) {
        const char *m = mode == SEQ_ARP_OFF ? "MODE OFF" : "PLAY KEYS OR A CHORD STEP";
        cv_text(120 - text_w(&FONT_T, m) / 2, bottom + 7, &FONT_T, m, C_GRAY);
    }
}

static void draw_arp_page(void)
{
    cv_text(8, 24, &FONT_XS, page_name(active_page), C_HI);
    if (!SYNTH_IS_DRUM(track_sel) && arp_hold(track_sel))
        cv_text(232 - text_w(&FONT_XS, "HOLD"), 24, &FONT_XS, "HOLD", seq.arp[track_sel].latched ? C_HI : C_DIM);
    draw_arp_ladder(0, plat_ms());
}

/* the effect's page: the lake (its name in it) */
static void draw_fx_page(void)
{
    draw_lake(0, plat_ms());
}

/* SEQ (step mode): every track's steps of the page shown, a row each, in the track's colour (its terricon's, 0.28):
 * a step on brighter the harder; the selected track's row on a ground of its colour, its steps at their brightest */
static void draw_grid(int32_t dy, uint32_t now)
{
    const uint32_t page = ui.view_page;
    for (uint32_t t = 0; t < SEQ_TRACKS; t++) {
        const seq_track_t *tr = &seq.pat.track[t];
        const int32_t y = GRID_Y + dy + (int32_t)t * GRID_DY + (t >= SEQ_S_TRACKS ? GRID_DRUMS : 0), sel = t == track_sel;
        const int32_t at = seq.playing ? seq.step[t] : -1, hit = flash256(now - ui.hit_at[t], 200u);
        const uint16_t hue = TRACK_HUE[t], sel_lo = mix565(C_BLACK, hue, 90);
        if (sel)                             /* the selected track: its row on a ground of its colour */
            cv_rect(0, y - 1, 240, CELL_H + 2, mix565(C_BLACK, hue, 40));
        const int muted = seq.mute >> t & 1u;   /* (0.26) silent (muted, or another soloed): its name red, steps dim */
        cv_text(3, y - 1, &FONT_XS, TRACK_NAME[t], muted ? C_RED : sel ? C_WHITE : hue);
        for (uint32_t i = 0; i < SEQ_PAGE; i++) {
            const uint32_t s = page * SEQ_PAGE + i;
            const int32_t x = step_x(i);
            if (s >= tr->length) {           /* past its length: a dot */
                cv_rect(x + 5, y + CELL_H / 2, 2, 1, C_LINE);
                continue;
            }
            const seq_step_t *st = &tr->step[s];
            uint16_t c = i % 4u ? RGB(20, 20, 25) : RGB(28, 28, 34);
            if (st->on)
                c = muted ? C_DIM : mix565(C_BLACK, hue, (sel ? 150 : 90) + (int32_t)st->vel * (sel ? 106 : 120) / 127);
            if (at == (int32_t)s)            /* its step playing: lit; a note, flashing as it begins */
                c = st->on ? mix565(c, C_WHITE, 64 + hit * 192 / 256) : RGB(70, 70, 82);
            cv_rect(x, y, 12, CELL_H, c);
            if (st->on && st->n > 1u)        /* a chord: a notch */
                cv_rect(x + 9, y, 3, 3, sel ? sel_lo : RGB(20, 20, 25));
            if (seq_lock_mask(&seq.pat, t, s, 0))   /* (0.35) its locks: a mark at its foot */
                cv_rect(x + 1, y + CELL_H - 3, 4, 2, sel ? C_WHITE : C_GRAY);
            if (sel && (ui.step_held == (int8_t)s || (ui.lock_mode && (ui.lock_held >> i & 1u)))) {   /* held: framed */
                cv_rect(x - 1, y - 1, 14, 1, C_WHITE);
                cv_rect(x - 1, y + CELL_H, 14, 1, C_WHITE);
                cv_rect(x - 1, y, 1, CELL_H, C_WHITE);
                cv_rect(x + 12, y, 1, CELL_H, C_WHITE);
            }
            if (sel && ui.rec && !seq.playing && ui.rec_pos == s)
                cv_rect(x, y + CELL_H - 2, 12, 2, C_RED);
        }
    }
    draw_pages(dy, page, seq.playing ? seq.step[track_sel] : -1, seq.playing ? step_frac(now) : 0);
}

/* (0.31) SEQ again: the selected track alone. A synth track's notes as a piano roll: the page's 16 steps across (the
 * white keys), 20 semitones up (the notes' own, followed), a note as long as its length, brighter the harder, its
 * start lit; the keyboard at the left, each C named. A drum track's 64 steps, a page a row (the one on the white
 * keys on a ground of its colour), each step's velocity a bar */
#define ROLL_Y 43                            /* the roll's top row: 20 semitones, 6 rows each, to 163 */
#define ROLL_N 20
#define ROLL_DY 6
#define DRUM_Y 43                            /* the drum's four pages, 28 high, 31 apart: to 164 */
#define DRUM_H 28
#define DRUM_DY 31

static int black_note(int32_t n)
{
    const int32_t k = n % 12;
    return k == 1 || k == 3 || k == 6 || k == 8 || k == 10;
}

/* the roll's lowest note: kept while the track's notes fit in it; else they are centred (more than the roll holds:
 * the page's) */
static void roll_follow(void)
{
    int32_t lo = 127, hi = 0, plo = 127, phi = 0, b = ui.roll_lo == 0xFF ? -1 : ui.roll_lo;
    for (uint32_t s = 0; s < TRK.length; s++) {
        const seq_step_t *st = &TRK.step[s];
        for (uint32_t k = 0; st->on && k < st->n; k++) {
            const int32_t n = st->note[k];
            lo = n < lo ? n : lo;
            hi = n > hi ? n : hi;
            if (s / SEQ_PAGE == ui.view_page) {
                plo = n < plo ? n : plo;
                phi = n > phi ? n : phi;
            }
        }
    }
    if (lo > hi)                             /* none: around the next step's note */
        lo = hi = ui.last_chord[0];
    if (hi - lo >= ROLL_N && plo <= phi) {
        lo = plo;
        hi = phi;
    }
    if (b < 0 || lo < b || hi > b + ROLL_N - 1)
        b = lo - (ROLL_N - 1 - (hi - lo)) / 2;
    ui.roll_lo = (uint8_t)clamp(b, 0, 127 - (ROLL_N - 1));
}

static void step_frame(int32_t x, int32_t y, int32_t h)   /* the step held: framed in white */
{
    cv_rect(x - 1, y - 1, 14, 1, C_WHITE);
    cv_rect(x - 1, y + h, 14, 1, C_WHITE);
    cv_rect(x - 1, y, 1, h, C_WHITE);
    cv_rect(x + 12, y, 1, h, C_WHITE);
}

static void draw_roll(int32_t dy, uint32_t now)
{
    const seq_track_t *tr = &TRK;
    const uint32_t p0 = ui.view_page * SEQ_PAGE, len = tr->length;
    const int32_t at = seq.playing ? seq.step[track_sel] : -1, hit = flash256(now - ui.hit_at[track_sel], 200u);
    const int32_t y0 = ROLL_Y + dy, y1 = y0 + ROLL_N * ROLL_DY;
    const uint16_t hue = TRACK_HUE[track_sel];
    const int muted = seq.mute >> track_sel & 1u;
    roll_follow();
    const int32_t lo = ui.roll_lo;
    for (int32_t r = 0; r < ROLL_N; r++) {   /* the keyboard, the rows (a black key's darker), each C's line */
        const int32_t n = lo + ROLL_N - 1 - r, y = y0 + r * ROLL_DY, blk = black_note(n);
        cv_rect(0, y, 7, ROLL_DY - 1, blk ? RGB(34, 34, 40) : RGB(118, 118, 128));
        for (uint32_t i = 0; i < SEQ_PAGE; i++) {
            const uint32_t s = p0 + i;
            uint16_t c = s >= len ? RGB(9, 9, 11) : blk ? RGB(15, 15, 19) : RGB(24, 24, 30);
            if (at == (int32_t)s)            /* the step playing: its column lighter */
                c = mix565(c, RGB(96, 96, 112), 70);
            cv_rect(step_x(i), y, 12, ROLL_DY - 1, c);
        }
        if (n % 12 == 0) {                   /* a C: named, and a line under it */
            char t[6];
            note_name(n, t);
            cv_rect(step_x(0), y + ROLL_DY - 1, step_x(15) + 12 - step_x(0), 1, RGB(44, 44, 54));
            cv_text(8, y - 4, &FONT_T, t, C_GRAY);
        }
    }
    for (uint32_t s = 0; s < p0 + SEQ_PAGE && s < len; s++) {   /* the notes: the page's, and those held into it */
        const seq_step_t *st = &tr->step[s];
        if (!st->on || s + st->len <= p0)
            continue;
        uint32_t end = s + st->len;          /* (the step after its last, within the page and the length) */
        end = end < p0 + SEQ_PAGE ? end : p0 + SEQ_PAGE;
        end = end < len ? end : len;
        if (end <= p0)
            continue;
        const int32_t x0 = step_x(s < p0 ? 0u : s - p0), x1 = step_x(end - 1u - p0) + 12;
        uint16_t c = muted ? C_DIM : mix565(C_BLACK, hue, 150 + (int32_t)st->vel * 106 / 127);
        if (at == (int32_t)s)                /* its step playing: flashing as it begins */
            c = mix565(c, C_WHITE, 64 + hit * 192 / 256);
        else if (at > (int32_t)s && at < (int32_t)end)   /* held on: a little lighter */
            c = mix565(c, C_WHITE, 40);
        for (uint32_t k = 0; k < st->n; k++) {
            const int32_t r = lo + ROLL_N - 1 - (int32_t)st->note[k];
            if (r < 0)                       /* above the roll, or below: a mark at its edge */
                cv_rect(x0 + 3, y0 - 3, 6, 2, c);
            else if (r >= ROLL_N)
                cv_rect(x0 + 3, y1, 6, 2, c);
            else {
                const int32_t y = y0 + r * ROLL_DY;
                cv_rect(x0, y, x1 - x0, ROLL_DY - 1, c);
                if (s >= p0)                 /* its start */
                    cv_rect(x0, y, 2, ROLL_DY - 1, mix565(c, C_WHITE, 140));
            }
        }
    }
    if (ui.step_held >= (int32_t)p0 && ui.step_held < (int32_t)(p0 + SEQ_PAGE))
        step_frame(step_x((uint32_t)ui.step_held - p0), y0, y1 - y0 - 1);
    if (ui.rec && !seq.playing && ui.rec_pos >= p0 && ui.rec_pos < p0 + SEQ_PAGE)
        cv_rect(step_x(ui.rec_pos - p0), y1, 12, 2, C_RED);
    draw_pages(dy, ui.view_page, at, seq.playing ? step_frac(now) : 0);
}

static void draw_drum_steps(int32_t dy, uint32_t now)
{
    const seq_track_t *tr = &TRK;
    const int32_t at = seq.playing ? seq.step[track_sel] : -1, hit = flash256(now - ui.hit_at[track_sel], 200u);
    const uint16_t hue = TRACK_HUE[track_sel];
    const int muted = seq.mute >> track_sel & 1u;
    for (uint32_t p = 0; p < SEQ_PAGES; p++) {
        const int32_t y = DRUM_Y + dy + (int32_t)(p * DRUM_DY), shown = p == ui.view_page;
        char t[4];
        if (shown)                           /* the page on the white keys: on a ground of its colour */
            cv_rect(0, y - 1, 240, DRUM_H + 2, mix565(C_BLACK, hue, 40));
        int_text((int32_t)p + 1, t);
        cv_text(7, y + DRUM_H / 2 - 8, &FONT_XS, t, shown ? C_WHITE : C_GRAY);
        for (uint32_t i = 0; i < SEQ_PAGE; i++) {
            const uint32_t s = p * SEQ_PAGE + i;
            const int32_t x = step_x(i);
            if (s >= tr->length) {           /* past its length: a dot */
                cv_rect(x + 5, y + DRUM_H / 2, 2, 1, C_LINE);
                continue;
            }
            const seq_step_t *st = &tr->step[s];
            uint16_t c = i % 4u ? RGB(20, 20, 25) : RGB(28, 28, 34);
            if (st->on)
                c = muted ? RGB(30, 30, 34) : mix565(C_BLACK, hue, shown ? 70 : 50);
            if (at == (int32_t)s && !st->on)
                c = RGB(70, 70, 82);
            cv_rect(x, y, 12, DRUM_H, c);
            if (st->on) {                    /* its velocity: a bar up from the foot, the harder the higher */
                const int32_t h = 6 + (DRUM_H - 6) * (int32_t)st->vel / 127;
                uint16_t b = muted ? C_DIM : mix565(C_BLACK, hue, (shown ? 170 : 130) + (int32_t)st->vel * 86 / 127);
                if (at == (int32_t)s)
                    b = mix565(b, C_WHITE, 64 + hit * 192 / 256);
                cv_rect(x, y + DRUM_H - h, 12, h, b);
            }
            if (ui.step_held == (int8_t)s)
                step_frame(x, y, DRUM_H);
            if (ui.rec && !seq.playing && ui.rec_pos == s)
                cv_rect(x, y + DRUM_H - 2, 12, 2, C_RED);
        }
    }
    if (at >= 0) {                           /* where it plays: a mark under its step */
        const int32_t y = DRUM_Y + dy + (at / (int32_t)SEQ_PAGE) * (int32_t)DRUM_DY + DRUM_H + 1;
        cv_rect(step_x((uint32_t)at % SEQ_PAGE) + 3, y, 6, 1, C_WHITE);
    }
}

static void draw_seq_body(int32_t dy)        /* the strip (rows STRIP_Y on), drawn dy rows up or down */
{
    if (ui.step_mode == 2 && SYNTH_IS_DRUM(track_sel))
        draw_drum_steps(dy, plat_ms());
    else if (ui.step_mode == 2)
        draw_roll(dy, plat_ms());
    else if (ui.step_mode)
        draw_grid(dy, plat_ms());
    else
        draw_terricons(dy, plat_ms());
}

static void draw_seq(void)
{
    if (ui.step_mode)                        /* (0.28) step mode: its knobs at the page's top */
        draw_chips(0, 7u, 0);
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
    cv_rect(4, 20, 232, 123, C_LINE);
    cv_rect(5, 21, 230, 121, C_BLACK);
    cv_text(12, 24, &FONT_XS, "PATTERN", C_GRAY);
    if (clip_from >= 0 || ui.copy_req || ui.bank_copy) {   /* (0.40) the copy, and the buttons for it */
        cv_text(80, 24, &FONT_XS, "COPY", C_GRAY);
        int_text((ui.copy_req || ui.bank_copy ? copy_from : clip_from) + 1, t);
        cv_text(110, 24, &FONT_XS, t, C_WHITE);
    }
    if (chain.bn || chain.n) {               /* (0.41) the chain (being made: the keys so far), the one playing lit */
        const uint32_t n = chain.bn ? chain.bn : chain.n;
        const uint8_t *at = chain.bn ? chain.build : chain.at;
        cv_text(12, 129, &FONT_XS, "CHAIN", C_GRAY);
        for (uint32_t i = 0; i < n; i++) {
            int_text(at[i] + 1, t);
            cv_text(46 + (int32_t)i * 12, 129, &FONT_XS, t,
                    !chain.bn && (int32_t)i == chain.pos ? C_HI : C_WHITE);
        }
    } else {
        cv_text(12, 129, &FONT_XS, "OCT- COPY", C_DIM);
        cv_text(120 - text_w(&FONT_XS, "OCT+ PASTE") / 2, 129, &FONT_XS, "OCT+ PASTE", C_DIM);
        cv_text(228 - text_w(&FONT_XS, "REC CLEAR"), 129, &FONT_XS, "REC CLEAR", C_DIM);
    }
    if (seq.next != SEQ_NO_PATTERN) {
        cv_text(150, 24, &FONT_XS, "NEXT", C_GRAY);
        int_text(seq.next + 1, t);
        cv_text(184, 24, &FONT_XS, t, C_HI);
    }
    for (uint32_t n = 0; n < BANK_PATTERNS; n++) {
        const int32_t x = 9 + (int32_t)(n % 4u) * 57, y = 39 + (int32_t)(n / 4u) * 22;   /* (0.40: room for the copy's
                                                                                    * buttons under them) */
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
    if (active_page == PAGE_SEQ && !ui.step_mode) {   /* HOME's terricons: the whole page, the scene's foot under
                                                       * it; the level, in the band */
        const uint32_t now = plat_ms();
        cv_begin(240, 144, C_BLACK);
        draw_terricons(0, now);
        cv_blit(0, 36);
        cv_begin(240, HOME_FOOT, C_BLACK);
        draw_terricons(-144, now);
        cv_blit(0, 180);
        cv_begin(40, 8, C_BLACK);
        draw_level(0, 0);
        cv_blit(LEVEL_X, HOME_BAND_Y + LEVEL_Y);
        return;
    }
    if (active_page == PAGE_MIXER) {
        cv_begin(240, 144u - STRIP_Y, C_BLACK);
        draw_mixer_body(-STRIP_Y);
        cv_blit(0, 36 + STRIP_Y);
    } else if (IS_FX_PAGE(active_page)) {    /* (0.33) the lake, under the page's name */
        cv_begin(240, 144u - LAKE_Y, C_BLACK);
        draw_lake(-LAKE_Y, plat_ms());
        cv_blit(0, 36 + LAKE_Y);
    } else if (IS_ARP_PAGE(active_page)) {   /* (0.34) the arp's ladder */
        cv_begin(240, 144u - ARP_Y, C_BLACK);
        draw_arp_ladder(-ARP_Y, plat_ms());
        cv_blit(0, 36 + ARP_Y);
    } else {                                 /* (step mode: under its knobs, and on into the page's foot) */
        cv_begin(240, 144u - SEQ_STRIP_Y, C_BLACK);
        draw_seq_body(-SEQ_STRIP_Y);
        cv_blit(0, 36 + SEQ_STRIP_Y);
        cv_begin(240, HOME_FOOT, C_BLACK);
        draw_seq_body(-144);
        cv_blit(0, 180);
    }
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

/* (0.39) GLO's MIDI: its knobs; above them every track's channel (the one selected lit, OFF a red dash), under them
 * what IN does */
static void draw_midi_page(void)
{
    int32_t h, r;
    char t[8];
    draw_dials(knob_row(&h, &r), 7u, 0);
    for (uint32_t k = 0; k < SYNTH_TRACKS; k++) {
        const int32_t x = 6 + (int32_t)k * 29;
        cv_text(x, 26, &FONT_T, TRACK_NAME[k], k == track_sel ? C_HI : C_GRAY);
        channel_text(set.midi_ch[k], t);
        if (!set.midi_ch[k])                 /* (OFF, as a dash: the knob says it) */
            t[0] = '-', t[1] = 0;
        cv_text(x + text_w(&FONT_T, TRACK_NAME[k]) + 2, 26, &FONT_T, t, set.midi_ch[k] ? C_WHITE : C_RED);
    }
    cv_text(8, 130, &FONT_XS, "IN", C_GRAY);
    if (set.midi_in == MIDI_IN_CHANNELS) {
        cv_text(26, 130, &FONT_XS, "A TRACK'S CHANNEL PLAYS IT, ELSE", C_WHITE);
        cv_text(26 + text_w(&FONT_XS, "A TRACK'S CHANNEL PLAYS IT, ELSE") + 4, 130, &FONT_XS, TRACK_NAME[track_sel], C_HI);
    } else {
        cv_text(26, 130, &FONT_XS, "EVERY CHANNEL PLAYS", C_WHITE);
        cv_text(26 + text_w(&FONT_XS, "EVERY CHANNEL PLAYS") + 4, 130, &FONT_XS, TRACK_NAME[track_sel], C_HI);
    }
}

/* (0.42) GLO's USB: K1 AUDIO; above it the computer's input each track comes in on (MULTI: 3..10; STEREO: none, a red
 * dash), under it what 1-2 carry */
static void draw_usb_page(void)
{
    int32_t h, r;
    char t[8];
    draw_dials(knob_row(&h, &r), 1u, 0);
    for (uint32_t k = 0; k < SYNTH_TRACKS; k++) {
        const int32_t x = 6 + (int32_t)k * 29;
        cv_text(x, 26, &FONT_T, TRACK_NAME[k], k == track_sel ? C_HI : C_GRAY);
        t[0] = '-', t[1] = 0;
        if (set.usb_audio)
            int_text((int32_t)k + 3, t);
        cv_text(x + text_w(&FONT_T, TRACK_NAME[k]) + 2, 26, &FONT_T, t, set.usb_audio ? C_WHITE : C_RED);
    }
    cv_text(8, 130, &FONT_XS, "1-2", C_GRAY);
    cv_text(30, 130, &FONT_XS, set.usb_audio ? "THE MIX; EACH TRACK DRY ON ITS OWN" : "THE MIX; THE COMPUTER PLAYS HERE TOO",
            C_WHITE);
}

/* GLO's SCREEN: THEME as a knob; beside it every palette in the knob's order, five a row: a swatch of its two
 * colours and its name, the one shown framed */
/* GLO's SCREEN: K1 THEME, K2 SKY (0.28); under the knobs every palette's swatch in THEME's order, the one shown
 * framed (below the knobs' tiles, so a knob turning never redraws them) */
static void draw_screen_page(void)
{
    int32_t h, r;
    const int32_t y = knob_row(&h, &r) + h + 3;
    draw_dials(knob_row(&h, &r), 3u, 0);
    for (uint32_t i = 0; i < THEMES; i++) {
        const palette_t *pl = &PALETTES[THEME_ORDER[i]];
        const int32_t x = 9 + (int32_t)i * 23;
        if (THEME_ORDER[i] == theme_palette())
            cv_rect(x - 2, y - 2, 24, 13, C_WHITE);
        cv_rect(x - 1, y - 1, 22, 11, C_BLACK);
        cv_rect(x, y, 20, 5, pl->c[3]);
        cv_rect(x, y + 5, 20, 4, pl->c[4]);
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

/* the track's line, its top at y: the track and its engine; MUTE or SOLO (0.26: muted, or another soloed; 0.26.1:
 * SOLO) */
static void draw_track_line(int32_t y)
{
    cv_text(8, y, &FONT_S, TRACK_NAME[track_sel], C_WHITE);
    cv_text(30, y, &FONT_S, grav_engine_name(*track_engine(track_sel)), C_HI);
    if (seq.mute >> track_sel & 1u)
        cv_text(200, y + 2, &FONT_XS, "MUTE", C_RED);
    else if (ui.solo >> track_sel & 1u)
        cv_text(200, y + 2, &FONT_XS, "SOLO", C_HI);
}

/* SEQ's bottom band (step mode): the track's line (0.28: its knobs at the page's top); under it PLAY or STOP, REC and its step / STEP / a MIDI clock in,
 * the pattern and the next one (blinking), the preset unsaved, the load. A moment's message (OCTAVE, PATTERN, the
 * volume ...) over the left of that line */
static void draw_seq_band(void)
{
    char t[48];
    const int32_t y = 8;                     /* (0.30.1: one line; the track's engine in the header, its name in the
                                              * rows) */
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
        } else if (ui.step_mode == 2) {      /* (0.31) the track alone: its name, NOTES (a drum's: STEPS) */
            cv_text(48, y, &FONT_XS, TRACK_NAME[track_sel], seq.mute >> track_sel & 1u ? C_RED : TRACK_HUE[track_sel]);
            cv_text(48 + text_w(&FONT_XS, "S1") + 4, y + 2, &FONT_T, SYNTH_IS_DRUM(track_sel) ? "STEPS" : "NOTES", C_HI);
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

/* (0.27) HOME's bottom band: the selected track (TRK 03) and its engine, its state (MUTE, SOLO) and the load;
 * its sound's name, large (a * once it is changed). A moment's message (TEMPO 120 ...) or the count-in in its place */
static void draw_home_band(void)
{
    char t[48], n[24];
    const uint32_t tr = track_sel;
    t[0] = 0;
    text_cat(t, "TRK 0");
    int_text((int32_t)tr + 1, n);
    text_cat(t, n);
    cv_text(8, 1, &FONT_XS, t, C_GRAY);
    cv_text(60, 3, &FONT_T, grav_engine_name(*track_engine(tr)), C_DIM);
    const int silent = !SYNTH_IS_DRUM(tr) && !set.trk_voices[tr];   /* (0.27.1) VOICES OFF: it cannot sound */
    const char *state = silent ? "NO VOICE" : seq.mute >> tr & 1u ? "MUTE" : ui.solo >> tr & 1u ? "SOLO" : "";
    cv_text(186 - text_w(&FONT_XS, state), 1, &FONT_XS, state,   /* (ending clear of CPU) */
            silent || seq.mute >> tr & 1u ? C_RED : C_HI);
    {                                        /* CPU over the level, their edges in line (0.29.3) */
        char c[8];
        int_text((int32_t)ui.cpu, c);
        text_cat(c, "%");
        cv_text(LEVEL_X, 1, &FONT_XS, "CPU", C_GRAY);
        cv_text(LEVEL_X + 40 - text_w(&FONT_XS, c), 1, &FONT_XS, c, ui.xruns ? C_RED : C_HI);
    }
    draw_level(LEVEL_X, LEVEL_Y);
    if (ui.shown_count) {                    /* counting in: the beats to come */
        t[0] = 'I'; t[1] = 'N'; t[2] = ' ';
        int_text(ui.shown_count, t + 3);
        cv_text(8, 16, &FONT_S, t, C_RED);
        return;
    }
    if (band_message(t, 186)) {              /* a moment's message: its first word small, the rest a little larger */
        if (ui.msg_until && plat_ms() < ui.msg_until && ui.msg[1][0]) {
            cv_text(8, 17, &FONT_XS, ui.msg[0], C_GRAY);
            cv_text(8 + text_w(&FONT_XS, ui.msg[0]) + 6, 16, &FONT_S, ui.msg[1], C_HI);
        } else
            cv_text(8, 17, &FONT_XS, t, C_HI);
        return;
    }
    n[0] = 0;                                /* its sound: the preset's name, * once changed (0.29.2: smaller) */
    text_cat(n, preset_label(tr));
    if (preset_id[tr] == PRESET_CUSTOM && n[0] == 'C')   /* (none loaded: the engine's name) */
        n[0] = 0, text_cat(n, grav_engine_name(*track_engine(tr)));
    cv_text(8, 16, text_w(&FONT_S, n) <= 186 ? &FONT_S : &FONT_XS, n, C_WHITE);
}

/* (0.35) LOCK's strip: the selected track's 16 steps on the white keys, a cell each: on in its colour, locked marked
 * white, held white, past its length dark; the one playing lighter */
static void draw_lock_strip(int32_t y)
{
    const uint16_t hue = TRACK_HUE[track_sel];
    for (uint32_t i = 0; i < SEQ_PAGE; i++) {
        const uint32_t s = ui.view_page * SEQ_PAGE + i;
        const int32_t x = 8 + (int32_t)i * 10 + (int32_t)(i / 4u) * 2;
        const int held = ui.lock_held >> i & 1u, locks = seq_lock_mask(&seq.pat, track_sel, s, 0) != 0;
        uint16_t c = s >= TRK.length ? C_LINE : TRK.step[s].on ? mix565(C_BLACK, hue, 170) : RGB(30, 30, 38);
        if (seq.playing && seq.step[track_sel] == (int32_t)s)
            c = mix565(c, C_WHITE, 90);
        cv_rect(x, y, 8, 8, held ? C_WHITE : c);
        if (locks)
            cv_rect(x + 2, y + 2, 4, 4, held ? C_BLACK : C_WHITE);
    }
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
    else if (ui.lock_mode)                   /* (0.35) LOCK: the steps for the keys */
        draw_lock_strip(y + 2);
    else
        cv_text(8, y + 2, &FONT_T, "WHITE 1-8 MUTE  BLACK 1-8 SOLO", C_GRAY);
    draw_cpu(178, y);
}

/* the page's state, in the bottom band's first line: a sound page's HOME */
static void draw_status(int32_t y)
{
    if (active_page == PAGE_TEMPO) {         /* (0.38: its keys tap) */
        cv_text(8, y + 2, &FONT_T, "HOME  NEXT PAGE", C_GRAY);
        cv_text(232 - text_w(&FONT_T, "ANY KEY  TAP TEMPO"), y + 2, &FONT_T, "ANY KEY  TAP TEMPO", C_GRAY);
    } else if (IS_SOUND_PAGE(active_page)) {
        cv_text(8, y + 2, &FONT_T, "HOME  NEXT PAGE", C_GRAY);
        cv_text(232 - text_w(&FONT_T, "HOLD HOME + SELECT  JUMP"), y + 2, &FONT_T, "HOLD HOME + SELECT  JUMP", C_DIM);
    }
}

/* (0.33) the effect's bottom band: its three knobs, small (as the mixer's); under them ECHO's TIME, its note value
 * and in ms at the tempo (0.32), or a moment's message; whether the effect is on; the load */
static void draw_fx_band(void)
{
    char t[48];
    const int32_t y = 43;
    draw_chips(CHIP_Y, 7u, 0);
    if (band_message(t, 116))
        cv_text(8, y + 2, &FONT_T, t, preset_import_failed ? C_HI : C_GRAY);
    else {
        const uint32_t i = (uint32_t)fx_params[FX_ECHO_TIME];
        cv_text(8, y, &FONT_XS, "ECHO", C_GRAY);
        cv_text(44, y, &FONT_XS, grav_echo_time_name(i), C_WHITE);
        int_text((int32_t)(grav_echo_seconds(i, (float)seq_bpm(&seq)) * 1000.0f + 0.5f), t);
        text_cat(t, " MS");
        cv_text(44 + text_w(&FONT_XS, grav_echo_time_name(i)) + 8, y, &FONT_XS, t, C_GRAY);
    }
    cv_text(128, y, &FONT_XS, "FX", C_GRAY);
    cv_text(146, y, &FONT_XS, fx_wanted() ? "ON" : "OFF", fx_wanted() ? C_HI : C_WHITE);
    draw_cpu(178, y);
}

/* (0.34) the ARP pages' bottom band: their knobs, small; under them the track and its arp's state (the notes held,
 * HOLD keeping them), or a moment's message; the load */
static void draw_arp_band(void)
{
    char t[48];
    const int32_t y = 43;
    const seq_arp_t *a = &seq.arp[track_sel];
    draw_chips(CHIP_Y, 7u, 0);
    if (band_message(t, 160))
        cv_text(8, y + 2, &FONT_T, t, preset_import_failed ? C_HI : C_GRAY);
    else if (!SYNTH_IS_DRUM(track_sel)) {
        cv_text(8, y, &FONT_XS, TRACK_NAME[track_sel], TRACK_HUE[track_sel]);
        if (arp_mode(track_sel) == SEQ_ARP_OFF)
            cv_text(32, y, &FONT_XS, "ARP OFF", C_GRAY);
        else {
            cv_text(32, y, &FONT_XS, "HELD", C_GRAY);
            int_text(a->n, t);
            cv_text(32 + text_w(&FONT_XS, "HELD") + 6, y, &FONT_XS, t, C_WHITE);
            if (a->latched)
                cv_text(92, y, &FONT_XS, "HOLD", C_HI);
        }
    }
    draw_cpu(178, y);
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
    {                                        /* (0.30) the selected track's engine, where Terykons was */
        const char *e = grav_engine_name(*track_engine(track_sel));
        cv_text(6, text_w(&FONT_B, e) <= 88 ? 6 : 8, text_w(&FONT_B, e) <= 88 ? &FONT_B : &FONT_XS, e, C_WHITE);
    }
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
    if (active_page != PAGE_SEQ)             /* (HOME's terricons: named in the bottom band; step mode too, 0.28) */
        draw_track_line(4);
    if (active_page == PAGE_GLO)
        draw_glo();
    else if (active_page == PAGE_MIDI)
        draw_midi_page();
    else if (active_page == PAGE_USB)
        draw_usb_page();
    else if (active_page == PAGE_SCREEN)
        draw_screen_page();
    else if (active_page == PAGE_DEBUG)
        draw_debug();
    else if (active_page == PAGE_SEQ)
        draw_seq();
    else if (active_page == PAGE_MIXER)
        draw_mixer_body(0);
    else if (IS_FX_PAGE(active_page))        /* (0.33) the lake; its knobs small, in the bottom band */
        draw_fx_page();
    else if (IS_ARP_PAGE(active_page))       /* (0.34) the arp's ladder; its knobs small, in the bottom band */
        draw_arp_page();
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
    if (active_page == PAGE_SEQ) {           /* HOME (0.29.2), step mode (0.30.1): the page's foot, then a band 32
                                              * high */
        cv_begin(240, HOME_FOOT, C_BLACK);
        if (ui.step_mode)
            draw_seq_body(-144);
        else
            draw_terricons(-144, plat_ms());
        cv_blit(0, 180);
        cv_begin(240, 240 - HOME_BAND_Y, C_BLACK);
        if (ui.step_mode)
            draw_seq_band();
        else
            draw_home_band();
        cv_blit(0, HOME_BAND_Y);
        return;
    }
    cv_begin(240, 60, C_BLACK);
    cv_rect(8, 0, 224, 1, C_LINE);
    if (active_page == PAGE_SEQ && !ui.step_mode)
        draw_home_band();
    else if (active_page == PAGE_SEQ)
        draw_seq_band();
    else if (active_page == PAGE_MIXER)
        draw_mixer_band();
    else if (IS_FX_PAGE(active_page))
        draw_fx_band();
    else if (IS_ARP_PAGE(active_page))
        draw_arp_band();
    else {
        if (ui.lock_mode) {                  /* (0.35) LOCK: the steps for the keys, in the page's state's place */
            draw_lock_strip(4);
            cv_text(184, 2, &FONT_XS, "LOCK", C_HI);
        } else
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
    ui.roll_lo = 0xFF;
    ui.last_n = 1;
    for (uint32_t k = 0; k < SEQ_NOTES; k++)
        ui.last_chord[k] = 60;
    ui.last_vel = 100;
    ui.shown_step = -2;
    touch_on(1);                             /* velocity and pressure: the keys' readings on */
    palette_set(theme_palette());            /* (0.25.1) GLO's SCREEN: the theme chosen (the start set the app's) */
    pb.open = 0;                             /* (0.27) no presets' browser at the start */
    ui.logo = plat_ms() < LOGO_MS;           /* (0.36.1) the logo kept a while; else the page at once */
    if (!ui.logo)
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
    if (!ui.step_mode) {                     /* (0.28: a sky moves on its own, 20 frames a second while all is quiet) */
        if (now - ui.grid_at >= frame_ms() && (levels_update(now) || (set.sky != SKY_OFF && now - ui.grid_at >= 50u)))
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

/* (0.33) the effect's pages: the lake redrawn while its water or its terricons move (~30 times a second, 10 while
 * the sound is busy), once more as all stills; not under a list */
static void fx_frame(uint32_t now)
{
    if (now - ui.grid_at < frame_ms() || list_open() || ui.splash_until)
        return;
    const int moving = levels_update(now) | lake_moving(now);
    if (moving || lake.shown)
        ui.redraw_grid = 1;
    lake.shown = (uint8_t)moving;
}

/* (0.34) the ARP pages: the ladder redrawn when the arp plays a note or its notes change (and as its flash fades);
 * the band when the notes held or HOLD's keeping change */
static void arp_frame(uint32_t now)
{
    if (now - ui.grid_at < frame_ms() || list_open() || ui.splash_until)
        return;
    const seq_arp_t *a = &seq.arp[track_sel];
    uint32_t sig = a->played * 977u + a->n * 31u + a->pos + arp_mode(track_sel) * 7919u;
    for (uint32_t i = 0; i < a->n && i < SEQ_ARP_NOTES; i++)
        sig = sig * 131u + a->note[i];
    const uint8_t band = (uint8_t)(a->n | a->latched << 4);
    if (band != ui.arp_band) {
        ui.arp_band = band;
        ui.redraw = 1;
    }
    if (sig != ui.arp_sig) {
        ui.arp_sig = sig;
        ui.arp_at = now;
        ui.redraw_grid = 1;
    } else if (now - ui.arp_at < 300u)
        ui.redraw_grid = 1;
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
    lake_listen(now);                        /* (0.33: the effect's levels, every frame) */
    if (active_page == PAGE_SEQ)
        seq_frame(now);
    else if (active_page == PAGE_MIXER)
        mixer_frame(now);
    else if (IS_FX_PAGE(active_page))
        fx_frame(now);
    else if (IS_ARP_PAGE(active_page))
        arp_frame(now);
    if (ui.splash_until && now >= ui.splash_until) {
        ui.splash_until = 0;
        ui.redraw = 1;
    }
    if (ui.logo) {                           /* (0.36.1) the logo at power-on: the FM-1 plays meanwhile, nothing drawn
                                              * over it until LOGO_MS since it started, or a key, button or knob */
        int touched = ui.keys || ui.btn;
        for (uint32_t i = 0; i < NE; i++)
            touched |= ui.turned_at[i] != 0u;
        if (now < LOGO_MS && !touched)
            return;
        ui.logo = 0;
        lcd_fill(0, 0, 240, 240, C_BLACK);
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
    fx_page = PAGE_FX_HALL;
    memset(seq_midi_sent, 0, sizeof seq_midi_sent);   /* (every track's) */
    seq_midi_owed = 0;
    memset(&chain, 0, sizeof chain);         /* (0.41: a chain is RAM's, gone at power-off) */
}

int app_host_cmd(const char *cmd, const char *a, const char *b, const char *c, const char *d)
{
    (void)a; (void)b; (void)c; (void)d;
    if (!strcmp(cmd, "preset_fail")) { preset_store_test_fail(atoi(a)); return 1; }
    if (!strcmp(cmd, "soundpage")) { sound_activate((uint32_t)atoi(a)); return 1; }
    if (!strcmp(cmd, "splash")) {            /* (0.36) the splash at power-on, as the FM-1 draws it (the pictures) */
        app_splash();
        return 1;
    }
    if (!strcmp(cmd, "sky")) {               /* (0.28) HOME's sky at once (the pictures): 1 NIGHT, 2 OFF, 3 DAY */
        set.sky = (uint8_t)clamp(atoi(a), SKY_NIGHT, SKY_DAY);
        ui.redraw = 1;
        return 1;
    }
    if (!strcmp(cmd, "save_moddest")) {
        settings_capture();
        settings_t legacy = set;
        legacy.mod[2] = (uint8_t)clamp(atoi(a), 0, 100);
        plat_store_save(OBJ_SETTINGS, &legacy, SET_SIZE_036);   /* (as 0.36 saved it: one LFO for all, 0.37) */
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
    if (!strcmp(cmd, "save038")) {           /* (0.39) the settings as 0.37..0.38 saved them: no MIDI page's */
        settings_capture();
        plat_store_save(OBJ_SETTINGS, &set, SET_SIZE_037);
        return 1;
    }
    if (!strcmp(cmd, "save036")) {           /* (0.37) the settings as 0.36 saved them: one envelope for every track
                                              * (ATTACK DECAY RELEASE), one LFO */
        settings_capture();
        settings_t legacy = set;
        legacy.env[0] = (uint8_t)clamp(atoi(a), 0, 100);
        legacy.env[1] = (uint8_t)clamp(atoi(b), 0, 100);
        legacy.env[2] = (uint8_t)clamp(atoi(c), 0, 100);
        plat_store_save(OBJ_SETTINGS, &legacy, SET_SIZE_036);
        return 1;
    }
    if (!strcmp(cmd, "save031")) {           /* (0.32) the settings as 0.31 saved them: Beads' knobs at rest, its
                                              * REVERB at a and DRY/WET at b */
        static const uint8_t BEADS[FX_N] = {75, 30, 65, 50, 50, 30, 0, 0};
        settings_t s;
        settings_capture();
        s = set;
        memcpy(s.fx, BEADS, sizeof BEADS);
        s.fx[FX_OLD_REVERB] = (uint8_t)clamp(atoi(a), 0, 100);
        s.fx[6] = (uint8_t)clamp(atoi(b), 0, 100);
        s.fx_layout = 0;
        plat_store_save(OBJ_SETTINGS, &s, sizeof s);
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
    else if (!strcmp(what, "chain_n"))       /* (0.41) the chain: its patterns, the one playing (-1: its first queued) */
        *got = chain.n;
    else if (!strcmp(what, "chain_pos"))
        *got = chain.pos;
    else if (!strcmp(what, "clip"))          /* (0.40) a copy to paste (1), its pattern (clip_from, -1 not known) */
        *got = bank_pattern(&bank, BANK_CLIP) != 0;
    else if (!strcmp(what, "clip_from"))
        *got = clip_from;
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
    else if (!strcmp(what, "lake_drops"))     /* (0.33) the lake's rings begun: drops, echoes, the tail's ripples */
        *got = (int)lake.drops;
    else if (!strcmp(what, "lake_echoes"))
        *got = (int)lake.echoes;
    else if (!strcmp(what, "lake_ripples"))
        *got = (int)lake.ripples;
    else if (!strncmp(what, "midi_ch", 7))   /* (0.39) GLO's MIDI: a track's channel (0 OFF), IN, CLOCK */
        *got = set.midi_ch[atoi(what + 7) % SYNTH_TRACKS];
    else if (!strcmp(what, "midi_in"))
        *got = set.midi_in;
    else if (!strcmp(what, "held_tracks")) {   /* (0.39) the tracks with a note held, a bit each */
        uint32_t m = 0;
        for (uint32_t i = 0; i < SYNTH_VOICES_MAX; i++)
            if (synth.voice_track[i] != SYNTH_NO_TRACK && synth.voice[i].gate)
                m |= 1u << synth.voice_track[i];
        *got = (int)m;
    }
    else if (!strcmp(what, "midi_clock"))
        *got = set.midi_clock;
    else if (!strcmp(what, "usb_audio"))     /* (0.42) GLO's USB: AUDIO; a track's loudest sample in the last render's
                                              * USB audio (usb_trk<track>; -1: not MULTI) */
        *got = set.usb_audio;
#ifdef PLAT_USB_TRACKS
    else if (!strncmp(what, "usb_trk", 7)) {
        const int16_t *b = plat_usb_tracks();
        int peak = -1;
        if (b) {
            peak = 0;
            for (uint32_t i = 0; i < APP_BLOCK; i++) {
                const int v = b[i * SYNTH_TRACKS + (uint32_t)atoi(what + 7) % SYNTH_TRACKS];
                if ((v < 0 ? -v : v) > peak)
                    peak = v < 0 ? -v : v;
            }
        }
        *got = peak;
    }
#endif
    else if (!strcmp(what, "lock_mode"))     /* (0.35) LOCK mode; the pattern's locks; the steps held; a step's lock
                                              * (lk<step>_<param>, -1 none); a knob heard locked (lkon<param>, -1) */
        *got = ui.lock_mode;
    else if (!strcmp(what, "locks"))
        *got = (int)seq.pat.nlocks;
    else if (!strcmp(what, "lock_held"))
        *got = (int)ui.lock_held;
    else if (!strncmp(what, "lkon", 4))
        *got = (int)locked(track_sel, (uint32_t)atoi(what + 4) % SEQ_LK_PARAMS, -1);
    else if (!strncmp(what, "lk", 2) && strchr(what, '_'))
        *got = seq_lock_get(&seq.pat, track_sel, (uint32_t)atoi(what + 2) - 1u, (uint32_t)atoi(strchr(what, '_') + 1));
    else if (!strncmp(what, "arp_", 4)) {    /* (0.34) the selected track's arp: its settings, notes held, played */
        const char *w = what + 4;
        *got = !strcmp(w, "mode") ? (int)arp_mode(track_sel) : !strcmp(w, "rate") ? (int)arp_rate(track_sel) :
               !strcmp(w, "oct") ? (int)arp_octaves(track_sel) : !strcmp(w, "gate") ? (int)arp_gate(track_sel) :
               !strcmp(w, "hold") ? (int)arp_hold(track_sel) : !strcmp(w, "held") ? (int)seq.arp[track_sel].n :
               !strcmp(w, "latched") ? (int)seq.arp[track_sel].latched :
               !strcmp(w, "played") ? (int)seq.arp[track_sel].played : -1;
    }
    else if (!strcmp(what, "lake_moving"))
        *got = lake_moving(plat_ms());
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
    else if (!strcmp(what, "save_sweep"))   /* (0.31) the white keys SAVE's sweep lights (0: none) */
        *got = (int32_t)save_sweep(plat_ms());
    else if (!strcmp(what, "roll_lo"))
        *got = ui.roll_lo;
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
    else if (!strcmp(what, "send"))          /* the selected track's SEND, LEVEL; the effect's RETURN (HALL's AMOUNT) */
        *got = set.send[track_sel];
    else if (!strcmp(what, "level"))
        *got = set.controls[track_sel].level;
    else if (!strcmp(what, "vol"))           /* (0.27) the selected track's fader */
        *got = set.vol[track_sel];
    else if (!strcmp(what, "fx_return"))
        *got = fx_params[FX_HALL];
    else if (!strcmp(what, "meter"))         /* the selected track's meter, 0..256 */
        *got = ui.meter[track_sel];
    else if (!strcmp(what, "sky"))           /* (0.29) GLO's SCREEN: HOME's sky, SKY_* (1 NIGHT, 2 OFF, 3 DAY) */
        *got = set.sky;
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
        *got = (int)lfo_destination(*param_of(PAGE_MOD, 2));
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
        *got = grav_fx_on();
    else if (!strcmp(what, "quality"))       /* TAPE's QUALITY: GRAV_FX_* */
        *got = fx_quality;
    else if (!strcmp(what, "fx_layout"))     /* (0.32) the settings' effect knobs: FX_LAYOUT */
        *got = set.fx_layout;
    else if (!strncmp(what, "fx", 2))        /* fxN: the effect's knob N (FX_*: 0 HALL .. 7 FEEDBACK) */
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
    } else if (!strncmp(what, "mod", 3))     /* (0.37: the selected track's) */
        *got = *param_of(PAGE_MOD, (uint32_t)atoi(what + 3) % 3u);
    else if (!strncmp(what, "env", 3))
        *got = *param_of(PAGE_ENV, (uint32_t)atoi(what + 3) % 3u);
    else
        return 0;
    return 1;
}
#endif
