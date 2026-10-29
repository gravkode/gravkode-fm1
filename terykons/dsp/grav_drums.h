/* Terykons' drums (0.19): Plaits' drum models, each with knobs of its own. Plaits' three drum engines each render
 * two models, the second to its AUX out (which Terykons leaves unused), and fold their parameters onto its three
 * knobs (the bass drum's HARMONICS is its attack, its pitch sweep and its drive in turn). Here a drum renders the
 * one model chosen, with the DRUM page's TONE and DECAY (timbre, morph) and three knobs more, the SHAPE page's:
 *   kick:  PUNCH (the attack's pitch FM), SWEEP (the body's self FM; the digital kick's sweep time), DRIVE
 *   snare: SNAPPY (the snares' noise), DRIVE, LEVEL
 *   hat:   NOISE (metal against noise), DRIVE, LEVEL
 * The first model is the one Plaits' engine sends to its main out: built the same, it sounds as it did.
 * C++ only (dsp/grav_plaits.cpp builds them in the drum voices' slots). */
#pragma once
#include <algorithm>

#include "plaits/dsp/dsp.h"
#include "plaits/dsp/drums/analog_bass_drum.h"
#include "plaits/dsp/drums/analog_snare_drum.h"
#include "plaits/dsp/drums/hi_hat.h"
#include "plaits/dsp/drums/synthetic_bass_drum.h"
#include "plaits/dsp/drums/synthetic_snare_drum.h"
#include "plaits/dsp/engine/engine.h"
#include "plaits/dsp/fx/overdrive.h"
#include "stmlib/utils/buffer_allocator.h"

namespace grav {

class Drum : public plaits::Engine {
 public:
  explicit Drum(bool second) : second_(second) { shape[0] = shape[1] = shape[2] = 0.0f; }
  virtual void Reset() { }
  virtual void LoadUserData(const uint8_t* user_data) { (void)user_data; }
  float shape[3];                        // the SHAPE page's knobs, 0..1 (set before each block)

 protected:
  static bool Sustain(const plaits::EngineParameters& p) { return p.trigger & plaits::TRIGGER_UNPATCHED; }
  static bool Trigger(const plaits::EngineParameters& p) { return p.trigger & plaits::TRIGGER_RISING_EDGE; }
  // DRIVE and LEVEL, after the snare and the hi-hat (the kick drives its own way)
  void DriveLevel(float drive, float level, float* out, size_t size) {
    if (drive > 0.0f) {
      overdrive_.Process(0.5f + 0.5f * drive, out, size);
    }
    for (size_t i = 0; i < size; ++i) {
      out[i] *= level;
    }
  }
  static void Silence(float* aux, size_t size) { std::fill(aux, aux + size, 0.0f); }

  bool second_;                          // the second model (Plaits' AUX)
  plaits::Overdrive overdrive_;
};

class Kick : public Drum {
 public:
  explicit Kick(bool second) : Drum(second) { }
  virtual void Init(stmlib::BufferAllocator* allocator) {
    (void)allocator;
    analog_.Init();
    synthetic_.Init();
    overdrive_.Init();
  }
  virtual void Render(const plaits::EngineParameters& p, float* out, float* aux, size_t size,
                      bool* already_enveloped) {
    const float f0 = plaits::NoteToFrequency(p.note);
    const float punch = shape[0], sweep = shape[1], drive = shape[2];
    if (!second_) {                      // as Plaits' bass drum, its HARMONICS undone into three
      analog_.Render(Sustain(p), Trigger(p), p.accent, f0, p.timbre, p.morph, punch, sweep, out, size);
      overdrive_.Process(0.5f + 0.5f * drive * std::max(1.0f - 16.0f * f0, 0.0f), out, size);
    } else {                             // the digital one: DRIVE its dirt, PUNCH and SWEEP its pitch envelope
      synthetic_.Render(Sustain(p), Trigger(p), p.accent, f0, p.timbre, p.morph, drive, punch, sweep, out, size);
    }
    Silence(aux, size);
    *already_enveloped = true;
  }

 private:
  plaits::AnalogBassDrum analog_;
  plaits::SyntheticBassDrum synthetic_;
};

class Snare : public Drum {
 public:
  explicit Snare(bool second) : Drum(second) { }
  virtual void Init(stmlib::BufferAllocator* allocator) {
    (void)allocator;
    analog_.Init();
    synthetic_.Init();
    overdrive_.Init();
  }
  virtual void Render(const plaits::EngineParameters& p, float* out, float* aux, size_t size,
                      bool* already_enveloped) {
    const float f0 = plaits::NoteToFrequency(p.note);
    if (!second_) {
      analog_.Render(Sustain(p), Trigger(p), p.accent, f0, p.timbre, p.morph, shape[0], out, size);
    } else {
      synthetic_.Render(Sustain(p), Trigger(p), p.accent, f0, p.timbre, p.morph, shape[0], out, size);
    }
    DriveLevel(shape[1], shape[2], out, size);
    Silence(aux, size);
    *already_enveloped = true;
  }

 private:
  plaits::AnalogSnareDrum analog_;
  plaits::SyntheticSnareDrum synthetic_;
};

class Hat : public Drum {
 public:
  explicit Hat(bool second) : Drum(second), temp_(0) { }
  virtual void Init(stmlib::BufferAllocator* allocator) {
    metal_.Init();
    ring_.Init();
    overdrive_.Init();
    temp_ = allocator->Allocate<float>(plaits::kMaxBlockSize * 2);
  }
  virtual void Render(const plaits::EngineParameters& p, float* out, float* aux, size_t size,
                      bool* already_enveloped) {
    const float f0 = plaits::NoteToFrequency(p.note);
    if (!second_) {
      metal_.Render(Sustain(p), Trigger(p), p.accent, f0, p.timbre, p.morph, shape[0], temp_, temp_ + size, out,
                    size);
    } else {
      ring_.Render(Sustain(p), Trigger(p), p.accent, f0, p.timbre, p.morph, shape[0], temp_, temp_ + size, out,
                   size);
    }
    DriveLevel(shape[1], shape[2], out, size);
    Silence(aux, size);
    *already_enveloped = true;
  }

 private:
  plaits::HiHat<plaits::SquareNoise, plaits::SwingVCA, true, false> metal_;   // Plaits' first: six square waves
  plaits::HiHat<plaits::RingModNoise, plaits::LinearVCA, false, true> ring_;  // its second: ring-modulated
  float* temp_;
};

}  // namespace grav
