/* SPDX-License-Identifier: MPL-2.0 */
/* USB audio MULTI (bsp/chip/usb.c, FELUCCA_UAC_CH 10): the render's blocks at the I2S rate (360 MHz / 8160 =
 * 44,117.647 Hz) into packets at the USB frames' rate (the 44.1 pattern, 44,100 a second), resampled by uac_tap_m.
 * Thirty simulated seconds, a render's frames landing as it ends (it takes a share of its block's time, up to 95 %,
 * the packets going on meanwhile), the I2S off by up to 200 ppm: the ring's fill stays near its target (nothing
 * dropped, nothing repeated), and sines on the master and the tracks come out clean (their residual against the ideal
 * sine).
 *   cc -O2 -w -Ibsp/chip -Ibsp/boards/fm1 -Idevice tests/uac_multi_test.c -lm && ./a.out */
#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#define RING_PUBLISH() __asm__ volatile("" ::: "memory")
#define HALF_FRAMES 256u
#define FELUCCA_UAC 1
#define FELUCCA_UAC_OUT 1
#define FELUCCA_UAC_48K 0
#define FELUCCA_UAC_CH 10
#define UAC_RAMFN __attribute__((noinline))   /* (the host: no .ram_text) */
static void fm1_delay_ms(uint32_t ms) { (void)ms; }
#include "usb.c"

#define FS_IN (360e6 / 8160.0)
#define SECONDS 30
#define CH FELUCCA_UAC_CH
static int16_t got[(SECONDS + 1) * 44100 * CH];
static const double FREQ[CH] = {1000, 2000, 500, 700, 1100, 3000, 5000, 7000, 9000, 11000};
static const double AMP[CH] = {0.5, 0.5, 0.25, 0.25, 0.25, 0.25, 0.25, 0.25, 0.25, 0.25};

/* the residual of channel c against the best sine at frequency f (Hz at 44,100), over frames a..b: dB under it */
static double snr(uint32_t c, double f, uint32_t a, uint32_t b)
{
    double ss = 0, sc = 0, cc = 0, ys = 0, yc = 0, e = 0, p = 0;
    uint32_t i;
    for (i = a; i < b; i++) {
        double s = sin(2 * M_PI * f * i / 44100.0), k = cos(2 * M_PI * f * i / 44100.0), y = got[i * CH + c];
        ss += s * s, sc += s * k, cc += k * k, ys += y * s, yc += y * k;
    }
    {
        double det = ss * cc - sc * sc, A = (ys * cc - yc * sc) / det, B = (yc * ss - ys * sc) / det;
        for (i = a; i < b; i++) {
            double m = A * sin(2 * M_PI * f * i / 44100.0) + B * cos(2 * M_PI * f * i / 44100.0), y = got[i * CH + c];
            e += (y - m) * (y - m), p += m * m;
        }
    }
    return 10 * log10(p / (e > 0 ? e : 1e-9));
}

/* one run: the I2S that much off its nominal rate (ppm, a crystal's or a host's difference), each render taking that
 * share of its block's time */
static int run(double ppm, double load)
{
    static int32_t out[2 * HALF_FRAMES];
    const double fs_in = FS_IN * (1.0 + ppm * 1e-6);
    uint32_t frames = 0, blocks = 0, i, c, lo = 0xFFFFFFFFu, hi = 0, fails = 0;
    double t_render = 0, t_packet = 0, in_at = 0, t_tap = 0;
    int pending = 0;
    memset(&uac, 0, sizeof uac);
    ua_w = ua_r = 0;
    usb.config = 1;
    usb.up = 1;
    uac.alt = 1;
    uac.flowing = 1;
    uac_multi = 1;
    printf(" I2S %+.0f ppm, renders taking %.0f %% of their time\n", ppm, load * 100);
    while (t_packet < SECONDS) {
        if (pending && t_tap <= t_packet) {             /* the render ends: its frames into the ring */
            uac_tap(out, HALF_FRAMES);
            pending = 0;
            t_render += HALF_FRAMES / fs_in;
            if (++blocks > 3000) {                      /* (after the loop has settled: ~17 s) */
                if (uac.fill_min < lo) lo = uac.fill_min;
                if (uac.fill_min > hi) hi = uac.fill_min;
            }
        } else if (!pending && t_render <= t_packet) {  /* a render begins: the next 256 frames at the I2S rate */
            uac_render_start();
            for (i = 0; i < HALF_FRAMES; i++, in_at += 1) {
                for (c = 0; c < CH; c++) {
                    double v = AMP[c] * sin(2 * M_PI * FREQ[c] * in_at / fs_in);
                    if (c < 2)
                        out[2 * i + c] = (int32_t)lrint(v * 8388607.0);
                    else
                        ua_tblk[i * UA_TRK + c - 2] = (int16_t)lrint(v * 32767.0);
                }
            }
            pending = 1;
            t_tap = t_render + load * HALF_FRAMES / fs_in;
        } else {                                        /* a USB frame: one packet */
            uint32_t n = uac_packet(ep4tx);
            memcpy(&got[frames * CH], ep4m, n * CH * sizeof(int16_t));
            frames += n;
            t_packet += 0.001;
        }
    }
    printf("  %u frames in %d s (%.3f a second), %u renders\n", frames, SECONDS, frames / (double)SECONDS, blocks);
    printf("  ring fill at a render's start %u..%u (within %u..%u), overruns %u, underruns %u, packets +1 %u, -1 %u\n",
           lo, hi, (unsigned)RM_TGT - 60u, (unsigned)RM_TGT + 60u, uac.overruns, uac.underruns, uac.adj_up, uac.adj_down);
    if (uac.overruns || uac.underruns || uac.adj_up || uac.adj_down || lo < RM_TGT - 60u || hi > RM_TGT + 60u)
        fails++, printf("FAIL the ring\n");
    if (frames != SECONDS * 44100u)
        fails++, printf("FAIL %u frames, not %u\n", frames, SECONDS * 44100u);
    for (c = 0; c < CH; c++) {   /* the last 10 s, ten windows of 0.1 s; in each the best frequency (the steering's
                                  * slow drift, under 0.01 %, is no fault: far under a wow one would hear) */
        double worst = 1e9, f = FREQ[c];   /* (resampled, its pitch kept: FREQ at 44,100 too) */
        uint32_t w, j;
        for (w = 0; w < 10; w++) {
            const uint32_t a = (SECONDS - 10) * 44100u + w * 44100u;
            double best = -1e9, at = 0, span = 1e-5;
            uint32_t r;
            for (r = 0; r < 4u; r++, span /= 10) {   /* the best frequency: 41 tries a step, each 10x finer */
                double c0 = at;
                for (j = 0; j <= 40u; j++) {
                    double x = c0 + (j - 20.0) * span, s = snr(c, f * (1.0 + x), a, a + 4410u);
                    if (s > best) best = s, at = x;
                }
            }
            if (best < worst) worst = best;
        }
        printf("  ch %2u %6.0f Hz: worst residual %.1f dB under the sine\n", c + 1, FREQ[c], worst);
        if (worst < (FREQ[c] <= 3000 ? 65.0 : 60.0))
            fails++, printf("FAIL ch %u\n", c + 1);
    }
    return fails;
}

int main(void)
{
    int fails = run(0, 0.1) + run(200, 0.6) + run(-200, 0.95);
    printf(fails ? "uac_multi: FAILED\n" : "uac_multi: ok\n");
    return fails != 0;
}
