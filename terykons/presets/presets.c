#include "presets.h"
#include "../dsp/grav_plaits.h"
#include <string.h>

_Static_assert(sizeof(preset_t) == 192 && __builtin_offsetof(preset_t, mod_set) == PRESET_SIZE_036,
               "expanded preset record layout (0.37: its LFO and envelope after 0.22's)");

typedef struct { char name[PRESET_TITLE]; uint8_t osc[3]; } factory_t;   /* (0.27: names up to 16, the browser's) */
#define F(n,a,b,c) {n,{a,b,c}}
#define INIT F("INIT",65,50,75)
/* Starting points, rather than imitations of named instruments. Engine IDs are the saved IDs, not ALGO order.
 * These tables live in flash. Listening and refining the bank on the instrument is a separate musical task. */
static const factory_t factory[GRAV_ENGINE_ALL][PRESET_FACTORY] = {
    {INIT,F("SOFT FOLD",20,35,35),F("BRASS FOLD",55,30,60),F("HARD FOLD",85,70,90)},
    {INIT,F("SINE BELL",25,10,35),F("FM BASS",40,20,0),F("METAL",85,70,85)},
    {INIT,F("SAW DUO",0,65,20),F("SQUARE DUO",75,0,15),F("DETUNED",65,65,85)},
    {INIT,F("WOOD",25,35,10),F("METAL",60,70,70),F("GLASS",85,85,95)},
    {INIT,F("LOW GRAIN",20,30,20),F("VOWEL",55,55,50),F("HIGH GRAIN",85,75,85)},
    {INIT,F("ROUND",20,20,25),F("HOLLOW",50,70,50),F("BRIGHT",85,40,90)},
    {INIT,F("TABLE A",20,20,0),F("TABLE B",50,60,35),F("TABLE C",80,35,80)},
    {INIT,F("OPEN",20,20,15),F("MINOR",50,40,45),F("WIDE",75,75,85)},
    {INIT,F("VOWEL",30,20,0),F("FORMANT",70,65,25),F("WORDS",50,50,85)},
    {INIT,F("SPARSE",20,30,20),F("CLUSTER",55,60,60),F("DENSE",90,80,85)},
    {INIT,F("DARK",35,20,20),F("WHISTLE",65,85,60),F("AIR",95,15,90)},
    {INIT,F("DUST",20,30,35),F("RAIN",65,65,65),F("BURST",95,85,90)},
    {INIT,F("PLUCK",15,25,20),F("ROUND",35,55,50),F("RING",80,85,85)},
    {INIT,F("SHORT",35,20,35),F("DEEP",50,65,65),F("HARD",85,45,95)},
    {INIT,F("TIGHT",40,20,45),F("WIRE",65,45,80),F("LONG",80,85,65)},
    {INIT,F("CLOSED",45,10,40),F("OPEN",65,80,65),F("BRIGHT",90,40,90)},
    {INIT,F("SUB",20,20,10),F("ACID",45,50,85),F("OPEN",90,75,25)},
    {INIT,F("ROUND",20,25,20),F("BENT",60,60,50),F("HARD",90,85,85)},
    {INIT,F("PATCH 01",50,50,0),F("PATCH 12",65,50,35),F("PATCH 27",75,60,82)},
    {INIT,F("PATCH 01",50,50,0),F("PATCH 12",65,50,35),F("PATCH 27",75,60,82)},
    {INIT,F("PATCH 01",50,50,0),F("PATCH 12",65,50,35),F("PATCH 27",75,60,82)},
    {INIT,F("SMALL",20,25,15),F("ORBIT",55,60,50),F("WIDE",90,80,85)},
    {INIT,F("SOFT",20,20,15),F("ENSEMBLE",70,50,50),F("HIGH",90,85,85)},
    {INIT,F("PULSE",0,25,0),F("CHORD",20,60,45),F("ARPEGGIO",85,40,80)},
    {INIT,F("SHORT",35,20,35),F("DEEP",50,65,65),F("HARD",85,45,95)},
    {INIT,F("TIGHT",40,20,45),F("WIRE",65,45,80),F("LONG",80,85,65)},
    {INIT,F("CLOSED",45,10,40),F("OPEN",65,80,65),F("BRIGHT",90,40,90)}
};
#undef INIT
#undef F

static int drum_engine(uint32_t e) { return (e >= 13u && e <= 15u) || (e >= 24u && e < GRAV_ENGINE_ALL); }
int preset_valid(const preset_t *p)
{
    if (p->engine >= GRAV_ENGINE_ALL || p->drum > 1 ||
        (p->drum ? !drum_engine(p->engine) : p->engine >= GRAV_ENGINE_COUNT) || p->tune > 100 ||
        !grav_controls_valid(p->engine, &p->controls))
        return 0;
    for (uint32_t i = 0; i < 3; i++)
        if (p->osc[i] > 100 || p->shape[i] > 100) return 0;
    if (p->mod_set == PRESET_MOD)            /* (0.37) its LFO and envelope */
        for (uint32_t i = 0; i < 3; i++)
            if (p->drum || p->lfo[i] > 100 || p->env[i] > 100) return 0;
    for (uint32_t i = 0; i < sizeof p->name; i++) {
        if (!p->name[i]) return i != 0;
        if ((uint8_t)p->name[i] < 32 || (uint8_t)p->name[i] > 126) return 0;
    }
    return 0;
}
void presets_init(preset_bank_t *b)
{
    memset(b, 0, sizeof *b);
    for (uint32_t s = 0; s < PRESET_USERS; s++) b->live[s] = -1;
}
int preset_used(const preset_bank_t *b, uint32_t slot)
{
    return preset_user(b, slot) != 0;
}
int presets_valid(const preset_bank_t *b)
{
    if (b->n_pending > PRESET_PENDING) return 0;
    for (uint32_t i = 0; i < PRESET_USERS; i++)
        if (preset_used(b, i) && !preset_valid(preset_user(b, i))) return 0;
    return 1;
}
int preset_put(preset_bank_t *b, uint32_t slot, const preset_t *p)
{
    if (slot >= PRESET_USERS || !preset_valid(p)) return 0;
    uint32_t n;
    for (n = 0; n < b->n_pending; n++) if (b->pending_slot[n] == slot) break;
    if (n == PRESET_PENDING) return 0;
    if (n == b->n_pending) b->n_pending++;
    b->pending_slot[n] = (uint8_t)slot;
    b->pending[n] = *p;
    return 1;
}
int preset_get(const preset_bank_t *b, uint32_t e, int drum, int id, preset_t *p)
{
    if (e >= GRAV_ENGINE_ALL || (drum ? !drum_engine(e) : e >= GRAV_ENGINE_COUNT) || id < 0) return 0;
    if (id < (int)PRESET_FACTORY) {
        memset(p, 0, sizeof *p);
        grav_controls_init(&p->controls);
        p->engine = (uint8_t)e;
        p->drum = !!drum;
        p->tune = 50;
        memcpy(p->osc, factory[e][id].osc, 3);
        memcpy(p->name, factory[e][id].name, sizeof p->name - 1u);   /* (a long one cut: preset_title has it whole) */
        p->name[sizeof p->name - 1u] = 0;
        if (!drum) {                         /* (0.37) a factory sound: the LFO and the envelope as they come */
            static const uint8_t lfo[3] = PRESET_LFO_DEFAULT, env[3] = PRESET_ENV_DEFAULT;
            p->mod_set = PRESET_MOD;
            memcpy(p->lfo, lfo, 3);
            memcpy(p->env, env, 3);
        }
        if (drum) {
            /* Dedicated drums expose these controls independently (synth-track drums retain Plaits' fold). */
            static const uint8_t shape[3][4][3] = {
                {{75,75,0},{35,45,0},{65,80,10},{95,95,55}},
                {{75,0,100},{45,0,100},{90,20,100},{65,30,100}},
                {{75,0,100},{40,0,100},{65,10,100},{90,25,100}}
            };
            const uint32_t kind = e >= 24 ? e - 24 : e - 13;
            memcpy(p->shape, shape[kind][id], 3);
            p->osc[2] = 0;                  /* unused on a dedicated drum */
        }
        return 1;
    }
    const uint32_t slot = (uint32_t)id - PRESET_FACTORY;
    const preset_t *saved = preset_user(b, slot);
    if (!saved || saved->engine != e || saved->drum != !!drum) return 0;
    *p = *saved;
    return 1;
}
const char *preset_title(const preset_bank_t *b, uint32_t e, int drum, int id)
{
    preset_t p;
    if (!preset_get(b, e, drum, id, &p)) return 0;
    if (id < (int)PRESET_FACTORY) return factory[e][id].name;
    return preset_user(b, (uint32_t)id - PRESET_FACTORY)->name;
}

const char *const PRESET_CATEGORY[PRESET_CATEGORIES] = {"SYNTH", "BASS", "LEAD", "PAD", "PLUCK", "KEYS", "DRUM", "FX",
                                                        "USER"};

uint32_t preset_category(uint32_t e, int id, const char *name)
{
    if (id >= (int)PRESET_FACTORY) return PRESET_CAT_USER;
    for (const char *c = name; c && *c; c++)   /* by its name first: a BASS, a SUB */
        if ((c[0] == 'B' && c[1] == 'A' && c[2] == 'S' && c[3] == 'S') || (c[0] == 'S' && c[1] == 'U' && c[2] == 'B'))
            return PRESET_CAT_BASS;
    switch (e) {                             /* then by its engine */
    case GRAV_ENGINE_VA_VCF: return PRESET_CAT_BASS;
    case GRAV_ENGINE_WAVESHAPING: case GRAV_ENGINE_PHASE_DISTORTION: case GRAV_ENGINE_CHIPTUNE: return PRESET_CAT_LEAD;
    case GRAV_ENGINE_CHORDS: case GRAV_ENGINE_SWARM: case GRAV_ENGINE_STRING_MACHINE: case GRAV_ENGINE_WAVE_TERRAIN:
        return PRESET_CAT_PAD;
    case GRAV_ENGINE_STRING: case GRAV_ENGINE_MODAL: return PRESET_CAT_PLUCK;
    case GRAV_ENGINE_FM: case GRAV_ENGINE_ADDITIVE: case GRAV_ENGINE_SIX_OP_1: case GRAV_ENGINE_SIX_OP_2:
    case GRAV_ENGINE_SIX_OP_3: return PRESET_CAT_KEYS;
    case GRAV_ENGINE_GRAIN: case GRAV_ENGINE_SPEECH: case GRAV_ENGINE_NOISE: case GRAV_ENGINE_PARTICLE:
        return PRESET_CAT_FX;
    case GRAV_ENGINE_BASS_DRUM: case GRAV_ENGINE_SNARE_DRUM: case GRAV_ENGINE_HI_HAT: case GRAV_ENGINE_SYN_KICK:
    case GRAV_ENGINE_SYN_SNARE: case GRAV_ENGINE_RING_HAT: return PRESET_CAT_DRUM;
    default: return PRESET_CAT_SYNTH;
    }
}

int preset_match(const preset_bank_t *b, const preset_t *p)
{
    preset_t q;
    /* A saved sound takes precedence over an identical factory starting point. Ignore its name. */
    for (int k = 0; k < (int)(PRESET_USERS + PRESET_FACTORY); k++) {
        const int id = k < (int)PRESET_USERS ? k + (int)PRESET_FACTORY : k - (int)PRESET_USERS;
        if (preset_get(b, p->engine, p->drum, id, &q) && !memcmp(p->osc, q.osc, 3) &&
            !memcmp(&p->controls, &q.controls, sizeof p->controls) &&
            (!p->drum || (p->tune == q.tune && !memcmp(p->shape, q.shape, 3))) &&
            (p->mod_set != PRESET_MOD || q.mod_set != PRESET_MOD ||   /* (0.37: one saved before: any LFO, envelope) */
             (!memcmp(p->lfo, q.lfo, 3) && !memcmp(p->env, q.env, 3)))) return id;
    }
    return PRESET_CUSTOM;
}
