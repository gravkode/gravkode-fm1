/* Mutable Instruments Plaits' engines for GRAV CODE: built, chosen and rendered here for the C side (grav_plaits.h) */
#include <new>

#include "grav_plaits.h"

#include "plaits/dsp/dsp.h"
#include "plaits/dsp/engine/additive_engine.h"
#include "plaits/dsp/engine/bass_drum_engine.h"
#include "plaits/dsp/engine/chord_engine.h"
#include "plaits/dsp/engine/fm_engine.h"
#include "plaits/dsp/engine/grain_engine.h"
#include "plaits/dsp/engine/hi_hat_engine.h"
#include "plaits/dsp/engine/modal_engine.h"
#include "plaits/dsp/engine/noise_engine.h"
#include "plaits/dsp/engine/particle_engine.h"
#include "plaits/dsp/engine/snare_drum_engine.h"
#include "plaits/dsp/engine/speech_engine.h"
#include "plaits/dsp/engine/string_engine.h"
#include "plaits/dsp/engine/swarm_engine.h"
#include "plaits/dsp/engine/virtual_analog_engine.h"
#include "plaits/dsp/engine/waveshaping_engine.h"
#include "plaits/dsp/engine/wavetable_engine.h"
#include "plaits/dsp/engine2/chiptune_engine.h"
#include "plaits/dsp/engine2/phase_distortion_engine.h"
#include "plaits/dsp/engine2/six_op_engine.h"
#include "plaits/dsp/engine2/string_machine_engine.h"
#include "plaits/dsp/engine2/virtual_analog_vcf_engine.h"
#include "plaits/dsp/engine2/wave_terrain_engine.h"
#include "plaits/resources.h"
#include "stmlib/utils/buffer_allocator.h"

static_assert(GRAV_PLAITS_BLOCK >= plaits::kBlockSize, "grav_plaits.h: a chunk renders a block at least");

#ifdef PLAT_HOST
#define GRAV_POOL
#else
#define GRAV_POOL __attribute__((section(".pool")))   /* the FM-1's RAM for big buffers (plat.h PLAT_POOL) */
#endif

namespace {

// What a note does to an engine's trigger input, as on Plaits:
enum Mode : uint8_t {
  FREE,    // nothing patched: it sounds as long as the note (most engines do not read the trigger at all)
  BOWED,   // struck when a note begins, then excited on while it is held (unpatched), and let ring once let go
  GATE,    // a gate patched: rising edge and high while held (drums, the 6-op FM's envelopes, spoken words)
};

struct Info {
  const char* name;                 // on the screen, 10 characters at most
  Mode mode;
  bool own_envelope;                // shaped by an envelope of its own (Plaits' "already enveloped"): GRAV CODE's
                                    // envelope leaves it (Modal and String keep it: they sound on while held)
  float gain;                       // its level among the others: measured in the host simulator at the defaults
                                    // (TIMBRE 65, MORPH 50, HARMONICS 75, C4 at velocity 100), the RMS while held
                                    // brought to about -34 dB, the first three's (kept at 1); a drum by its peak
  const uint8_t* user_data;         // the 6-op FM's patch bank
};

// By engine_id. Constant data only: the FM-1 runs no C++ static constructors, so nothing here may need one.
const Info info[] = {
  /* WAVESHAPING      */ { "WAVESHAPE",  FREE,  false, 1.0f,  nullptr },
  /* FM               */ { "FM 2-OP",    FREE,  false, 1.0f,  nullptr },
  /* VIRTUAL_ANALOG   */ { "ANALOG",     FREE,  false, 1.0f,  nullptr },
  /* MODAL            */ { "MODAL",      BOWED, false, 0.16f, nullptr },
  /* GRAIN            */ { "GRAIN",      FREE,  false, 1.75f, nullptr },
  /* ADDITIVE         */ { "ADDITIVE",   FREE,  false, 1.55f, nullptr },
  /* WAVETABLE        */ { "WAVETABLE",  FREE,  false, 1.7f,  nullptr },
  /* CHORDS           */ { "CHORDS",     FREE,  false, 1.0f,  nullptr },
  /* SPEECH           */ { "SPEECH",     GATE,  false, 2.0f,  nullptr },
  /* SWARM            */ { "SWARM",      FREE,  false, 4.5f,  nullptr },
  /* NOISE            */ { "NOISE",      FREE,  false, 0.72f, nullptr },
  /* PARTICLE         */ { "PARTICLE",   GATE,  false, 3.0f,  nullptr },
  /* STRING           */ { "STRING",     BOWED, false, 0.62f, nullptr },
  /* BASS_DRUM        */ { "BASS DRUM",  GATE,  true,  2.5f,  nullptr },
  /* SNARE_DRUM       */ { "SNARE",      GATE,  true,  1.5f,  nullptr },
  /* HI_HAT           */ { "HI-HAT",     GATE,  true,  2.2f,  nullptr },
  /* VA_VCF           */ { "VA VCF",     FREE,  false, 0.85f, nullptr },
  /* PHASE_DISTORTION */ { "PHASE DIST", FREE,  false, 0.73f, nullptr },
  /* SIX_OP_1         */ { "6-OP FM 1",  GATE,  true,  1.8f,  plaits::syx_bank_0 },
  /* SIX_OP_2         */ { "6-OP FM 2",  GATE,  true,  1.8f,  plaits::syx_bank_1 },
  /* SIX_OP_3         */ { "6-OP FM 3",  GATE,  true,  1.8f,  plaits::syx_bank_2 },
  /* WAVE_TERRAIN     */ { "TERRAIN",    FREE,  false, 1.65f, nullptr },
  /* STRING_MACHINE   */ { "STRING MCH", FREE,  false, 3.3f,  nullptr },
  /* CHIPTUNE         */ { "CHIPTUNE",   FREE,  false, 1.0f,  nullptr },
};
static_assert(sizeof(info) / sizeof(info[0]) == GRAV_ENGINE_COUNT, "an Info for each engine");

// PRESET's order: Plaits' own, its first sixteen engines, then the eight of its 1.2 firmware.
const uint8_t order[] = {
  GRAV_ENGINE_VIRTUAL_ANALOG, GRAV_ENGINE_WAVESHAPING, GRAV_ENGINE_FM, GRAV_ENGINE_GRAIN, GRAV_ENGINE_ADDITIVE,
  GRAV_ENGINE_WAVETABLE, GRAV_ENGINE_CHORDS, GRAV_ENGINE_SPEECH, GRAV_ENGINE_SWARM, GRAV_ENGINE_NOISE,
  GRAV_ENGINE_PARTICLE, GRAV_ENGINE_STRING, GRAV_ENGINE_MODAL, GRAV_ENGINE_BASS_DRUM, GRAV_ENGINE_SNARE_DRUM,
  GRAV_ENGINE_HI_HAT, GRAV_ENGINE_VA_VCF, GRAV_ENGINE_PHASE_DISTORTION, GRAV_ENGINE_SIX_OP_1, GRAV_ENGINE_SIX_OP_2,
  GRAV_ENGINE_SIX_OP_3, GRAV_ENGINE_WAVE_TERRAIN, GRAV_ENGINE_STRING_MACHINE, GRAV_ENGINE_CHIPTUNE,
};
static_assert(sizeof(order) == GRAV_ENGINE_COUNT, "each engine once in PRESET's order");

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
Slot<plaits::GrainEngine> grain_slot;
Slot<plaits::AdditiveEngine> additive_slot;
Slot<plaits::WavetableEngine> wavetable_slot;
Slot<plaits::ChordEngine> chord_slot;
Slot<plaits::SpeechEngine> speech_slot;
Slot<plaits::SwarmEngine> swarm_slot;
Slot<plaits::NoiseEngine> noise_slot;
Slot<plaits::ParticleEngine> particle_slot;
Slot<plaits::StringEngine> string_slot;
Slot<plaits::BassDrumEngine> bass_drum_slot;
Slot<plaits::SnareDrumEngine> snare_drum_slot;
Slot<plaits::HiHatEngine> hi_hat_slot;
Slot<plaits::VirtualAnalogVCFEngine> va_vcf_slot;
Slot<plaits::PhaseDistortionEngine> phase_distortion_slot;
Slot<plaits::SixOpEngine> six_op_slot;      // one engine for the three banks, as on Plaits
Slot<plaits::WaveTerrainEngine> wave_terrain_slot;
Slot<plaits::StringMachineEngine> string_machine_slot;
Slot<plaits::ChiptuneEngine> chiptune_slot;

plaits::Engine* engines[GRAV_ENGINE_COUNT];

// The engines' RAM: each its own part (Plaits shares one buffer and makes the engine chosen start over; here
// switching leaves nothing behind for the next to find). Init takes 58 KB on the host (59348 B: Particle 16 KB,
// String 15, Speech 14, the 6-op FM 5, the string machine 4; less on the FM-1, its pointers half as wide);
// tests/engines.txt checks the room left.
char engine_ram[72 * 1024] __attribute__((aligned(8))) GRAV_POOL;
uint32_t ram_used;

uint32_t current = GRAV_ENGINE_COUNT;
bool initialized;

// The engines run as on Plaits: kBlockSize (12) samples at a time, always. Some count on it: the 6-op FM renders
// its two voices staggered over blocks of one size, in a scratch buffer six blocks long (24 overran it: the voice
// after a bank change was silent). The FM-1's blocks (256) are no multiple of 12, so the engine's blocks go through
// this FIFO, each sample at most 11 late (0.25 ms).
float fifo_out[plaits::kBlockSize];
float fifo_aux[plaits::kBlockSize];
uint32_t fifo_read = plaits::kBlockSize;   // all read: the next sample wants a new block
bool fifo_enveloped;

// An engine chosen: its user data (the 6-op FM's bank) and a fresh start, as Plaits' Voice does.
void choose(uint32_t engine) {
  engines[engine]->LoadUserData(info[engine].user_data);
  engines[engine]->Reset();
  current = engine;
}

}  // namespace

extern "C" void grav_plaits_init(void) {
  plaits::Engine** e = engines;
  e[GRAV_ENGINE_WAVESHAPING] = new (waveshaping_slot.bytes) plaits::WaveshapingEngine();
  e[GRAV_ENGINE_FM] = new (fm_slot.bytes) plaits::FMEngine();
  e[GRAV_ENGINE_VIRTUAL_ANALOG] = new (virtual_analog_slot.bytes) plaits::VirtualAnalogEngine();
  e[GRAV_ENGINE_MODAL] = new (modal_slot.bytes) plaits::ModalEngine();
  e[GRAV_ENGINE_GRAIN] = new (grain_slot.bytes) plaits::GrainEngine();
  e[GRAV_ENGINE_ADDITIVE] = new (additive_slot.bytes) plaits::AdditiveEngine();
  e[GRAV_ENGINE_WAVETABLE] = new (wavetable_slot.bytes) plaits::WavetableEngine();
  e[GRAV_ENGINE_CHORDS] = new (chord_slot.bytes) plaits::ChordEngine();
  e[GRAV_ENGINE_SPEECH] = new (speech_slot.bytes) plaits::SpeechEngine();
  e[GRAV_ENGINE_SWARM] = new (swarm_slot.bytes) plaits::SwarmEngine();
  e[GRAV_ENGINE_NOISE] = new (noise_slot.bytes) plaits::NoiseEngine();
  e[GRAV_ENGINE_PARTICLE] = new (particle_slot.bytes) plaits::ParticleEngine();
  e[GRAV_ENGINE_STRING] = new (string_slot.bytes) plaits::StringEngine();
  e[GRAV_ENGINE_BASS_DRUM] = new (bass_drum_slot.bytes) plaits::BassDrumEngine();
  e[GRAV_ENGINE_SNARE_DRUM] = new (snare_drum_slot.bytes) plaits::SnareDrumEngine();
  e[GRAV_ENGINE_HI_HAT] = new (hi_hat_slot.bytes) plaits::HiHatEngine();
  e[GRAV_ENGINE_VA_VCF] = new (va_vcf_slot.bytes) plaits::VirtualAnalogVCFEngine();
  e[GRAV_ENGINE_PHASE_DISTORTION] = new (phase_distortion_slot.bytes) plaits::PhaseDistortionEngine();
  e[GRAV_ENGINE_SIX_OP_1] = e[GRAV_ENGINE_SIX_OP_2] = e[GRAV_ENGINE_SIX_OP_3] =
      new (six_op_slot.bytes) plaits::SixOpEngine();
  e[GRAV_ENGINE_WAVE_TERRAIN] = new (wave_terrain_slot.bytes) plaits::WaveTerrainEngine();
  e[GRAV_ENGINE_STRING_MACHINE] = new (string_machine_slot.bytes) plaits::StringMachineEngine();
  e[GRAV_ENGINE_CHIPTUNE] = new (chiptune_slot.bytes) plaits::ChiptuneEngine();

  stmlib::BufferAllocator allocator(engine_ram, sizeof(engine_ram));
  for (uint32_t i = 0; i < GRAV_ENGINE_COUNT; ++i) {
    if (i != GRAV_ENGINE_SIX_OP_2 && i != GRAV_ENGINE_SIX_OP_3) {   // (the same engine as SIX_OP_1)
      e[i]->Init(&allocator);
    }
  }
  ram_used = sizeof(engine_ram) - allocator.free();
  choose(GRAV_ENGINE_WAVESHAPING);
  initialized = true;
}

extern "C" int grav_plaits_render(const grav_plaits_params_t* in, float* out, float* aux, uint32_t size) {
  if (!initialized) {
    grav_plaits_init();
  }
  uint32_t engine = in->engine < GRAV_ENGINE_COUNT ? in->engine : GRAV_ENGINE_WAVESHAPING;
  if (engine != current) {
    choose(engine);
  }
  const Info& e = info[current];

  int first, held;                   // the trigger in the first block, and in the blocks after it
  switch (e.mode) {
    case BOWED:
      held = in->gate ? plaits::TRIGGER_UNPATCHED : plaits::TRIGGER_LOW;
      first = in->strike ? plaits::TRIGGER_RISING_EDGE : held;
      break;
    case GATE:
      held = in->gate ? plaits::TRIGGER_HIGH : plaits::TRIGGER_LOW;
      first = held | (in->strike ? plaits::TRIGGER_RISING_EDGE : 0);
      break;
    default:
      held = first = plaits::TRIGGER_UNPATCHED;
      break;
  }

  plaits::EngineParameters p = {};
  p.trigger = first;                 // (a call of kBlockSize samples or more renders a block: no strike is lost)
  p.note = in->note;
  p.timbre = in->timbre;
  p.morph = in->morph;
  p.harmonics = in->harmonics;
  p.accent = in->accent;

  for (uint32_t i = 0; i < size; ++i) {
    if (fifo_read == plaits::kBlockSize) {
      bool already_enveloped = e.own_envelope;   // an engine may say otherwise (Speech: a word, or a vowel)
      engines[current]->Render(p, fifo_out, fifo_aux, plaits::kBlockSize, &already_enveloped);
      if (e.gain != 1.0f) {
        for (size_t j = 0; j < plaits::kBlockSize; ++j) {
          fifo_out[j] *= e.gain;
          fifo_aux[j] *= e.gain;
        }
      }
      fifo_enveloped = already_enveloped;
      p.trigger = held;
      fifo_read = 0;
    }
    out[i] = fifo_out[fifo_read];
    aux[i] = fifo_aux[fifo_read];
    ++fifo_read;
  }
  return fifo_enveloped ? 1 : 0;
}

extern "C" const char* grav_engine_name(uint32_t engine) {
  return info[engine < GRAV_ENGINE_COUNT ? engine : 0].name;
}

extern "C" uint32_t grav_engine_step(uint32_t engine, int32_t steps) {
  int32_t at = 0;
  while (at < GRAV_ENGINE_COUNT && order[at] != engine) {
    ++at;
  }
  at = (at + steps % GRAV_ENGINE_COUNT + GRAV_ENGINE_COUNT) % GRAV_ENGINE_COUNT;
  return order[at];
}

extern "C" uint32_t grav_plaits_ram_used(void) {
  return ram_used;
}
