/* Mutable Instruments Beads for GRAV CODE: its processor built and run here for the C side (grav_beads.h) */
#include <new>

#include "grav_beads.h"

#include "beads/dsp/processor.h"

#ifdef PLAT_HOST
#define GRAV_POOL
#else
#define GRAV_POOL __attribute__((section(".pool")))   /* the FM-1's RAM for big buffers (plat.h PLAT_POOL) */
#endif

namespace {

const size_t kChunk = 12;                 // samples to a call: Beads' own block (48 kHz, 4 kHz block rate)
const float kDryGain = 1.0f / 0.75f;      // Beads leaves the dry at 0.75 (processor.cc post_gain_dry): back to
                                          // unity, so that turning the effect off changes nothing of the level
const float kFadeStep = 1.0f / 1024.0f;   // the fade in and out (on, off, a new QUALITY): 23 ms
const size_t kReverbSize = 32768;         // reverb.h's FxEngine<32768, FORMAT_16_BIT>

// the delay's bank (delay_line.h) and the reverb's line in the pool: 160 KiB
uint8_t delay_buffer[beads::kDelayBankSize * beads::kDelayNumBanks] __attribute__((aligned(8))) GRAV_POOL;
uint16_t reverb_buffer[kReverbSize] GRAV_POOL;

// The processor itself (about 25 KB) is built in place: the FM-1 runs no C++ static constructors
alignas(beads::Processor) char processor_storage[sizeof(beads::Processor)];
beads::Processor* processor;

bool running;                             // processing: off, Beads takes no time
float level;                              // 0: the mix only, 1: Beads' output (fading between)

// the delay, its interpolation tail and the freeze tail in the bank (Beads checks it with an assert, left out)
static_assert(beads::kDelayBufferSizeBits / 8 + 4 * 4 + 256 * sizeof(beads::FloatFrame) <=
              beads::kDelayBankSize * beads::kDelayNumBanks, "the delay fits its bank");

beads::Processor::Media media_of(uint8_t quality) {
  return beads::Processor::Media(beads::Processor::MEDIA_DIGITAL - (quality % GRAV_FX_QUALITIES));
}

beads::RandomizedParameter knob(float base) {
  beads::RandomizedParameter p;
  p.base = base;
  p.attenuverter = 0.0f;                  // no attenuverter, no CV: no random modulation
  p.cv = 0.0f;
  p.has_cv = false;
  return p;
}

}  // namespace

// Beads' PITCH pot law (beads/cv_reader.h PotChannel LAW_DETUNE): -24..+24 semitones, resting a while on each of
// 0, 1, 3, 4, 7, 12 and 24 on the way
extern "C" float grav_beads_pitch(float knob) {
  float detune = 2.0f * knob - 1.0f;
  float sign = detune < 0.0f ? -1.0f : 1.0f;
  detune = detune * sign * 6.0f;
  MAKE_INTEGRAL_FRACTIONAL(detune);
  if (detune_integral > 6) {
    detune_integral = 6;
    detune_fractional = 1.0f;
  }
  const float intervals[8] = { 0.0f, 1.0f, 3.0f, 4.0f, 7.0f, 12.0f, 24.0f, 24.0f };
  detune_fractional = std::max((detune_fractional - 0.33333333f) * 1.5f, 0.0f);
  float a = intervals[detune_integral];
  float b = intervals[detune_integral + 1];
  return stmlib::Crossfade(a, b, stmlib::SmoothStep(detune_fractional)) * sign;
}

extern "C" void grav_beads_init(void) {
  uint8_t* banks[beads::kDelayNumBanks];
  for (size_t i = 0; i < beads::kDelayNumBanks; ++i) {
    banks[i] = &delay_buffer[i * beads::kDelayBankSize];
  }
  processor = new (processor_storage) beads::Processor();
  processor->Init(banks, reverb_buffer);
  running = false;
  level = 0.0f;
}

extern "C" int grav_beads_on(void) {
  return running;
}

extern "C" void grav_beads_process(const grav_beads_params_t* p, const float* in, float* l, float* r,
                                   uint32_t n) {
  const beads::Processor::Media media = media_of(p->quality);
  // frozen, it runs on even at DRY/WET and REVERB 0: what it holds stays
  const bool on = p->dry_wet > 0.0f || p->reverb > 0.0f || p->freeze;

  if (!running) {
    if (!on) {
      for (uint32_t i = 0; i < n; ++i) {
        l[i] = r[i] = in[i];
      }
      return;
    }
    processor->Configure(media, true);    // a fresh start: the delay cleared. Mono: twice the time
    running = true;
    level = 0.0f;
  }

  const bool fade_in = on && media == processor->media();
  beads::Parameters parameters;
  parameters.time = knob(p->time);
  parameters.size = knob(p->size);
  parameters.shape = knob(p->shape);
  parameters.pitch = knob(grav_beads_pitch(p->pitch));
  parameters.monophonic_mode = false;
  beads::ModulatedParameter density;
  density.base = p->density;
  density.cv = 0.0f;
  beads::Processor::MixParameters mix;
  mix.feedback = p->feedback;
  mix.dry_wet = p->dry_wet;
  mix.reverb_amount = p->reverb;
  mix.output_gate = false;
  const beads::SeedButtonState seed = p->seed_auto ? beads::SEED_BUTTON_STATE_LOCKED
      : p->seed ? beads::SEED_BUTTON_STATE_ON : beads::SEED_BUTTON_STATE_OFF;
  bool freeze[kChunk];
  std::fill(&freeze[0], &freeze[kChunk], p->freeze != 0);

  for (uint32_t base = 0; base < n; base += kChunk) {
    const size_t size = std::min(size_t(n - base), kChunk);
    beads::FloatFrame frames_in[kChunk];
    beads::FloatFrame frames_out[kChunk];
    for (size_t i = 0; i < size; ++i) {
      frames_in[i].l = frames_in[i].r = in[base + i];
    }
    processor->Process(seed, NULL, freeze, density, parameters, mix, frames_in, frames_out, size);
    for (size_t i = 0; i < size; ++i) {
      const float dry = in[base + i];
      level += fade_in ? (level < 1.0f ? kFadeStep : 0.0f) : (level > 0.0f ? -kFadeStep : 0.0f);
      CONSTRAIN(level, 0.0f, 1.0f);
      l[base + i] = dry + (frames_out[i].l * kDryGain - dry) * level;
      r[base + i] = dry + (frames_out[i].r * kDryGain - dry) * level;
    }
  }

  if (!fade_in && level <= 0.0f) {        // faded out: off, or back in with the new QUALITY
    if (on) {
      processor->Configure(media, true);
    } else {
      running = false;
    }
  }
}
