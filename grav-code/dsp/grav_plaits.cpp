#include "plaits/dsp/engine/waveshaping_engine.h"

static plaits::WaveshapingEngine engine;
static bool initialized = false;

extern "C" void grav_plaits_init(void) {
    engine.Init(nullptr);
    initialized = true;
}

extern "C" void grav_plaits_render(
    float note,
    float* out,
    float* aux,
    unsigned int size,
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
    engine.Render(p, out, aux, size, &already_enveloped);
}
