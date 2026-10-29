// Copyright 2014 Emilie Gillet.
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
// Beads' reverb (beads/dsp/fx/reverb.h), on its own as GRAV CODE's HALL (0.32):
// wet only (the dry goes round it), and its diffusion and its tail's modulation
// set from outside (Beads fixes them). The topology, its delays and its LFOs are
// Beads' own.

#ifndef GRAV_DSP_REVERB_H_
#define GRAV_DSP_REVERB_H_

#include "stmlib/stmlib.h"
#include "stmlib/dsp/parameter_interpolator.h"

#include "beads/dsp/fx/fx_engine.h"

namespace grav {

class Reverb {
 public:
  Reverb() { }
  ~Reverb() { }

  void Init(uint16_t* buffer, float sample_rate) {
    engine_.Init(buffer);
    engine_.SetLFOFrequency(beads::LFO_1, 0.5f / sample_rate);
    engine_.SetLFOFrequency(beads::LFO_2, 0.3f / sample_rate);
    time_ = 0.5f;
    lp_ = 0.6f;
    diffusion_ = 0.625f;
    mod_ = 1.0f;
    Clear();
  }

  void Clear() {
    engine_.Clear();
    lp_1_ = lp_2_ = 0.0f;
    send_ = 0.0f;
  }

  // in: the mono send; its wet added to l and r. send: how much of in goes in
  // (Beads' REVERB's share, its curve outside), interpolated over the block
  void Process(const float* in, float* l, float* r, size_t size, float send) {
    // The Griesinger topology described in the Dattorro paper (4 AP diffusers
    // on the input, then a loop of 2x 2AP+1Delay). Modulation in the loop of
    // the first diffuser AP smears it; on the two long delays, a slow shimmer.
    typedef E::Reserve<150,
      E::Reserve<214,
      E::Reserve<319,
      E::Reserve<527,
      E::Reserve<2182,
      E::Reserve<2690,
      E::Reserve<4501,
      E::Reserve<2525,
      E::Reserve<2197,
      E::Reserve<6312> > > > > > > > > > Memory;
    E::DelayLine<Memory, 0> ap1;
    E::DelayLine<Memory, 1> ap2;
    E::DelayLine<Memory, 2> ap3;
    E::DelayLine<Memory, 3> ap4;
    E::DelayLine<Memory, 4> dap1a;
    E::DelayLine<Memory, 5> dap1b;
    E::DelayLine<Memory, 6> del1;
    E::DelayLine<Memory, 7> dap2a;
    E::DelayLine<Memory, 8> dap2b;
    E::DelayLine<Memory, 9> del2;
    E::Context c;

    const float kap = diffusion_;
    const float klp = lp_;
    const float krt = time_;
    // the modulation's depths, Beads' at mod_ 1; the long delays read back from
    // their ends so that the deepest still stays within each (an LFO: 0..1)
    const float m1 = 80.0f * mod_, m2 = 50.0f * mod_, m3 = 40.0f * mod_;
    const float input_gain = 0.2f * (1.0f - 0.333f * krt);
    const float output_gain = 0.4f / input_gain;
    stmlib::ParameterInterpolator send_modulation(&send_, send * input_gain, size);

    for (size_t i = 0; i < size; ++i) {
      const float send_amount = send_modulation.Next();
      float wet;
      float apout = 0.0f;
      engine_.Start(&c);

      c.Interpolate(ap1, 10.0f, beads::LFO_1, m1, 1.0f);
      c.Write(ap1, 100, 0.0f);

      c.Read(in[i] * 2.0f, send_amount);   // (Beads: left + right, a mono pair)

      c.Read(ap1 TAIL, kap);
      c.WriteAllPass(ap1, -kap);
      c.Read(ap2 TAIL, kap);
      c.WriteAllPass(ap2, -kap);
      c.Read(ap3 TAIL, kap);
      c.WriteAllPass(ap3, -kap);
      c.Read(ap4 TAIL, kap);
      c.WriteAllPass(ap4, -kap);
      c.Write(apout);

      c.Load(apout);
      c.Interpolate(del2, 6311.0f - m2, beads::LFO_2, m2, krt);
      c.Lp(lp_1_, klp);
      c.Read(dap1a TAIL, -kap);
      c.WriteAllPass(dap1a, kap);
      c.Read(dap1b TAIL, kap);
      c.WriteAllPass(dap1b, -kap);
      c.Write(del1, 1.0f);
      c.Write(wet);
      l[i] += wet * output_gain;

      c.Load(apout);
      c.Interpolate(del1, 4500.0f - m3, beads::LFO_1, m3, krt);
      c.Lp(lp_2_, klp);
      c.Read(dap2a TAIL, kap);
      c.WriteAllPass(dap2a, -kap);
      c.Read(dap2b TAIL, -kap);
      c.WriteAllPass(dap2b, kap);
      c.Write(del2, 1.0f);
      c.Write(wet);
      r[i] += wet * output_gain;
    }
  }

  inline void set_time(float reverb_time) { time_ = reverb_time; }   // the loop's gain, under 1
  inline void set_lp(float lp) { lp_ = lp; }                          // the loop's low pass: 1 open
  inline void set_diffusion(float kap) { diffusion_ = kap; }         // the all-passes' gain (Beads: 0.625)
  inline void set_mod(float mod) { mod_ = mod; }                      // 0 still, 1 Beads', 1.6 at most

 private:
  typedef beads::FxEngine<32768, beads::FORMAT_16_BIT> E;
  E engine_;

  float send_;
  float time_;
  float lp_;
  float diffusion_;
  float mod_;
  float lp_1_;
  float lp_2_;

  DISALLOW_COPY_AND_ASSIGN(Reverb);
};

}  // namespace grav

#endif  // GRAV_DSP_REVERB_H_
