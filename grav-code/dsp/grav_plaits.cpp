/* Mutable Instruments Plaits' engines for GRAV CODE: built, chosen and rendered here for the C side (grav_plaits.h) */
#include <new>
#include <cstring>

#include "grav_plaits.h"
#include "grav_drums.h"

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
  const char* knob[3];              // its K1..K3 (Plaits' TIMBRE, MORPH, HARMONICS), by what each does (0.21)
};

// By engine_id. Constant data only: the FM-1 runs no C++ static constructors, so nothing here may need one.
const Info info[] = {
  /* WAVESHAPING      */ { "WAVESHAPE",  FREE,  false, 1.0f,  nullptr, {"FOLD", "ASYMMETRY", "CURVE"} },
  /* FM               */ { "FM 2-OP",    FREE,  false, 1.0f,  nullptr, {"INDEX", "FEEDBACK", "RATIO"} },
  /* VIRTUAL_ANALOG   */ { "ANALOG",     FREE,  false, 1.0f,  nullptr, {"SQUARE", "SAW", "DETUNE"} },
  /* MODAL            */ { "MODAL",      BOWED, false, 0.16f, nullptr, {"EXCITE", "DECAY", "MATERIAL"} },
  /* GRAIN            */ { "GRAIN",      FREE,  false, 1.75f, nullptr, {"FORMANT", "WIDTH", "RATIO"} },
  /* ADDITIVE         */ { "ADDITIVE",   FREE,  false, 1.55f, nullptr, {"PEAK", "SHAPE", "BUMPS"} },
  /* WAVETABLE        */ { "WAVETABLE",  FREE,  false, 1.7f,  nullptr, {"ROW", "COLUMN", "BANK"} },
  /* CHORDS           */ { "CHORDS",     FREE,  false, 1.0f,  nullptr, {"INVERSION", "WAVE", "CHORD"} },
  /* SPEECH           */ { "SPEECH",     GATE,  false, 2.0f,  nullptr, {"SPECIES", "PHONEME", "BANK"} },
  /* SWARM            */ { "SWARM",      FREE,  false, 4.5f,  nullptr, {"DENSITY", "LENGTH", "RANDOM"} },
  /* NOISE            */ { "NOISE",      FREE,  false, 0.72f, nullptr, {"CLOCK", "RESONANCE", "FILTER"} },
  /* PARTICLE         */ { "PARTICLE",   GATE,  false, 3.0f,  nullptr, {"DENSITY", "FILTER", "RANDOM"} },
  /* STRING           */ { "STRING",     BOWED, false, 0.62f, nullptr, {"EXCITE", "DECAY", "MATERIAL"} },
  /* BASS_DRUM        */ { "KICK",       GATE,  true,  2.5f,  nullptr, {"TONE", "DECAY", "PUNCH"} },
  /* SNARE_DRUM       */ { "SNARE",      GATE,  true,  1.5f,  nullptr, {"TONE", "DECAY", "SNAPPY"} },
  /* HI_HAT           */ { "HI-HAT",     GATE,  true,  2.2f,  nullptr, {"TONE", "DECAY", "NOISE"} },
  /* VA_VCF           */ { "VA VCF",     FREE,  false, 0.85f, nullptr, {"CUTOFF", "WAVE", "RESONANCE"} },
  /* PHASE_DISTORTION */ { "PHASE DIST", FREE,  false, 0.73f, nullptr, {"AMOUNT", "ASYMMETRY", "FREQUENCY"} },
  // FM operator envelopes shape the patch's timbre; the app's ENV still shapes its final amplitude.
  /* SIX_OP_1         */ { "6-OP FM 1",  GATE,  false, 1.8f,  plaits::syx_bank_0, {"BRIGHTNESS", "ENVELOPE", "PATCH"} },
  /* SIX_OP_2         */ { "6-OP FM 2",  GATE,  false, 1.8f,  plaits::syx_bank_1, {"BRIGHTNESS", "ENVELOPE", "PATCH"} },
  /* SIX_OP_3         */ { "6-OP FM 3",  GATE,  false, 1.8f,  plaits::syx_bank_2, {"BRIGHTNESS", "ENVELOPE", "PATCH"} },
  /* WAVE_TERRAIN     */ { "TERRAIN",    FREE,  false, 1.65f, nullptr, {"RADIUS", "OFFSET", "TERRAIN"} },
  /* STRING_MACHINE   */ { "STRING MCH", FREE,  false, 3.3f,  nullptr, {"CHORUS", "WAVE", "CHORD"} },
  /* CHIPTUNE         */ { "CHIPTUNE",   FREE,  false, 1.0f,  nullptr, {"ARPEGGIO", "PULSE", "CHORD"} },
  // a drum voice's alone (dsp/grav_drums.h): Plaits' second models, brought to the first ones' peaks (measured at
  // the kit's defaults: C2, G3, C4 at velocity 100)
  /* SYN_KICK         */ { "SYN KICK",   GATE,  true,  1.62f, nullptr, {"TONE", "DECAY", "PUNCH"} },
  /* SYN_SNARE        */ { "SYN SNARE",  GATE,  true,  1.84f, nullptr, {"TONE", "DECAY", "SNAPPY"} },
  /* RING_HAT         */ { "RING HAT",   GATE,  true,  1.21f, nullptr, {"TONE", "DECAY", "NOISE"} },
};
static_assert(sizeof(info) / sizeof(info[0]) == GRAV_ENGINE_ALL, "an Info for each engine");

// ALGO's order: Plaits' own, its first sixteen engines, then the eight of its 1.2 firmware.
const uint8_t order[] = {
  GRAV_ENGINE_VIRTUAL_ANALOG, GRAV_ENGINE_WAVESHAPING, GRAV_ENGINE_FM, GRAV_ENGINE_GRAIN, GRAV_ENGINE_ADDITIVE,
  GRAV_ENGINE_WAVETABLE, GRAV_ENGINE_CHORDS, GRAV_ENGINE_SPEECH, GRAV_ENGINE_SWARM, GRAV_ENGINE_NOISE,
  GRAV_ENGINE_PARTICLE, GRAV_ENGINE_STRING, GRAV_ENGINE_MODAL, GRAV_ENGINE_BASS_DRUM, GRAV_ENGINE_SNARE_DRUM,
  GRAV_ENGINE_HI_HAT, GRAV_ENGINE_VA_VCF, GRAV_ENGINE_PHASE_DISTORTION, GRAV_ENGINE_SIX_OP_1, GRAV_ENGINE_SIX_OP_2,
  GRAV_ENGINE_SIX_OP_3, GRAV_ENGINE_WAVE_TERRAIN, GRAV_ENGINE_STRING_MACHINE, GRAV_ENGINE_CHIPTUNE,
};
static_assert(sizeof(order) == GRAV_ENGINE_COUNT, "each engine once in ALGO's order");

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

// A drum voice's: GRAV CODE's drums (a few hundred bytes each; the hi-hat's Init takes 192 bytes of RAM)
#define GRAV_DRUM_TYPES grav::Kick, grav::Snare, grav::Hat
const size_t kDrumSlotSize = LargestSize<GRAV_DRUM_TYPES>();
const size_t kDrumSlotAlign = LargestAlign<GRAV_DRUM_TYPES>();
const size_t kDrumRam = 512;
const uint32_t kDrumVoices = GRAV_VOICES - GRAV_BIG_VOICES;

// A voice's engine. The FM-1 runs no C++ static constructors (its link keeps no .init_array), so an engine can't
// be a static object (its vtable pointer would stay zero, and the first virtual call jump to nowhere): it is built
// in the voice's slot (placement new) whenever the voice takes up an engine.
//
// The engines run as on Plaits: kBlockSize (12) samples at a time, always. Some count on it: the 6-op FM renders
// its two voices staggered over blocks of one size, in a scratch buffer six blocks long (24 overran it: the voice
// after a bank change was silent). The FM-1's blocks (256) are no multiple of 12, so the engine's blocks go
// through a FIFO, each sample at most 11 late (0.25 ms).
struct Voice {
  unsigned char* slot;               // where its engine is built, and its RAM (a synth voice's big, a drum's small)
  size_t slot_size;
  char* ram;
  size_t ram_size;
  plaits::Engine* engine;
  float fifo_out[plaits::kBlockSize];
  float fifo_aux[plaits::kBlockSize];
  uint32_t fifo_read;                // kBlockSize: all read, the next sample wants a new block
  bool fifo_enveloped;
  uint32_t random_state;             // voice-local stream: independent of scheduling and of Beads
  uint32_t id;                       // the engine built in it (GRAV_ENGINE_*; GRAV_ENGINE_ALL: none yet)
  uint32_t ram_used;                 // bytes of its RAM the engine's Init took
  grav::Drum* drum;                  // a drum voice's drum (its SHAPE knobs set before each block), or none
};

// The voices (plain data: nothing to construct); the synth voices' slots and RAM in the FM-1's pool (4 x 24 KB),
// the drum voices' in the RAM (4 x 1 KB)
Voice voices[GRAV_VOICES];
alignas(kSlotAlign) unsigned char voice_slot[GRAV_BIG_VOICES][kSlotSize] GRAV_POOL;
char voice_ram[GRAV_BIG_VOICES][kVoiceRam] __attribute__((aligned(8))) GRAV_POOL;
alignas(kDrumSlotAlign) unsigned char drum_slot[kDrumVoices][kDrumSlotSize];
char drum_ram[kDrumVoices][kDrumRam] __attribute__((aligned(8)));

static_assert(GRAV_VOICES > GRAV_BIG_VOICES, "drum voices after the synth's");

bool initialized;

// A drum voice's drum, GRAV CODE's (dsp/grav_drums.h): the first model or the second
grav::Drum* BuildDrum(uint32_t engine, void* slot) {
  switch (engine) {
    case GRAV_ENGINE_SYN_KICK: return new (slot) grav::Kick(true);
    case GRAV_ENGINE_SNARE_DRUM: return new (slot) grav::Snare(false);
    case GRAV_ENGINE_SYN_SNARE: return new (slot) grav::Snare(true);
    case GRAV_ENGINE_HI_HAT: return new (slot) grav::Hat(false);
    case GRAV_ENGINE_RING_HAT: return new (slot) grav::Hat(true);
    default: return new (slot) grav::Kick(false);
  }
}

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

// An engine chosen for a voice: built in it, from Init, with its user data (the 6-op FM's bank) and a fresh start,
// as Plaits' Voice starts an engine it switches to. (The engines' destructors do nothing: a slot is just reused.)
// Each voice its own (since 0.16: each track's engine in its voices).
int drum_kind(uint32_t engine) {
  switch (engine) {
    case GRAV_ENGINE_BASS_DRUM: case GRAV_ENGINE_SYN_KICK: return 0;
    case GRAV_ENGINE_SNARE_DRUM: case GRAV_ENGINE_SYN_SNARE: return 1;
    case GRAV_ENGINE_HI_HAT: case GRAV_ENGINE_RING_HAT: return 2;
    default: return -1;
  }
}

bool is_drum(uint32_t engine) { return drum_kind(engine) >= 0; }

void choose(uint32_t i, uint32_t engine) {
  Voice& v = voices[i];
  const bool drum_voice = i >= GRAV_BIG_VOICES;
  if (drum_voice && !is_drum(engine)) {
    engine = GRAV_ENGINE_BASS_DRUM;   // (a drum voice's slot fits a drum alone)
  }
  if (!drum_voice && engine >= GRAV_ENGINE_COUNT) {
    engine = GRAV_ENGINE_WAVESHAPING; // (the second drum models: a drum voice's alone)
  }
  const uint32_t previous_random = stmlib::Random::state();
  stmlib::Random::Seed(0x21u + i * 0x9e3779b9u);
  // Init does not clear every engine's scratch memory (notably the staggered 6-op FM buffers).
  // A newly selected engine must not consume samples/state left by a previous engine.
  std::memset(v.slot, 0, v.slot_size);
  std::memset(v.ram, 0, v.ram_size);
  v.drum = drum_voice ? BuildDrum(engine, v.slot) : nullptr;
  v.engine = drum_voice ? static_cast<plaits::Engine*>(v.drum) : Build(engine, v.slot);
  stmlib::BufferAllocator allocator(v.ram, v.ram_size);
  v.engine->Init(&allocator);
  v.ram_used = v.ram_size - allocator.free();
  v.engine->LoadUserData(info[engine].user_data);
  v.engine->Reset();
  v.fifo_read = plaits::kBlockSize;
  v.fifo_enveloped = info[engine].own_envelope;
  v.random_state = stmlib::Random::state();
  v.id = engine;
  stmlib::Random::Seed(previous_random);
}

}  // namespace

extern "C" void grav_plaits_init(void) {
  for (uint32_t i = 0; i < GRAV_VOICES; ++i) {   // each voice its storage (no static constructors: set here)
    Voice& v = voices[i];
    const bool drum = i >= GRAV_BIG_VOICES;
    v.slot = drum ? drum_slot[i - GRAV_BIG_VOICES] : voice_slot[i];
    v.slot_size = drum ? kDrumSlotSize : kSlotSize;
    v.ram = drum ? drum_ram[i - GRAV_BIG_VOICES] : voice_ram[i];
    v.ram_size = drum ? kDrumRam : kVoiceRam;
    choose(i, drum ? GRAV_ENGINE_BASS_DRUM : GRAV_ENGINE_WAVESHAPING);
  }
  initialized = true;
}

extern "C" void grav_plaits_voice_engine(uint32_t voice, uint32_t engine) {
  if (!initialized) {
    grav_plaits_init();
  }
  if (voice >= GRAV_VOICES) {
    return;
  }
  if (engine >= GRAV_ENGINE_ALL || (voice < GRAV_BIG_VOICES && engine >= GRAV_ENGINE_COUNT)) {
    engine = GRAV_ENGINE_WAVESHAPING; // (as choose builds it: not built anew at every block)
  }
  if (voice >= GRAV_BIG_VOICES && !is_drum(engine)) {
    engine = GRAV_ENGINE_BASS_DRUM;
  }
  if (engine != voices[voice].id) {
    choose(voice, engine);
  }
}

extern "C" void grav_plaits_engine(uint32_t engine) {
  for (uint32_t i = 0; i < GRAV_VOICES; ++i) {
    grav_plaits_voice_engine(i, engine);
  }
}

extern "C" uint32_t grav_plaits_voice_engine_id(uint32_t voice) {
  return voice < GRAV_VOICES ? voices[voice].id : GRAV_ENGINE_ALL;
}

extern "C" void grav_plaits_reset_voice(uint32_t voice) {
  if (!initialized || voice >= GRAV_VOICES) return;
  Voice& v = voices[voice];
  v.engine->Reset();
  v.fifo_read = plaits::kBlockSize;
  v.fifo_enveloped = info[v.id].own_envelope;
}

extern "C" int grav_plaits_render(uint32_t voice, const grav_plaits_params_t* in, float* out, float* aux,
                                  uint32_t size) {
  if (!initialized) {
    grav_plaits_init();
  }
  Voice& v = voices[voice < GRAV_VOICES ? voice : 0];
  const uint32_t previous_random = stmlib::Random::state();
  stmlib::Random::Seed(v.random_state);
  const Info& e = info[v.id < GRAV_ENGINE_ALL ? v.id : 0];
  if (v.drum) {                      // a drum: its SHAPE knobs
    for (uint32_t k = 0; k < 3; ++k) {
      v.drum->shape[k] = in->shape[k];
    }
  }

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

extern "C" int grav_engine_is_drum(uint32_t engine) {
  return is_drum(engine) ? 1 : 0;
}

extern "C" int grav_drum_kind(uint32_t engine) {
  return drum_kind(engine);
}

extern "C" uint32_t grav_drum_step(uint32_t engine, int32_t steps) {
  static const uint8_t drums[6] = {GRAV_ENGINE_BASS_DRUM, GRAV_ENGINE_SYN_KICK, GRAV_ENGINE_SNARE_DRUM,
                                   GRAV_ENGINE_SYN_SNARE, GRAV_ENGINE_HI_HAT, GRAV_ENGINE_RING_HAT};
  int32_t at = 0;
  while (at < 6 && drums[at] != engine) {
    ++at;
  }
  at = ((at % 6 + steps) % 6 + 6) % 6;
  return drums[at];
}

extern "C" void grav_drum_shape(uint32_t engine, float harmonics, float shape[3]) {
  if (drum_kind(engine) == 0) {      // Plaits' bass drum: its attack FM, self FM and drive, each a quarter or a half
    shape[0] = std::min(harmonics * 4.0f, 1.0f);
    shape[1] = std::max(std::min(harmonics * 4.0f - 1.0f, 1.0f), 0.0f);
    shape[2] = std::max(harmonics * 2.0f - 1.0f, 0.0f);
  } else {
    shape[0] = harmonics;
    shape[1] = 0.0f;
    shape[2] = 1.0f;
  }
}

extern "C" const char* grav_engine_name(uint32_t engine) {
  return info[engine < GRAV_ENGINE_ALL ? engine : 0].name;
}

extern "C" const char* grav_engine_knob(uint32_t engine, uint32_t k) {
  return info[engine < GRAV_ENGINE_ALL ? engine : 0].knob[k % 3];
}

extern "C" uint32_t grav_engine_step(uint32_t engine, int32_t steps) {
  int32_t at = 0;
  while (at < GRAV_ENGINE_COUNT && order[at] != engine) {
    ++at;
  }
  at = (at + steps % GRAV_ENGINE_COUNT + GRAV_ENGINE_COUNT) % GRAV_ENGINE_COUNT;
  return order[at];
}

extern "C" uint32_t grav_plaits_ram_used(uint32_t voice) {
  return voices[voice % GRAV_VOICES].ram_used;
}
