/* Mutable Instruments Plaits' engines for GRAV CODE: built, chosen and rendered here for the C side (grav_plaits.h) */
#include <new>
#include <cstring>

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
#include "stmlib/utils/random.h"

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
  // FM operator envelopes shape the patch's timbre; the app's ENV still shapes its final amplitude.
  /* SIX_OP_1         */ { "6-OP FM 1",  GATE,  false, 1.8f,  plaits::syx_bank_0 },
  /* SIX_OP_2         */ { "6-OP FM 2",  GATE,  false, 1.8f,  plaits::syx_bank_1 },
  /* SIX_OP_3         */ { "6-OP FM 3",  GATE,  false, 1.8f,  plaits::syx_bank_2 },
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

constexpr size_t Larger(size_t a, size_t b) { return a > b ? a : b; }

template<typename T>
constexpr size_t LargestSize() { return sizeof(T); }
template<typename T, typename U, typename... Rest>
constexpr size_t LargestSize() { return Larger(sizeof(T), LargestSize<U, Rest...>()); }

template<typename T>
constexpr size_t LargestAlign() { return alignof(T); }
template<typename T, typename U, typename... Rest>
constexpr size_t LargestAlign() { return Larger(alignof(T), LargestAlign<U, Rest...>()); }

#define GRAV_ENGINE_TYPES plaits::WaveshapingEngine, plaits::FMEngine, plaits::VirtualAnalogEngine, \
  plaits::ModalEngine, plaits::GrainEngine, plaits::AdditiveEngine, plaits::WavetableEngine, plaits::ChordEngine, \
  plaits::SpeechEngine, plaits::SwarmEngine, plaits::NoiseEngine, plaits::ParticleEngine, plaits::StringEngine, \
  plaits::BassDrumEngine, plaits::SnareDrumEngine, plaits::HiHatEngine, plaits::VirtualAnalogVCFEngine, \
  plaits::PhaseDistortionEngine, plaits::SixOpEngine, plaits::WaveTerrainEngine, plaits::StringMachineEngine, \
  plaits::ChiptuneEngine
const size_t kSlotSize = LargestSize<GRAV_ENGINE_TYPES>();    // the 6-op FM's, 4.3 KB on the FM-1
const size_t kSlotAlign = LargestAlign<GRAV_ENGINE_TYPES>();

// Each voice's RAM for its engine's Init: Particle takes the most, 16 KB (on the host: String 15, Speech 14, the
// 6-op FM 5, the string machine 4); tests/engines.txt checks every engine's share.
const size_t kVoiceRam = 20 * 1024;

// A voice's engine. The FM-1 runs no C++ static constructors (its link keeps no .init_array), so an engine can't
// be a static object (its vtable pointer would stay zero, and the first virtual call jump to nowhere): it is built
// in the voice's slot (placement new) whenever the voice takes up an engine.
//
// The engines run as on Plaits: kBlockSize (12) samples at a time, always. Some count on it: the 6-op FM renders
// its two voices staggered over blocks of one size, in a scratch buffer six blocks long (24 overran it: the voice
// after a bank change was silent). The FM-1's blocks (256) are no multiple of 12, so the engine's blocks go
// through a FIFO, each sample at most 11 late (0.25 ms).
struct Voice {
  alignas(kSlotAlign) unsigned char slot[kSlotSize];
  plaits::Engine* engine;
  float fifo_out[plaits::kBlockSize];
  float fifo_aux[plaits::kBlockSize];
  uint32_t fifo_read;                // kBlockSize: all read, the next sample wants a new block
  bool fifo_enveloped;
  uint32_t random_state;             // voice-local stream: independent of scheduling and of Beads
};

// In the FM-1's pool (zeroed at boot; all plain data, nothing to construct): 4 x 24 KB.
Voice voices[GRAV_VOICES] GRAV_POOL;
char voice_ram[GRAV_VOICES][kVoiceRam] __attribute__((aligned(8))) GRAV_POOL;
uint32_t ram_used;

uint32_t current = GRAV_ENGINE_COUNT;
bool initialized;

// An engine built in a slot (the 6-op FM's three banks are one engine with three sets of user data, as on Plaits).
plaits::Engine* Build(uint32_t engine, void* slot) {
  switch (engine) {
    case GRAV_ENGINE_FM: return new (slot) plaits::FMEngine();
    case GRAV_ENGINE_VIRTUAL_ANALOG: return new (slot) plaits::VirtualAnalogEngine();
    case GRAV_ENGINE_MODAL: return new (slot) plaits::ModalEngine();
    case GRAV_ENGINE_GRAIN: return new (slot) plaits::GrainEngine();
    case GRAV_ENGINE_ADDITIVE: return new (slot) plaits::AdditiveEngine();
    case GRAV_ENGINE_WAVETABLE: return new (slot) plaits::WavetableEngine();
    case GRAV_ENGINE_CHORDS: return new (slot) plaits::ChordEngine();
    case GRAV_ENGINE_SPEECH: return new (slot) plaits::SpeechEngine();
    case GRAV_ENGINE_SWARM: return new (slot) plaits::SwarmEngine();
    case GRAV_ENGINE_NOISE: return new (slot) plaits::NoiseEngine();
    case GRAV_ENGINE_PARTICLE: return new (slot) plaits::ParticleEngine();
    case GRAV_ENGINE_STRING: return new (slot) plaits::StringEngine();
    case GRAV_ENGINE_BASS_DRUM: return new (slot) plaits::BassDrumEngine();
    case GRAV_ENGINE_SNARE_DRUM: return new (slot) plaits::SnareDrumEngine();
    case GRAV_ENGINE_HI_HAT: return new (slot) plaits::HiHatEngine();
    case GRAV_ENGINE_VA_VCF: return new (slot) plaits::VirtualAnalogVCFEngine();
    case GRAV_ENGINE_PHASE_DISTORTION: return new (slot) plaits::PhaseDistortionEngine();
    case GRAV_ENGINE_SIX_OP_1:
    case GRAV_ENGINE_SIX_OP_2:
    case GRAV_ENGINE_SIX_OP_3: return new (slot) plaits::SixOpEngine();
    case GRAV_ENGINE_WAVE_TERRAIN: return new (slot) plaits::WaveTerrainEngine();
    case GRAV_ENGINE_STRING_MACHINE: return new (slot) plaits::StringMachineEngine();
    case GRAV_ENGINE_CHIPTUNE: return new (slot) plaits::ChiptuneEngine();
    default: return new (slot) plaits::WaveshapingEngine();
  }
}

// An engine chosen: built in every voice, from Init, with its user data (the 6-op FM's bank) and a fresh start,
// as Plaits' Voice starts an engine it switches to. (The engines' destructors do nothing: a slot is just reused.)
void choose(uint32_t engine) {
  for (uint32_t i = 0; i < GRAV_VOICES; ++i) {
    Voice& v = voices[i];
    const uint32_t previous_random = stmlib::Random::state();
    stmlib::Random::Seed(0x21u + i * 0x9e3779b9u);
    // Init does not clear every engine's scratch memory (notably the staggered 6-op FM buffers).
    // A newly selected engine must not consume samples/state left by a previous engine.
    std::memset(v.slot, 0, sizeof v.slot);
    std::memset(voice_ram[i], 0, kVoiceRam);
    v.engine = Build(engine, v.slot);
    stmlib::BufferAllocator allocator(voice_ram[i], kVoiceRam);
    v.engine->Init(&allocator);
    ram_used = kVoiceRam - allocator.free();
    v.engine->LoadUserData(info[engine].user_data);
    v.engine->Reset();
    v.fifo_read = plaits::kBlockSize;
    v.fifo_enveloped = info[engine].own_envelope;
    v.random_state = stmlib::Random::state();
    stmlib::Random::Seed(previous_random);
  }
  current = engine;
}

}  // namespace

extern "C" void grav_plaits_init(void) {
  choose(GRAV_ENGINE_WAVESHAPING);
  initialized = true;
}

extern "C" void grav_plaits_engine(uint32_t engine) {
  if (!initialized) {
    grav_plaits_init();
  }
  if (engine >= GRAV_ENGINE_COUNT) {
    engine = GRAV_ENGINE_WAVESHAPING;
  }
  if (engine != current) {
    choose(engine);
  }
}

extern "C" int grav_plaits_render(uint32_t voice, const grav_plaits_params_t* in, float* out, float* aux,
                                  uint32_t size) {
  if (!initialized) {
    grav_plaits_init();
  }
  Voice& v = voices[voice < GRAV_VOICES ? voice : 0];
  const uint32_t previous_random = stmlib::Random::state();
  stmlib::Random::Seed(v.random_state);
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
    if (v.fifo_read == plaits::kBlockSize) {
      bool already_enveloped = e.own_envelope;   // an engine may say otherwise (Speech: a word, or a vowel)
      v.engine->Render(p, v.fifo_out, v.fifo_aux, plaits::kBlockSize, &already_enveloped);
      if (e.gain != 1.0f) {
        for (size_t j = 0; j < plaits::kBlockSize; ++j) {
          v.fifo_out[j] *= e.gain;
          v.fifo_aux[j] *= e.gain;
        }
      }
      v.fifo_enveloped = already_enveloped;
      p.trigger = held;
      v.fifo_read = 0;
    }
    out[i] = v.fifo_out[v.fifo_read];
    aux[i] = v.fifo_aux[v.fifo_read];
    ++v.fifo_read;
  }
  v.random_state = stmlib::Random::state();
  stmlib::Random::Seed(previous_random);
  return v.fifo_enveloped ? 1 : 0;
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
