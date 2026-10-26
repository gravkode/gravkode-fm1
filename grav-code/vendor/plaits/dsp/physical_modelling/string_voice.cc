// Copyright 2016 Emilie Gillet.
//
// Author: Emilie Gillet (emilie.o.gillet@gmail.com)
//
// Permission is hereby granted, free of charge, to any person obtaining a copy
// of this software and associated documentation files (the "Software"), to deal
// in the Software without restriction, including without limitation the rights
// to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
// copies of the Software, and to permit persons to whom the Software is
// furnished to do so, subject to the following conditions:
// 
// The above copyright notice and this permission notice shall be included in
// all copies or substantial portions of the Software.
// 
// THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
// IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
// FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
// AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
// LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
// OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
// THE SOFTWARE.
// 
// See http://creativecommons.org/licenses/MIT/ for more information.
//
// -----------------------------------------------------------------------------
//
// Extended Karplus-Strong, with all the niceties from Rings.

#include "plaits/dsp/physical_modelling/string_voice.h"
#include "plaits/dsp/engine/engine.h"

#include <algorithm>

#include "stmlib/dsp/units.h"
#include "stmlib/utils/random.h"

#include "plaits/dsp/noise/dust.h"

// GRAV CODE: parameters passed down for the sound editor's independent controls (engine/engine.h GravExtra);
// AUTO keeps Plaits' own values.

namespace plaits {

using namespace std;
using namespace stmlib;

void StringVoice::Init(BufferAllocator* allocator) {
  excitation_filter_.Init();
  string_.Init(allocator);
  remaining_noise_samples_ = 0;
}

void StringVoice::Reset() {
  string_.Reset();
}

void StringVoice::Render(
    bool sustain,
    bool trigger,
    float accent,
    float f0,
    float structure,
    float brightness,
    float damping,
    float* temp,
    float* out,
    float* aux,
    size_t size, const uint8_t* extra) {
  EngineParameters controls = {};
  controls.extra = extra;
  const float density = GravExtra(controls, 0, brightness * brightness);

  brightness += GravExtra(controls, 4, 0.25f) * accent * (1.0f - brightness);
  damping += GravExtra(controls, 4, 0.25f) * accent * (1.0f - damping);
  
  // Synthesize excitation signal.
  if (trigger || sustain) {
    const float range = 72.0f;
    const float f = 4.0f * f0;
    const float macro_cutoff = min(
        f * SemitonesToRatio((brightness * (2.0f - brightness) - 0.5f) * range),
        0.499f);
    const float cutoff = extra && extra[1] != 255 ? min(f0 * SemitonesToRatio(GravExtra(controls, 1, 0.5f) * 120.0f - 24.0f), 0.499f) : macro_cutoff;
    const float q = extra && extra[2] != 255 ? 0.1f + 7.9f * GravExtra(controls, 2, 0.0f) : (sustain ? 1.0f : 0.5f);
    remaining_noise_samples_ = static_cast<size_t>((extra && extra[3] != 255 ? 0.1f + 3.9f * GravExtra(controls, 3, 0.0f) : 1.0f) / f0);
    excitation_filter_.set_f_q<FREQUENCY_DIRTY>(cutoff, q);
  }

  if (sustain) {
    const float dust_f = 0.00005f + 0.99995f * density * density;
    for (size_t i = 0; i < size; ++i) {
      temp[i] = Dust(dust_f) * (8.0f - dust_f * 6.0f) * accent;
    }
  } else if (remaining_noise_samples_) {
    size_t noise_samples = min(remaining_noise_samples_, size);
    remaining_noise_samples_ -= noise_samples;
    size_t tail = size - noise_samples;
    float* start = temp;
    while (noise_samples--) {
      *start++ = 2.0f * Random::GetFloat() - 1.0f;
    }
    while (tail--) {
      *start++ = 0.0f;
    }
  } else {
    fill(&temp[0], &temp[size], 0.0f);
  }
  
  excitation_filter_.Process<FILTER_MODE_LOW_PASS>(temp, temp, size);
  for (size_t i = 0; i < size; ++i) {
    aux[i] += temp[i];
  }
  
  float non_linearity = structure < 0.24f
      ? (structure - 0.24f) * 4.166f
      : (structure > 0.26f ? (structure - 0.26f) * 1.35135f : 0.0f);
  if (extra && extra[5] != 255) non_linearity = GravExtra(controls, 5, 0.5f) * 2.0f - 1.0f;
  string_.Process(f0, non_linearity, brightness, damping, temp, out, size);
}

}  // namespace plaits
