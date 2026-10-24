/* Mutable Instruments Plaits' engines for GRAV CODE: built, chosen and rendered here for the C side (grav_plaits.h) */
#include <new>

#include "grav_plaits.h"

#include "plaits/dsp/dsp.h"
#include "plaits/dsp/engine/fm_engine.h"
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

plaits::Engine* engines[GRAV_ENGINE_COUNT];

// The engines' scratch RAM, shared as Plaits' Voice shares it: one engine renders at a time, and the one chosen is
// Reset first. (Virtual Analog takes a block of floats here.)
char engine_ram[1024] __attribute__((aligned(8)));

uint32_t current;
bool initialized;

}  // namespace

extern "C" void grav_plaits_init(void) {
  engines[GRAV_ENGINE_WAVESHAPING] = new (waveshaping_slot.bytes) plaits::WaveshapingEngine();
  engines[GRAV_ENGINE_FM] = new (fm_slot.bytes) plaits::FMEngine();
  engines[GRAV_ENGINE_VIRTUAL_ANALOG] = new (virtual_analog_slot.bytes) plaits::VirtualAnalogEngine();

  stmlib::BufferAllocator allocator(engine_ram, sizeof(engine_ram));
  for (uint32_t i = 0; i < GRAV_ENGINE_COUNT; ++i) {
    allocator.Free();
    engines[i]->Init(&allocator);
  }
  current = GRAV_ENGINE_WAVESHAPING;
  initialized = true;
}

extern "C" void grav_plaits_render(
    float note,
    float* out,
    float* aux,
    uint32_t size,
    uint32_t engine,
    float timbre,
    float morph,
    float harmonics) {
  if (!initialized) {
    grav_plaits_init();
  }
  if (engine >= GRAV_ENGINE_COUNT) {
    engine = GRAV_ENGINE_WAVESHAPING;
  }
  if (engine != current) {
    current = engine;
    engines[current]->Reset();
  }

  plaits::EngineParameters p = {};
  p.trigger = plaits::TRIGGER_UNPATCHED;
  p.note = note;
  p.timbre = timbre;
  p.morph = morph;
  p.harmonics = harmonics;
  p.accent = 1.0f;

  bool already_enveloped = false;
  for (uint32_t done = 0; done < size; ) {
    uint32_t n = size - done < GRAV_PLAITS_BLOCK ? size - done : GRAV_PLAITS_BLOCK;
    engines[current]->Render(p, out + done, aux + done, n, &already_enveloped);
    done += n;
  }
}
