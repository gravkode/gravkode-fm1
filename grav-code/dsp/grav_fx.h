/* GRAV CODE's effect (0.32): the tracks' sends through ECHO, a delay in time with the tempo, and HALL, Mutable
 * Instruments Beads' reverb, both coloured by TAPE's QUALITY (Beads' media); the dry round them (grav_fx.cpp) */
#pragma once
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* QUALITY, from the cleanest: Beads' media in reverse order */
enum { GRAV_FX_DIGITAL = 0, GRAV_FX_VINTAGE, GRAV_FX_CLEAN_TAPE, GRAV_FX_DIRTY_TAPE, GRAV_FX_QUALITIES };

/* ECHO's TIME: note values, from a 32nd to a half (T a triplet's, . a dotted one) */
#define GRAV_ECHO_TIMES 12u

typedef struct {
    float hall, time, tone;              /* HALL: AMOUNT (its send), TIME (the tail), TONE (dark to bright), 0..1 */
    float diffuse, mod;                  /* TAPE: DIFFUSE (the reflections' density), MOD (the tail's movement), 0..1 */
    float echo, feedback;                /* ECHO: AMOUNT (its send), FEEDBACK, 0..1 */
    uint8_t echo_time;                   /* ECHO: TIME, 0..GRAV_ECHO_TIMES - 1 */
    uint8_t quality;                     /* TAPE: QUALITY, GRAV_FX_* */
    float bpm;                           /* the tempo ECHO's TIME counts in */
} grav_fx_params_t;

void grav_fx_init(void);
/* n samples of the mono send (in) through the effect into l and r, the dry in them. With AMOUNT at 0 on both it
 * stops taking in; once its tails have died away it is off: l and r are in, and it takes no time */
void grav_fx_process(const grav_fx_params_t *p, const float *in, float *l, float *r, uint32_t n);
int grav_fx_on(void);                    /* running (not off) */
int grav_fx_busy(const grav_fx_params_t *p);   /* running, or to start with p: the render's choice of core */
const char *grav_echo_time_name(uint32_t i);   /* ECHO's TIME on the screen: 1/8, 1/8T, 1/8. ... */
float grav_echo_seconds(uint32_t i, float bpm);   /* ECHO's TIME at a tempo, before it is fitted to the line */
float grav_echo_heard(uint32_t i, float bpm);     /* (0.33) and as it is heard: halved until it fits the line */
/* (0.33) the screen's lake: the loudest sample since the last take of what was sent in and of HALL's tail; taking
 * them sets them to 0. Measured as the effect runs (off: what is sent in alone), on either core */
void grav_fx_meters(float *in, float *hall);

#ifdef __cplusplus
}
#endif
