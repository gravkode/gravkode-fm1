/* SPDX-License-Identifier: MPL-2.0
 * Copyright (C) 2026 Charles Vestal */
/* Terykons, a synthesizer for the M-VAVE FM-1 on Mutable Instruments Plaits' engines, grown from fm1-base's
 * template. This header is the app's half of the contract with the platform (device/unity.c, host/plat_host.c). */
#pragma once
#include <stdint.h>
#include "plat.h"

#define APP_NAME "TERYKONS"
#define APP_SPLASH_RGB RGB(60, 210, 185)   /* (0.25.1) the name on the splash screen, in the theme's teal */
#define APP_SPLASH                       /* (0.36) app_splash: the logo, Terykons under its ridge */
void app_splash(void);
#define APP_PALETTE 6u                   /* (0.25.1) device/gfx.c PALETTES: TEAL (amber until 0.25) */
#define APP_BLOCK 256u                   /* frames per audio block: 5.8 ms at 44.1 kHz */
#define APP_TOUCH                        /* velocity and pressure from the keys (lib/touch.h) */
#define APP_OVERLOAD                     /* app_overload: the render ran long, a voice falls silent */
void app_overload(void);
#define APP_SYSEX                        /* read-only dual-core debug snapshot over USB-MIDI */
int app_sysex(const uint8_t *p, uint32_t n);

/* Object 2 holds the old 0.21 preset bank, retained for resumable migration. New expanded presets use their own
 * log E5000..ED000 (presets/store.c). Settings, panel calibration, legacy SEQ/DRUMS and the pattern bank keep
 * their addresses. No radio since 0.10: older BLE contents are rejected by size/magic. */
enum { OBJ_SETTINGS, OBJ_PANEL, OBJ_PRESETS, OBJ_SEQ, OBJ_DRUMS, OBJ_NOBJ };
#define APP_STORE_MAP {0xFC000u, 0xDC000u, 0xDE000u, 0xDA000u, 0xD8000u}

/* the contract: the platform calls these */
void app_init(void);                     /* boot: the engine, then the saved settings (or defaults) */
void app_render(int32_t *lr, uint32_t n, uint32_t master_q12);   /* audio interrupt: n stereo frames, 24-bit */
void app_panic(void);                    /* silence now (before an update rewrites the flash) */
void app_ota_mute(int active);           /* an update running (1): no sound until it ends or is given up (0) */
void app_ui_init(void);                  /* the first screen, after the splash */
void app_frame(void);                    /* the UI: input and a redraw, about 60 times a second */
void app_input_only(void);               /* between frames: input only */
void app_say(const char *a, const char *b);   /* a short message on the screen and to a screen reader */

/* the host simulator's checks (host/plat_host.c "expect"): octave, wave, note (-1 none), dirty, turnsN */
#define APP_HOST_EXPECT
int app_expect(const char *what, int *got);
/* and its own command: save012 (the settings as 0.12 saved them, before the effect: a reboot loads that) */
#define APP_HOST_CMD
int app_host_cmd(const char *cmd, const char *a, const char *b, const char *c, const char *d);
/* and its reboot: the UI's state forgotten, as the FM-1's RAM is at a restart */
#define APP_HOST_REBOOT
void app_host_reboot(void);
