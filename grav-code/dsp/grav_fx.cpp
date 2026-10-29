/* GRAV CODE's effect (0.32, grav_fx.h): ECHO, then HALL (Beads' reverb, grav_reverb.h), coloured by TAPE's QUALITY.
 * Until 0.31 the effect was all of Beads (its grains, its delay, its tape and its reverb); its reverb, its tape's
 * wow and flutter and its mu-law stay. The sends come in mono; the dry goes round; the wet is added:
 *
 *   send --> ECHO (its AMOUNT a send into it, FEEDBACK, a low pass and the media in its loop) --> the echoes
 *   send + the echoes --> the media --> HALL (its AMOUNT a send into it, TIME, TONE, DIFFUSE, MOD) --> the tail
 *   l, r = send + the echoes + the tail (through the tape's wow and flutter on CLEAN and DIRTY TAPE)
 *
 * QUALITY, as Beads' media: DIGITAL clean; VINTAGE 12 bits; CLEAN TAPE saturated, a little wow and flutter; DIRTY
 * TAPE 8-bit mu-law, saturated, the full wow and flutter. Off the media darken the echoes' loop and HALL's tail */
#include <algorithm>
#include <cmath>
#include <new>
#include <cstring>

#include "grav_fx.h"
#include "grav_reverb.h"

#include "beads/dsp/fx/wow_and_flutter.h"
#include "beads/dsp/mu_law.h"
#include "stmlib/dsp/dsp.h"
#include "stmlib/utils/random.h"

#ifdef PLAT_HOST
#define GRAV_POOL
#else
#define GRAV_POOL __attribute__((section(".pool")))   /* the FM-1's RAM for big buffers (plat.h PLAT_POOL) */
#endif

namespace {

const float kSampleRate = 44100.0f;
const size_t kChunk = 12;                 // samples to a step of the wow and flutter
const size_t kReverbSize = 32768;         // grav_reverb.h's FxEngine<32768, FORMAT_16_BIT>
const int32_t kEchoSize = 49152;          // ECHO's line: 96 KiB of 16-bit samples, 1.11 s
const float kEchoScale = 16384.0f;        // a sample of 1 in the line (headroom: 2 before it clips)
const float kQuietPeak = 1.0e-5f;         // the tails under this...
const uint32_t kQuietSamples = 22050;     // ...for half a second: off

uint16_t reverb_buffer[kReverbSize] GRAV_POOL;
int16_t echo_buffer[kEchoSize] GRAV_POOL;

// built in place: the FM-1 runs no C++ static constructors
alignas(grav::Reverb) char reverb_storage[sizeof(grav::Reverb)];
alignas(beads::WowAndFlutter) char tape_storage[sizeof(beads::WowAndFlutter)];
grav::Reverb* reverb;
beads::WowAndFlutter* tape;

bool running;                             // processing: off, it takes no time
uint32_t quiet;                           // samples the tails have stayed under kQuietPeak with nothing sent
uint32_t random_state;                    // the wow and flutter's own random stream: the same on either core

int32_t echo_write;                       // the line's next sample
float echo_delay;                         // the echoes' delay in samples, gliding to TIME's (a tape's pitch bend)
float echo_lp, echo_hp;                   // the loop's low pass and its DC blocker
float echo_send, hall_send;               // the sends, smoothed

// ECHO's TIME: note values in quarter notes, and on the screen
const float kEchoBeats[GRAV_ECHO_TIMES] = {
  0.125f, 1.0f / 6.0f, 0.25f, 1.0f / 3.0f, 0.375f, 0.5f, 2.0f / 3.0f, 0.75f, 1.0f, 4.0f / 3.0f, 1.5f, 2.0f };
const char* const kEchoNames[GRAV_ECHO_TIMES] = {
  "1/32", "1/16T", "1/16", "1/8T", "1/16.", "1/8", "1/4T", "1/8.", "1/4", "1/2T", "1/4.", "1/2" };

// a sample through the media (as Beads' dry signal goes: VINTAGE 12 bits, DIRTY TAPE mu-law; the tapes saturate)
inline float media(float x, uint8_t quality) {
  switch (quality) {
    case GRAV_FX_VINTAGE:
      return static_cast<float>(static_cast<int32_t>(x * 2048.0f)) / 2048.0f;
    case GRAV_FX_CLEAN_TAPE:
      return stmlib::SoftLimit(x * 1.2f) / 1.2f;
    case GRAV_FX_DIRTY_TAPE:
      return beads::MuLaw2Lin(beads::Lin2MuLaw(
          static_cast<int16_t>(stmlib::Clip16(static_cast<int32_t>(stmlib::SoftLimit(x * 1.5f) / 1.5f * 32768.0f)))));
    default:
      return x;
  }
}

void start() {
  reverb->Clear();
  std::memset(echo_buffer, 0, sizeof echo_buffer);
  echo_write = 0;
  echo_lp = echo_hp = 0.0f;
  echo_send = hall_send = 0.0f;
  quiet = 0;
  running = true;
}

}  // namespace

extern "C" const char* grav_echo_time_name(uint32_t i) {
  return kEchoNames[i < GRAV_ECHO_TIMES ? i : GRAV_ECHO_TIMES - 1];
}

extern "C" float grav_echo_seconds(uint32_t i, float bpm) {
  return kEchoBeats[i < GRAV_ECHO_TIMES ? i : GRAV_ECHO_TIMES - 1] * 60.0f / (bpm > 1.0f ? bpm : 120.0f);
}

extern "C" void grav_fx_init(void) {
  reverb = new (reverb_storage) grav::Reverb();
  reverb->Init(reverb_buffer, kSampleRate);
  tape = new (tape_storage) beads::WowAndFlutter();
  tape->Init(kSampleRate);
  tape->set_lp(0.6f);                     // (Beads' at 44.1 kHz)
  running = false;
  random_state = 0xBEAD5u;
  echo_delay = 0.0f;
}

extern "C" int grav_fx_on(void) {
  return running;
}

extern "C" int grav_fx_busy(const grav_fx_params_t* p) {
  return running || p->hall > 0.0f || p->echo > 0.0f;
}

extern "C" void grav_fx_process(const grav_fx_params_t* p, const float* in, float* l, float* r, uint32_t n) {
  const bool on = p->hall > 0.0f || p->echo > 0.0f;
  if (!running && !on) {
    for (uint32_t i = 0; i < n; ++i) {
      l[i] = r[i] = in[i];
    }
    return;
  }
  if (!running) {
    start();
  }
  const uint8_t quality = p->quality % GRAV_FX_QUALITIES;
  const bool clean = quality == GRAV_FX_DIGITAL;
  const bool tape_on = quality >= GRAV_FX_CLEAN_TAPE;

  // HALL: Beads' REVERB curve for the send (its mix, its send), TIME and TONE over the range Beads' one knob swept,
  // and further; off DIGITAL the tail longer and darker, as Beads' "caveman" media have it
  {
    const float m = 0.9f * p->hall * p->hall;
    const float s = m * (2.0f - m);
    hall_send = s * (2.0f - s);
    reverb->set_time((clean ? 0.3f : 0.38f) + (clean ? 0.67f : 0.6f) * p->time);
    reverb->set_lp((0.2f + 0.75f * p->tone) * (clean ? 1.0f : 0.72f));
    reverb->set_diffusion(0.3f + 0.45f * p->diffuse);
    reverb->set_mod(1.6f * p->mod);
  }

  // ECHO: its time fitted to the line (halved until it fits: still in time), its send squared, the loop's gain under
  // 1 (a tape's a little over: its saturation holds it), its low pass darker as the media wear
  float target = grav_echo_seconds(p->echo_time, p->bpm) * kSampleRate;
  while (target > float(kEchoSize - 4)) {
    target *= 0.5f;
  }
  if (echo_delay <= 0.0f) {
    echo_delay = target;
  }
  const float echo_target = p->echo * p->echo;
  const float feedback = p->feedback * (tape_on ? 1.04f : 0.97f);
  static const float kLoopLp[GRAV_FX_QUALITIES] = { 0.85f, 0.6f, 0.45f, 0.3f };
  const float loop_lp = kLoopLp[quality];

  // the wow and flutter's randomness from its own stream, whichever core runs it
  const uint32_t previous_random = stmlib::Random::state();
  stmlib::Random::Seed(random_state);
  tape->set_amount(quality == GRAV_FX_DIRTY_TAPE ? 1.0f : 0.35f);

  float peak = 0.0f;
  for (uint32_t base = 0; base < n; base += kChunk) {
    const size_t size = std::min(size_t(n - base), kChunk);
    float to_hall[kChunk];
    beads::FloatFrame wet[kChunk];
    for (size_t i = 0; i < size; ++i) {
      const float x = in[base + i];
      echo_send += (echo_target - echo_send) * 0.002f;
      echo_delay += (target - echo_delay) * 0.0002f;   // (a change of TIME glides, ~110 ms)
      // the echoes: read back, then the send and the loop written in
      float read = echo_write - echo_delay;
      if (read < 0.0f) {
        read += float(kEchoSize);
      }
      const int32_t ri = static_cast<int32_t>(read);
      const float rf = read - float(ri);
      const float a = echo_buffer[ri % kEchoSize], b = echo_buffer[(ri + 1) % kEchoSize];
      const float echo = (a + (b - a) * rf) / kEchoScale;
      ONE_POLE(echo_lp, echo, loop_lp);
      ONE_POLE(echo_hp, echo_lp, 0.0085f);   // (~60 Hz)
      float loop = x * echo_send + (echo_lp - echo_hp) * feedback;
      if (!clean) {
        loop = media(loop, quality);
      }
      echo_buffer[echo_write] = static_cast<int16_t>(stmlib::Clip16(static_cast<int32_t>(loop * kEchoScale)));
      echo_write = echo_write + 1 < kEchoSize ? echo_write + 1 : 0;
      // into HALL: the send and the echoes, through the media
      to_hall[i] = clean ? x + echo : media(x + echo, quality);
      wet[i].l = wet[i].r = echo;
    }
    float hall_l[kChunk] = { 0.0f }, hall_r[kChunk] = { 0.0f };
    reverb->Process(to_hall, hall_l, hall_r, size, hall_send);
    for (size_t i = 0; i < size; ++i) {
      wet[i].l += hall_l[i];
      wet[i].r += hall_r[i];
    }
    if (tape_on) {
      tape->Process(wet, size);
    }
    for (size_t i = 0; i < size; ++i) {
      const float x = in[base + i];
      l[base + i] = x + wet[i].l;
      r[base + i] = x + wet[i].r;
      const float w = std::max(fabsf(wet[i].l), fabsf(wet[i].r));
      peak = std::max(peak, w);
    }
  }
  random_state = stmlib::Random::state();
  stmlib::Random::Seed(previous_random);

  // nothing sent, and the tails died away: off (the next start clears the lines)
  if (!on && peak < kQuietPeak) {
    quiet += n;
    if (quiet >= kQuietSamples) {
      running = false;
    }
  } else {
    quiet = 0;
  }
}
