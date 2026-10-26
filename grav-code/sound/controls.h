#pragma once
#include <stdint.h>

#define GRAV_CONTROL_BYTES 156u
#define GRAV_AUTO 255u
#define GRAV_CUSTOM_FM 1u
#define GRAV_DRUM_HOLD 2u
typedef struct {
    uint8_t tune, level, aux, flags, accent, reserved[3];
    uint8_t value[GRAV_CONTROL_BYTES];
} grav_controls_t;

enum { GC_AUTO, GC_BOOL, GC_SIGNED, GC_STEP, GC_FM, GC_FM_BOOL, GC_FM_SIGNED, GC_FM_BIT };
typedef struct {
    const char *name;
    uint8_t offset, max, kind, bit;
} grav_control_info_t;

#ifdef __cplusplus
extern "C" {
#endif
void grav_controls_init(grav_controls_t *c);
int grav_controls_valid(uint32_t engine, const grav_controls_t *c);
int grav_controls_fm(uint32_t engine);
uint32_t grav_control_pages(uint32_t engine);
const char *grav_control_page(uint32_t engine, uint32_t page); /* page 0 is first EXTRA, excludes OSC/OUTPUT */
int grav_control_info(uint32_t engine, uint32_t page, uint32_t knob, grav_control_info_t *info);
int grav_control_read(const grav_controls_t *c, const grav_control_info_t *info);
void grav_control_write(grav_controls_t *c, const grav_control_info_t *info, uint32_t value);
#ifdef __cplusplus
}
#endif
