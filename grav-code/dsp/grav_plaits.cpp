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
    unsigned int size
) {
    if (!initialized) {
        grav_plaits_init();
    }

    plaits::EngineParameters p = {};
    p.trigger = plaits::TRIGGER_UNPATCHED;
    p.note = note;
    p.timbre = 0.65f;
    p.morph = 0.5f;
    p.harmonics = 0.75f;
    p.accent = 1.0f;

    bool already_enveloped = false;
    engine.Render(p, out, aux, size, &already_enveloped);
}
