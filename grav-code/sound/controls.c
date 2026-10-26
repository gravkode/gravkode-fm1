#include "controls.h"
#include "../dsp/grav_plaits.h"
#include <string.h>

_Static_assert(sizeof(grav_controls_t) == 164u, "extended sound format");
typedef struct { const char *name; uint8_t kind, max; } item_t;
#define A(n) {n,GC_AUTO,100}
#define S(n) {n,GC_SIGNED,100}
#define B(n) {n,GC_BOOL,1}
#define D(n,m) {n,GC_STEP,m}
static const item_t extra[GRAV_ENGINE_ALL][9] = {
    {A("AUX FOLD"), S("BIAS")},
    {S("FINE"), A("PHASE FB"), A("AMP FB"), A("SUB INDEX")},
    {A("PULSE"), A("SYNC"), A("SAW SHAPE"), A("SAW LEVEL"), A("SQR LEVEL"), A("AUX SYNC")},
    {A("DENSITY"), A("CUTOFF"), A("EXCITE Q"), A("ACCENT")},
    {A("BLEED"), A("FORMANT 2"), A("BALANCE"), A("Z CUTOFF"), A("Z SHAPE"), A("Z FORMANT")},
    {A("SLOPE"), A("EVEN"), A("ODD")},
    {A("STEPPED"), A("SLEW"), A("PHASE")},
    {A("REGIST"), A("TABLE MIX"), A("VOICE 1"), A("VOICE 2"), A("VOICE 3"), A("VOICE 4"), A("VOICE 5")},
    {S("SPEED"), A("PROSODY"), B("REPLAY"), D("WORD BANK",5)},
    {B("BURST"), A("GRAIN TUNE"), A("LINEAR FM")},
    {A("FILTER"), S("CUTOFF"), S("BAND 2")},
    {A("RESONANCE"), A("DIFFUSE"), A("SIZE")},
    {A("DENSITY"), A("CUTOFF"), A("EXCITE Q"), A("PLUCK"), A("ACCENT"), S("NONLINEAR")},
    {A("PUNCH"), A("SWEEP"), A("DRIVE"), A("ALT TONE"), A("ALT DECAY"), A("ALT DIRT"), A("ALT PUNCH"), A("ALT SWEEP")},
    {A("ALT TONE"), A("ALT DECAY"), A("ALT SNAPPY")},
    {A("ALT TONE"), A("ALT DECAY"), A("ALT NOISE")},
    {A("PULSE"), A("SUB"), A("POLES"), A("DRIVE"), A("WAVE")},
    {A("WAVE"), A("ALT AMOUNT"), A("ALT PULSE"), S("FINE")},
    {{0,0,0}}, {{0,0,0}}, {{0,0,0}},
    {S("Y OFFSET"), A("ASPECT"), A("X SCALE"), A("ROTATE")},
    {A("CUTOFF"), A("ENSEMBLE"), A("DEPTH"), A("RESONANCE"), A("SPREAD"), A("VOICE 1"), A("VOICE 2"), A("VOICE 3"), A("VOICE 4")},
    {B("CLOCK"), S("ENVELOPE"), A("BASS ENV"), D("ARP MODE",3), D("ARP RANGE",2), S("BASS TUNE")},
    {{0,0,0}}, {{0,0,0}}, {{0,0,0}}
};
#undef A
#undef B
#undef S
#undef D
static const uint8_t counts[GRAV_ENGINE_ALL] = {2,4,6,4,6,3,3,7,4,3,3,3,6,8,3,3,5,4,0,0,0,4,9,6,0,0,0};
/* Patch bytes, checked against offsetof(plaits::fm::Patch) in grav_plaits.cpp. DX packs OP6 first. */
typedef struct { const char *name; uint8_t offset, max, kind; } fm_item_t;
static const fm_item_t fm_global[7][3] = {
    {{"ALGORITHM",134,31,GC_FM},{"FEEDBACK",135,7,GC_FM},{"KEY SYNC",136,1,GC_FM_BOOL}},
    {{"TRANSPOSE",144,48,GC_FM_SIGNED},{"LFO RATE",137,99,GC_FM},{"LFO DELAY",138,99,GC_FM}},
    {{"PITCH MOD",139,99,GC_FM},{"AMP MOD",140,99,GC_FM},{"PM SENSE",143,7,GC_FM}},
    {{"LFO WAVE",142,5,GC_FM},{"LFO SYNC",141,1,GC_FM_BOOL},{0,0,0,0}},
    {{"RATE 1",126,99,GC_FM},{"RATE 2",127,99,GC_FM},{"RATE 3",128,99,GC_FM}},
    {{"RATE 4",129,99,GC_FM},{"LEVEL 1",130,99,GC_FM},{"LEVEL 2",131,99,GC_FM}},
    {{"LEVEL 3",132,99,GC_FM},{"LEVEL 4",133,99,GC_FM},{0,0,0,0}}
};
static const fm_item_t fm_operator[8][3] = {
    {{"LEVEL",16,99,GC_FM},{"COARSE",18,31,GC_FM},{"FINE",19,99,GC_FM}},
    {{"MODE",17,1,GC_FM_BOOL},{"DETUNE",20,14,GC_FM_SIGNED},{"ENABLE",155,1,GC_FM_BIT}},
    {{"RATE 1",0,99,GC_FM},{"RATE 2",1,99,GC_FM},{"RATE 3",2,99,GC_FM}},
    {{"RATE 4",3,99,GC_FM},{"LEVEL 1",4,99,GC_FM},{"LEVEL 2",5,99,GC_FM}},
    {{"LEVEL 3",6,99,GC_FM},{"LEVEL 4",7,99,GC_FM},{"RATE SCALE",13,7,GC_FM}},
    {{"BREAKPOINT",8,99,GC_FM},{"LEFT DEPTH",9,99,GC_FM},{"RIGHT DEP",10,99,GC_FM}},
    {{"LEFT CURVE",11,3,GC_FM},{"RIGHT CURV",12,3,GC_FM},{"VEL SENSE",15,7,GC_FM}},
    {{"AM SENSE",14,3,GC_FM},{0,0,0,0},{0,0,0,0}}
};

int grav_controls_fm(uint32_t e) { return e >= GRAV_ENGINE_SIX_OP_1 && e <= GRAV_ENGINE_SIX_OP_3; }
void grav_controls_init(grav_controls_t *c)
{
    memset(c, 0, sizeof *c);
    c->tune = 50; c->level = c->accent = 100;
    memset(c->value, GRAV_AUTO, sizeof c->value);
}
uint32_t grav_control_pages(uint32_t e) { return e >= GRAV_ENGINE_ALL ? 0 : grav_controls_fm(e) ? 55u : (counts[e] + 2u) / 3u; }
const char *grav_control_page(uint32_t e, uint32_t p)
{
    if (grav_controls_fm(e)) {
        static const char *const global[] = {"FM ROUTING","FM MOD","FM DEPTH","FM LFO","PITCH RATE","PITCH ENV","PITCH LEVEL"};
        static const char *const op[] = {"OSC","TUNE","RATES","ENV","LEVELS","KEYBOARD","CURVES","MOD"};
        return p < 7 ? global[p] : op[(p - 7) % 8];
    }
    static const char *const page[] = {"SHAPE","DETAIL","MIX"};
    return page[p % 3];
}
int grav_control_info(uint32_t e, uint32_t page, uint32_t k, grav_control_info_t *i)
{
    if (e >= GRAV_ENGINE_ALL || k >= 3 || page >= grav_control_pages(e)) return 0;
    memset(i, 0, sizeof *i);
    if (grav_controls_fm(e)) {
        const fm_item_t *f;
        const uint32_t op = page < 7 ? 0 : 5u - (page - 7u) / 8u;
        f = page < 7 ? &fm_global[page][k] : &fm_operator[(page - 7) % 8][k];
        if (!f->name) return 0;
        i->name = f->name; i->max = f->max; i->kind = f->kind;
        i->offset = (uint8_t)(f->offset + (page < 7 || f->kind == GC_FM_BIT ? 0 : op * 21));
        i->bit = (uint8_t)op;
    } else {
        const uint32_t n = page * 3u + k;
        if (n >= counts[e]) return 0;
        i->name = extra[e][n].name; i->offset = (uint8_t)n; i->kind = extra[e][n].kind;
        i->max = extra[e][n].max;
    }
    return 1;
}
int grav_control_read(const grav_controls_t *c, const grav_control_info_t *i)
{
    return i->kind == GC_FM_BIT ? (c->value[i->offset] >> i->bit) & 1u : c->value[i->offset];
}
void grav_control_write(grav_controls_t *c, const grav_control_info_t *i, uint32_t v)
{
    if (i->kind == GC_FM_BIT)
        c->value[i->offset] = (uint8_t)((c->value[i->offset] & ~(1u << i->bit)) | ((v != 0) << i->bit));
    else c->value[i->offset] = (uint8_t)v;
    if (i->kind >= GC_FM) c->flags |= GRAV_CUSTOM_FM;
}
int grav_controls_valid(uint32_t e, const grav_controls_t *c)
{
    if (e >= GRAV_ENGINE_ALL || c->tune > 100 || c->level > 100 || c->aux > 100 || c->accent > 100 ||
        (c->flags & ~(GRAV_CUSTOM_FM | GRAV_DRUM_HOLD)) || c->reserved[0] || c->reserved[1] || c->reserved[2]) return 0;
    if (!grav_controls_fm(e) && (c->flags & GRAV_CUSTOM_FM)) return 0;
    if (grav_controls_fm(e) && !(c->flags & GRAV_CUSTOM_FM)) return 1;
    for (uint32_t p = 0; p < grav_control_pages(e); p++)
        for (uint32_t k = 0; k < 3; k++) {
            grav_control_info_t i;
            if (grav_control_info(e, p, k, &i) && i.kind != GC_FM_BIT) {
                const uint32_t v = c->value[i.offset];
                if (v > i.max && (i.kind >= GC_FM || v != GRAV_AUTO)) return 0;
            }
        }
    if (grav_controls_fm(e)) {
        if (c->value[155] > 63) return 0;
        for (uint32_t n = 145; n < 155; n++) if (c->value[n] > 127) return 0;
    } else
        for (uint32_t n = counts[e]; n < GRAV_CONTROL_BYTES; n++) if (c->value[n] != GRAV_AUTO) return 0;
    return 1;
}
