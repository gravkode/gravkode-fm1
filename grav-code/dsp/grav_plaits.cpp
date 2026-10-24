/* Mutable Instruments Plaits' engines for GRAV CODE: built, chosen and rendered here for the C side (grav_plaits.h) */
#include <new>

#include "grav_plaits.h"

#include "plaits/dsp/dsp.h"
#include "plaits/dsp/engine/fm_engine.h"
#include "plaits/dsp/engine/modal_engine.h"
#include "plaits/dsp/engine/virtual_analog_engine.h"
#include "plaits/dsp/engine/waveshaping_engine.h"
#include "stmlib/utils/buffer_allocator.h"

static_assert(GRAV_PLAITS_BLOCK == plaits::kMaxBlockSize, "grav_plaits.h: the block is Plaits' largest");

namespace {

// Room for one engine. The FM-1 runs no C++ static constructors (its link keeps no .init_array), so a static
// engine object would stay zeroed, its vtable pointer too, and the first virtual call (an Init calling Reset)
// would jump to nowhere. grav_plaits_init builds each engine in its slot instead (placement new).
template<typename T>
struct Slot {
  alignas(T) unsigned char bytes[sizeof(T)];
};

Slot<plaits::WaveshapingEngine> waveshaping_slot;
Slot<plaits::FMEngine> fm_slot;
Slot<plaits::VirtualAnalogEngine> virtual_analog_slot;
Slot<plaits::ModalEngine> modal_slot;

plaits::Engine* engines[GRAV_ENGINE_COUNT];

// Each engine's output level, so they sound about as loud as one another. Measured in the host simulator at the
// defaults (TIMBRE 65, MORPH 50, HARMONICS 75, C4 at velocity 100), the RMS while held: Waveshaping -34.6 dB, FM
// -31.2, Virtual Analog -36.2, Modal -17.2; Modal comes down 16 dB to the others. (Plaits balances its engines with
// gains of its own and a limiter; GRAV CODE keeps the first three as they always sounded.) Modal's level follows
// TIMBRE, its exciter's density, as on Plaits: quieter below the default, a sparse crackle at 0.
const float engine_gain[GRAV_ENGINE_COUNT] = { 1.0f, 1.0f, 1.0f, 0.16f };

// The engines' scratch RAM, shared as Plaits' Voice shares it: one engine renders at a time, and the one chosen is
// Reset first. (Virtual Analog and Modal each take a block of floats here.)
char engine_ram[1024] __attribute__((aligned(8)));

uint32_t current;
bool initialized;

}  // namespace

extern "C" void grav_plaits_init(void) {
  engines[GRAV_ENGINE_WAVESHAPING] = new (waveshaping_slot.bytes) plaits::WaveshapingEngine();
  engines[GRAV_ENGINE_FM] = new (fm_slot.bytes) plaits::FMEngine();
  engines[GRAV_ENGINE_VIRTUAL_ANALOG] = new (virtual_analog_slot.bytes) plaits::VirtualAnalogEngine();
  engines[GRAV_ENGINE_MODAL] = new (modal_slot.bytes) plaits::ModalEngine();

  stmlib::BufferAllocator allocator(engine_ram, sizeof(engine_ram));
  for (uint32_t i = 0; i < GRAV_ENGINE_COUNT; ++i) {
    allocator.Free();
    engines[i]->Init(&allocator);
  }
  current = GRAV_ENGINE_WAVESHAPING;
  initialized = true;
}

extern "C" void grav_plaits_render(const grav_plaits_params_t* in, float* out, float* aux, uint32_t size) {
  if (!initialized) {
    grav_plaits_init();
  }
  uint32_t engine = in->engine < GRAV_ENGINE_COUNT ? in->engine : GRAV_ENGINE_WAVESHAPING;
  if (engine != current) {
    current = engine;
    engines[current]->Reset();
  }

  // Only Modal reads the trigger. A new note strikes its resonator (the rising edge); while the note is held it is
  // excited on, as Plaits excites it with nothing in its trigger input (dust, as dense as TIMBRE is bright), so it
  // sounds for as long as the others do and GRAV CODE's envelope and LFO shape it alike; let go, it rings out.
  const plaits::TriggerState held = in->gate ? plaits::TRIGGER_UNPATCHED : plaits::TRIGGER_LOW;
  plaits::EngineParameters p = {};
  p.trigger = in->strike ? plaits::TRIGGER_RISING_EDGE : held;
  p.note = in->note;
  p.timbre = in->timbre;
  p.morph = in->morph;
  p.harmonics = in->harmonics;
  p.accent = in->accent;

  bool already_enveloped = false;
  for (uint32_t done = 0; done < size; ) {
    uint32_t n = size - done < GRAV_PLAITS_BLOCK ? size - done : GRAV_PLAITS_BLOCK;
    engines[current]->Render(p, out + done, aux + done, n, &already_enveloped);
    p.trigger = held;
    done += n;
  }
  const float gain = engine_gain[current];
  if (gain != 1.0f) {
    for (uint32_t i = 0; i < size; ++i) {
      out[i] *= gain;
      aux[i] *= gain;
    }
  }
}
