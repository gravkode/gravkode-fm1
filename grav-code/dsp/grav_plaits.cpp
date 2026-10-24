#include "plaits/dsp/engine/waveshaping_engine.h"
#include "plaits/dsp/engine/fm_engine.h"
#include "plaits/dsp/engine/virtual_analog_engine.h"

static plaits::WaveshapingEngine engine;
static plaits::FMEngine fm_engine;
static plaits::VirtualAnalogEngine va_engine;
static bool initialized = false;

extern "C" void grav_plaits_init(void) {
    engine.Init(nullptr);
    fm_engine.Init(nullptr);
    va_engine.Init(nullptr);
    initialized = true;
}

extern "C" void grav_plaits_render(
    float note,
    float* out,
    float* aux,
    unsigned int size,
    unsigned int algorithm,
    float timbre,
    float morph,
    float harmonics
) {
    if (!initialized) {
        grav_plaits_init();
    }

    plaits::EngineParameters p = {};
    p.trigger = plaits::TRIGGER_UNPATCHED;
    p.note = note;
    p.timbre = timbre;
    p.morph = morph;
    p.harmonics = harmonics;
    p.accent = 1.0f;

    bool already_enveloped = false;
    if (algorithm == 2)
        va_engine.Render(p, out, aux, size, &already_enveloped);
    else if (algorithm == 1)
        fm_engine.Render(p, out, aux, size, &already_enveloped);
    else
        engine.Render(p, out, aux, size, &already_enveloped);
}
