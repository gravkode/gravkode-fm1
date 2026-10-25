// Real concurrent DSP test. Build with tests/run_dual_core_test.py after --host.
#include <atomic>
#include <cassert>
#include <chrono>
#include <cstdio>
#include <cstring>
#include <cmath>
#include <thread>
#include <vector>
#include "stmlib/utils/random.h"
extern "C" {
#include "dsp/grav_engine.h"
#include "dsp/grav_plaits.h"
#include "presets/presets.h"
#include "plat.h"
}

static thread_local uint32_t core_index;
static std::thread worker;
static bool enabled = true, accept_job = true;
static std::atomic<bool> running(false);
static std::atomic<unsigned> overlaps(0);
static unsigned dispatched;

extern "C" uint32_t plat_cpu_index(void) { return core_index; }
extern "C" uint32_t plat_ticks24(void) {
    if (!core_index && running.load()) ++overlaps;
    return uint32_t(std::chrono::duration_cast<std::chrono::nanoseconds>(
        std::chrono::steady_clock::now().time_since_epoch()).count() / 1000 * 24);
}
extern "C" int plat_cpu2_on(void) { return enabled; }
extern "C" int plat_cpu2_run(void (*fn)(uint32_t), uint32_t arg) {
    if (!enabled || !accept_job) return 0;
    assert(!worker.joinable());
    ++dispatched;
    worker = std::thread([=] {
        core_index = 1;
        running = true;
        fn(arg);
        running = false;
    });
    return 1;
}
extern "C" int plat_cpu2_wait(void) { worker.join(); return 0; }

static std::vector<int32_t> render(unsigned engine, unsigned voices, bool fx_on,
                                 bool dual, bool reject, bool offline, bool toggle, bool drums = false) {
    stmlib::Random::Seed(0x21);
    synth_t synth;
    grav_engine_t dsp;
    synth_init(&synth);
    synth_set_voices(&synth, voices);
    synth_envelopes(&synth, 0.005f, 0.02f, 0.7f, 0.025f, 44100.0f);
    grav_engine_init(&dsp);
    grav_dual_core = dual;
    enabled = !offline;
    accept_job = !reject;
    grav_beads_params_t fx = {};
    fx.density = 0.8f; fx.time = 0.4f; fx.size = 0.35f; fx.shape = 0.6f; fx.pitch = 0.5f;
    fx.feedback = 0.2f; fx.dry_wet = fx_on ? 0.35f : 0.0f;
    fx.reverb = fx_on ? 0.6f : 0.0f;
    fx.quality = engine % GRAV_FX_QUALITIES;
    fx.seed_auto = 1;
    std::vector<int32_t> result;
    for (unsigned b = 0; b < 40; ++b) {
        // Changes and steals occur while releases and reverb are still sounding.
        if (!(b % 5)) {
            if (b) synth_note_off(&synth, 48 + (b / 5 - 1) % 12);
            synth_note_on(&synth, 48 + (b / 5) % 12, 100);
        }
        if (b == 1)
            for (unsigned v = 1; v < voices; ++v) synth_note_on(&synth, 60 + 3 * v, 80 + v);
        if (drums && b % 3 == 0) {          // the drum tracks D1..D4 (voices 4..7): a hit each, in turn
            const unsigned d = SYNTH_S_TRACKS + (b / 3) % SYNTH_D_TRACKS;
            synth_track_note_off(&synth, d, 36 + 2 * (b / 3 % 4));
            synth_track_note_on(&synth, d, 36 + 2 * (b / 3 % 4), 90 + b);
        }
        if (b == 25) synth_panic(&synth);
        if (toggle) grav_dual_core = b % 3 != 0;
        fx.freeze = fx_on && b >= 20 && b < 24;
        // 256 is not a multiple of Plaits' 12-sample FIFO; also exercise partial blocks.
        const unsigned n = b % 7 == 0 ? 37 : 256;
        int32_t output[512];
        const unsigned current_engine = b >= 16 && b < 33 ? (engine + 1) % GRAV_ENGINE_COUNT : engine;
        const float timbre = float(b % 3) * 0.5f;
        if (drums) {                         // each track its sound: the synth tracks the engine, the drums a kit
            static const unsigned KIT[2][SYNTH_D_TRACKS] = {   // Plaits' first models; the second (0.19)
                {GRAV_ENGINE_BASS_DRUM, GRAV_ENGINE_SNARE_DRUM, GRAV_ENGINE_HI_HAT, GRAV_ENGINE_HI_HAT},
                {GRAV_ENGINE_SYN_KICK, GRAV_ENGINE_SYN_SNARE, GRAV_ENGINE_RING_HAT, GRAV_ENGINE_HI_HAT}};
            grav_sound_t sound[SYNTH_TRACKS];
            for (unsigned t = 0; t < SYNTH_TRACKS; ++t) {
                const bool drum = t >= SYNTH_S_TRACKS;
                sound[t].engine = drum ? KIT[engine & 1u][t - SYNTH_S_TRACKS] : current_engine;
                sound[t].timbre = drum ? 0.3f + 0.1f * float(t - SYNTH_S_TRACKS) : timbre;
                sound[t].morph = drum ? 0.6f : 0.5f;
                sound[t].harmonics = drum ? 0.45f : 0.75f;
                sound[t].pitch = drum ? float(t) - 5.5f : 0.0f;   // (a drum's TUNE)
                grav_drum_shape(sound[t].engine, sound[t].harmonics, sound[t].shape);
                if (drum)
                    sound[t].shape[1] = 0.25f * float(t - SYNTH_S_TRACKS);   // (some DRIVE, SWEEP)
            }
            grav_engine_render_tracks(&dsp, output, n, &synth, 3200, sound, 0.0f, &fx);
        } else
            grav_engine_render(&dsp, output, n, &synth, current_engine, 3200, timbre, 0.5f, 0.75f, 0.0f, &fx);
        assert(!worker.joinable());
        assert((grav_debug.masks[0] & grav_debug.masks[1]) == 0);
        result.insert(result.end(), output, output + 2 * n);
    }
    return result;
}

// Audio regressions: changing ENV must change 6-op output, not just its hidden app envelope.
static std::vector<int32_t> six_op_audio(unsigned engine, float attack, float decay, float release,
                                       unsigned destination = 0, bool modulate = false, bool held = false,
                                       int retrigger_block = -1, float retrigger_attack = 0.0f) {
    stmlib::Random::Seed(0x21);
    synth_t synth;
    grav_engine_t dsp;
    synth_init(&synth);
    synth_envelopes(&synth, attack, decay, 0.7f, release, 44100.0f);
    grav_engine_init(&dsp);
    grav_dual_core = 0;
    enabled = true;
    accept_job = true;
    grav_beads_params_t fx = {};
    synth_note_on(&synth, 60, 100);
    std::vector<int32_t> result;
    for (unsigned b = 0; b < 100; ++b) {
        if (b == 60 && !held) synth_note_off(&synth, 60);
        if ((int)b == retrigger_block) {
            synth_envelopes(&synth, retrigger_attack, decay, 0.7f, release, 44100.0f);
            synth_note_on(&synth, 60, 100);
        }
        const float lfo = modulate ? 0.8f * std::sin(float(b) * 0.17f) : 0.0f;
        float timbre = 0.5f + (destination == 0 ? lfo * 0.5f : 0.0f);
        float morph = 0.5f + (destination == 1 ? lfo * 0.5f : 0.0f);
        float pitch = destination == 2 ? lfo * 2.0f : 0.0f;
        int32_t out[512];
        grav_engine_render(&dsp, out, 256, &synth, engine, 3200, timbre, morph, 0.75f, pitch, &fx);
        for (unsigned i = 0; i < 256; ++i) result.push_back(out[2 * i]);
    }
    return result;
}

static double energy(const std::vector<int32_t>& samples, unsigned first, unsigned last) {
    double sum = 0;
    for (unsigned i = first; i < last; ++i) {
        const double sample = double(samples[i]) / 8388608.0;
        sum += sample * sample;
    }
    return sum / (last - first);
}

static bool six_op_controls() {
    for (unsigned e = GRAV_ENGINE_SIX_OP_1; e <= GRAV_ENGINE_SIX_OP_3; ++e) {
        const auto fast = six_op_audio(e, 0.0f, 0.0f, 0.15f);
        const auto slow_attack = six_op_audio(e, 2.0f, 0.0f, 0.15f);
        const auto slow_decay = six_op_audio(e, 0.0f, 2.0f, 0.15f);
        const auto no_release = six_op_audio(e, 0.0f, 0.0f, 0.0f);
        const double early = energy(fast, 256, 12 * 256);
        const double attack_ratio = energy(slow_attack, 256, 12 * 256) / early;
        const double decay_ratio = energy(slow_decay, 5 * 256, 20 * 256) /
                                   energy(fast, 5 * 256, 20 * 256);
        const double short_release = energy(no_release, 61 * 256, 64 * 256);
        const double long_release = energy(fast, 61 * 256, 64 * 256);
        if (!(early > 1e-12 && attack_ratio < 0.02 && decay_ratio > 1.3 &&
              short_release == 0.0 && long_release > 1e-12 && energy(fast, 95 * 256, 100 * 256) == 0.0)) {
            std::fprintf(stderr, "Six-op ENV failure engine=%u: attack ratio=%g decay ratio=%g release=%g/%g\n",
                         e, attack_ratio, decay_ratio, short_release, long_release);
            return false;
        }
        const auto held_note = six_op_audio(e, 0.0f, 0.0f, 4.0f, 0, false, true);
        const auto long_tail = six_op_audio(e, 0.0f, 0.0f, 4.0f);
        const double tail_ratio = energy(long_tail, 70 * 256, 100 * 256) /
                                  energy(held_note, 70 * 256, 100 * 256);
        const auto repeat_fast = six_op_audio(e, 0.0f, 0.0f, 4.0f, 0, false, false, 70, 0.0f);
        const auto repeat_slow = six_op_audio(e, 0.0f, 0.0f, 4.0f, 0, false, false, 70, 2.0f);
        const double repeat_ratio = energy(repeat_slow, 71 * 256, 80 * 256) /
                                    energy(repeat_fast, 71 * 256, 80 * 256);
        if (!(tail_ratio > 0.8 && repeat_ratio < 0.02)) {
            std::fprintf(stderr, "Six-op repeated ENV failure engine=%u: long-release ratio=%g repeat-attack ratio=%g\n",
                         e, tail_ratio, repeat_ratio);
            return false;
        }
        std::printf("Six-op bank %u: release/held energy %.3f, slow/fast repeated-attack energy %.6f\n",
                    e - GRAV_ENGINE_SIX_OP_1 + 1, tail_ratio, repeat_ratio);
        for (unsigned destination = 0; destination < 3; ++destination) {
            const auto modulated = six_op_audio(e, 0.0f, 0.0f, 0.15f, destination, true);
            if (modulated == fast) {
                std::fprintf(stderr, "Six-op modulation did not affect audio: engine=%u dest=%u\n", e, destination);
                return false;
            }
        }
    }
    std::puts("PASS: all three 6-op banks respond to ATTACK, DECAY, RELEASE and TIMBRE/MORPH/PITCH modulation");
    return true;
}

// The overload protection: a lone voice is kept; a voice taken fades out over GRAV_SHED_FADE (128) samples,
// from where it was (no step at the cut), then sleeps.
static bool shed_fade() {
    for (unsigned voices = 2; voices <= 4; voices += 2) {
        std::vector<int32_t> runs[2];
        for (int shed = 0; shed < 2; ++shed) {
            stmlib::Random::Seed(0x21);
            synth_t synth;
            grav_engine_t dsp;
            synth_init(&synth);
            synth_set_voices(&synth, voices);
            synth_envelopes(&synth, 0.0f, 0.0f, 0.7f, 2.0f, 44100.0f);
            grav_engine_init(&dsp);
            grav_dual_core = 0;
            grav_beads_params_t fx = {};
            synth_note_on(&synth, 60, 100);
            synth_shed(&synth);                        // alone: kept
            if (synth.shed_count) {
                std::fprintf(stderr, "Shed took the only voice sounding\n");
                return false;
            }
            synth_note_on(&synth, 67, 100);
            int32_t out[512];
            for (unsigned b = 0; b < 8; ++b) {
                if (b == 4 && shed) synth_shed(&synth);   // takes the older note, C4
                grav_engine_render(&dsp, out, 256, &synth, GRAV_ENGINE_VIRTUAL_ANALOG, 3200, 0.5f, 0.5f, 0.5f, 0.0f, &fx);
                if (b >= 4) runs[shed].insert(runs[shed].end(), out, out + 512);
            }
            if (shed && (synth.shed_count != 1 || synth_voices_held(&synth) != 1)) {
                std::fprintf(stderr, "Shed did not take exactly one of two voices\n");
                return false;
            }
        }
        // the first block after the cut: the same first sample, then the difference grows as C4 fades
        const int32_t first = runs[0][0] - runs[1][0];
        double early = 0, late = 0;
        for (unsigned i = 0; i < 16; ++i) early += std::fabs(double(runs[0][2 * i] - runs[1][2 * i]));
        for (unsigned i = 128; i < 144; ++i) late += std::fabs(double(runs[0][2 * i] - runs[1][2 * i]));
        if (first != 0 || !(late > 8 * early && late > 0)) {
            std::fprintf(stderr, "Shed fade failure voices=%u: first %d, early %g, late %g\n", voices, first, early, late);
            return false;
        }
    }
    std::puts("PASS: overload protection keeps a lone voice and fades a voice it takes");
    return true;
}

static bool preset_audio() {
    preset_bank_t bank;
    presets_init(&bank);
    unsigned count = 0;
    for (unsigned drum = 0; drum < 2; ++drum)
        for (unsigned e = 0; e < GRAV_ENGINE_ALL; ++e)
            for (int id = 0; id < int(PRESET_FACTORY); ++id) {
                preset_t preset;
                if (!preset_get(&bank, e, drum, id, &preset)) continue;
                grav_plaits_init();
                const unsigned voice = drum ? 4 : 0;
                grav_plaits_voice_engine(voice, e);
                grav_plaits_params_t p = {};
                p.note = drum ? (e == 13 || e == 24 ? 36 : 60) : 60;
                p.timbre = preset.osc[0] * 0.01f;
                p.morph = preset.osc[1] * 0.01f;
                p.harmonics = preset.osc[2] * 0.01f;
                for (unsigned k = 0; k < 3; ++k) p.shape[k] = preset.shape[k] * 0.01f;
                p.accent = 100.0f / 127.0f;
                float peak = 0;
                for (unsigned b = 0; b < 400; ++b) {
                    float out[24], aux[24];
                    p.strike = b == 0;
                    p.gate = b < 300;
                    grav_plaits_render(voice, &p, out, aux, 24);
                    for (unsigned k = 0; k < 24; ++k) {
                        if (!std::isfinite(out[k]) || !std::isfinite(aux[k])) {
                            std::fprintf(stderr, "Invalid factory audio engine=%u drum=%u preset=%d block=%u out=%g aux=%g\n",
                                         e, drum, id, b, out[k], aux[k]);
                            return false;
                        }
                        peak = std::fmax(peak, std::fabs(out[k]));
                    }
                }
                if (peak < 1e-9f) {
                    std::fprintf(stderr, "Silent factory audio engine=%u drum=%u preset=%d\n", e, drum, id);
                    return false;
                }
                ++count;
            }
    // A short drum, a long external release: idle saves must not wait forever on an envelope frozen asleep.
    synth_t synth;
    grav_engine_t dsp;
    synth_init(&synth);
    synth_envelopes(&synth, 0, 0, 0.7f, 20, 44100);
    grav_engine_init(&dsp);
    grav_dual_core = 0;
    grav_sound_t sound[SYNTH_TRACKS] = {};
    for (unsigned t = 4; t < 8; ++t) {
        sound[t].engine = GRAV_ENGINE_BASS_DRUM;
        sound[t].timbre = 0.3f;
        sound[t].morph = 0.05f;
    }
    synth_track_note_on(&synth, 4, 36, 100);
    grav_beads_params_t fx = {};
    for (unsigned b = 0; b < 1000; ++b) {
        if (b == 2) synth_track_note_off(&synth, 4, 36);
        int32_t out[512];
        grav_engine_render_tracks(&dsp, out, 256, &synth, 3200, sound, 0, &fx);
    }
    if (grav_debug.active || !synth_quiet(&synth)) {
        std::fprintf(stderr, "A silent drum blocked idle saving\n");
        return false;
    }
    std::printf("PASS: %u factory presets produce finite nonzero audio; a finished drum permits idle saving\n", count);
    return true;
}

int main() {
    if (!preset_audio()) return 1;
    if (!shed_fade()) return 1;
    // Retrigger a still-audible envelope, then shorten a release which is already in progress.
    envelope_t envelope;
    envelope_init(&envelope);
    envelope_set(&envelope, 0.0f, 0.0f, 0.7f, 1.0f, 1000.0f);
    envelope_note_on(&envelope);
    envelope_process(&envelope); envelope_process(&envelope);
    envelope_set(&envelope, 2.0f, 0.0f, 0.7f, 1.0f, 1000.0f);
    envelope_note_on(&envelope);
    for (unsigned i = 0; i < 4; ++i) envelope_process(&envelope);
    assert(envelope.level < 0.002f);
    for (unsigned i = 0; i < 100; ++i) envelope_process(&envelope);
    envelope_note_off(&envelope);
    for (unsigned i = 0; i < 10; ++i) envelope_process(&envelope);
    assert(envelope.level > 0.0f);
    envelope_set(&envelope, 2.0f, 0.0f, 0.7f, 0.0f, 1000.0f);
    assert(envelope_process(&envelope) == 0.0f && envelope.stage == ENV_IDLE);
    if (!six_op_controls()) return 1;
    unsigned comparisons = 0;
    for (unsigned e = 0; e < GRAV_ENGINE_COUNT; ++e) {
        for (unsigned voices = 1; voices <= 5; ++voices) {   // 5: the four synth voices and the four drums'
            const bool drums = voices > 4;
            for (bool fx : {false, true}) {
                const auto reference = render(e, drums ? 4 : voices, fx, false, false, false, false, drums);
                for (unsigned mode = 0; mode < 4; ++mode) {
                    const auto actual = render(e, drums ? 4 : voices, fx, true, mode == 1, mode == 2, mode == 3, drums);
                    if (actual != reference) {
                        std::fprintf(stderr, "Mismatch engine=%u voices=%u drums=%d fx=%d mode=%u\n", e,
                                     drums ? 4 : voices, drums, fx, mode);
                        return 1;
                    }
                    ++comparisons;
                }
            }
        }
    }
    assert(dispatched > 0 && overlaps > 0);
    std::printf("PASS: %u bit-exact comparisons across 24 engines, 1..4 voices and 4 + 4 drums, FX off/on; "
                "%u real worker jobs, %u overlap observations\n", comparisons, dispatched, overlaps.load());
}
