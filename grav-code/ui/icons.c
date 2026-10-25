/* SPDX-License-Identifier: MPL-2.0 */
/* GRAV CODE's engine icons (0.20): each engine drawn by code, alive. An icon is drawn into a box of the canvas
 * (device/gfx.c, in scope: the app is part of the unity build), clipped to it, from the time, the engine's three
 * knobs (a drum's TONE, DECAY and SHAPE's first) and a note's strike, so that turning a knob or playing a note shows
 * on it: FOLD folds WAVESHAPE's wave further, INDEX widens FM's orbit, a kick's ring jumps at each hit. Lines in the
 * theme's two hues, the strike's flash white; small (the track's line, the tracks' list): fewer details. A few
 * hundred bytes of code an icon, no bitmaps. Included by app.c. */

typedef struct {
    int32_t x, y, w, h;                  /* the box on the canvas: drawing is clipped to it */
    float t;                             /* seconds (wrapped every 10 minutes) */
    float k[3];                          /* the engine's knobs, 0..1 */
    float hit;                           /* a note's strike: 1 as it begins, decaying; a held note keeps some */
    int small;                           /* small: fewer details */
} icon_t;

#define IC_PI 3.14159265f
#define IC_TAU 6.2831853f

static const icon_t *ic;                 /* the icon being drawn */

static void ic_pset(int32_t x, int32_t y, uint16_t c)
{
    if (x >= ic->x && x < ic->x + ic->w && y >= ic->y && y < ic->y + ic->h)
        cv_pset(x, y, c);
}

static int32_t ic_round(float v) { return (int32_t)(v < 0.0f ? v - 0.5f : v + 0.5f); }

static void ic_line(float fx0, float fy0, float fx1, float fy1, uint16_t c)
{
    int32_t x0 = ic_round(fx0), y0 = ic_round(fy0), x1 = ic_round(fx1), y1 = ic_round(fy1);
    int32_t dx = x1 > x0 ? x1 - x0 : x0 - x1, sx = x0 < x1 ? 1 : -1;
    int32_t dy = y1 > y0 ? y0 - y1 : y1 - y0, sy = y0 < y1 ? 1 : -1;
    int32_t err = dx + dy, guard = 600;
    while (guard--) {
        int32_t e2 = 2 * err;
        ic_pset(x0, y0, c);
        if (x0 == x1 && y0 == y1)
            break;
        if (e2 >= dy) {
            err += dy;
            x0 += sx;
        }
        if (e2 <= dx) {
            err += dx;
            y0 += sy;
        }
    }
}

static void ic_rect(float x, float y, float w, float h, uint16_t c)
{
    for (int32_t j = ic_round(y); j < ic_round(y + h); j++)
        for (int32_t i = ic_round(x); i < ic_round(x + w); i++)
            ic_pset(i, j, c);
}

static void ic_circle(float cx, float cy, float r, uint16_t c, int dotted)
{
    const int32_t n = (int32_t)(r * 6.3f) + 8;
    float px = cx + r, py = cy;
    for (int32_t i = 1; i <= n; i++) {
        const float a = IC_TAU * (float)i / (float)n, x = cx + r * fm_cosf(a), y = cy + r * fm_sinf(a);
        if (!dotted || (i & 2))
            ic_line(px, py, x, y, c);
        px = x;
        py = y;
    }
}

static void ic_disc(float cx, float cy, float r, uint16_t c)
{
    for (int32_t j = ic_round(cy - r); j <= ic_round(cy + r); j++)
        for (int32_t i = ic_round(cx - r); i <= ic_round(cx + r); i++) {
            const float dx = (float)i - cx, dy = (float)j - cy;
            if (dx * dx + dy * dy <= r * r + 0.5f)
                ic_pset(i, j, c);
        }
}

/* a wave across the box: f(u) in -1..1 for u 0..1, drawn about row cy, amp pixels up and down */
static void ic_wave(float (*f)(float u), float cy, float amp, uint16_t c)
{
    float px = (float)ic->x, py = cy - f(0.0f) * amp;
    const int32_t n = ic->w;
    for (int32_t i = 1; i <= n; i++) {
        const float x = (float)(ic->x + i), y = cy - f((float)i / (float)n) * amp;
        ic_line(px, py, x, y, c);
        px = x;
        py = y;
    }
}

static uint32_t ic_hash(uint32_t x)      /* a fixed scatter of points */
{
    x ^= x >> 16;
    x *= 0x7FEB352Du;
    x ^= x >> 15;
    x *= 0x846CA68Bu;
    return x ^ (x >> 16);
}

static float ic_rnd(uint32_t i) { return (float)(ic_hash(i) & 0xFFFFu) / 65535.0f; }

#define CX ((float)ic->x + (float)ic->w * 0.5f)
#define CY ((float)ic->y + (float)ic->h * 0.5f)
#define W ((float)ic->w)
#define H ((float)ic->h)
#define K0 (ic->k[0])
#define K1 (ic->k[1])
#define K2 (ic->k[2])
#define T (ic->t)
#define HIT (ic->hit)
#define LIVE (0.3f + 0.7f * HIT)         /* a wave's amplitude: playing, full */

/* ------------------------------------------------------------- the waves --- */
static float f_fold(float u)             /* WAVESHAPE: a sine through a folder */
{
    const float s = fm_sinf(u * IC_TAU * 1.5f + T * 2.0f) * LIVE;
    return fm_sinf((s * (1.0f + K0 * 5.0f) + (K1 - 0.5f) * 1.4f) * (0.8f + K2 * 0.6f));
}

static float f_fm(float u)               /* FM 2-OP's output */
{
    const float ph = u * IC_TAU * 2.0f + T * 3.0f, ratio = 1.0f + (float)(int32_t)(K2 * 4.0f);
    return fm_sinf(ph + K0 * 3.0f * LIVE * fm_sinf(ph * ratio));
}

static float f_pulse(float u)            /* ANALOG: a pulse, its width K0 */
{
    const float v = u * 2.0f + T * 0.5f;
    return (v - fm_floorf(v) < 0.1f + 0.8f * K0 ? 1.0f : -1.0f) * LIVE;
}

static float f_saw(float u)              /* ANALOG: triangle to saw (K1), detuned (K2) */
{
    const float v = u * 2.0f + T * (0.5f + K2 * 0.6f), p = v - fm_floorf(v), a = 0.5f + 0.49f * K1;
    return (p < a ? p / a * 2.0f - 1.0f : 1.0f - (p - a) / (1.0f - a) * 2.0f) * LIVE;
}

static float f_grains(float u)           /* GRAIN: windowed sine packets */
{
    const float g = u * 4.0f, v = g - fm_floorf(g), win = fm_sinf(IC_PI * fm_minf(1.0f, v / (0.4f + 0.6f * K1)));
    return win * win * fm_sinf(IC_TAU * (2.0f + K0 * 8.0f) * v + T * 5.0f) * LIVE;
}

static float f_bent(float u)             /* PHASE DIST: a sine, its phase bent */
{
    const float v = u * 2.0f + T * 0.4f, p = v - fm_floorf(v), d = 0.5f - 0.45f * K0 * LIVE;
    const float q = p < d ? p * 0.5f / d : 0.5f + (p - d) * 0.5f / (1.0f - d);
    return fm_sinf(q * IC_TAU + K1 * fm_sinf(p * IC_TAU));
}

static float f_sine(float u) { return fm_sinf(u * IC_TAU * 2.0f + T * 0.4f); }

static float f_chirp(float u)            /* SYN KICK: a sine, its pitch falling (PUNCH the depth) */
{
    const float sweep = (1.0f + 6.0f * K2) * (1.0f - fm_expf(-u * 6.0f)) / 6.0f;
    const float env = fm_expf(-u * (1.0f + 4.0f * (1.0f - K1))) * (0.2f + 0.8f * HIT);
    return fm_sinf(IC_TAU * (1.5f * u + sweep * 6.0f)) * env;
}

static float f_chip(float u)             /* CHIPTUNE: a pulse stepping up an arpeggio, in steps of 3 px */
{
    static const float ARP[4] = {1.0f, 1.25f, 1.5f, 2.0f};
    const float q = fm_floorf(u * W / 3.0f) * 3.0f / W, a = ARP[(int32_t)(q * 4.0f) & 3];
    const float v = q * 4.0f * a + T * 2.0f, p = v - fm_floorf(v);
    return p < 0.25f + 0.5f * K1 ? 0.8f : -0.8f;
}

/* ------------------------------------------------------------- the icons --- */
static void i_waveshape(void) { ic_line(ic->x, CY, ic->x + W, CY, C_LINE); ic_wave(f_fold, CY, H * 0.4f, C_HI); }

static void i_fm(void)
{
    const float cx = (float)ic->x + W * 0.42f, cy = (float)ic->y + H * (ic->small ? 0.5f : 0.42f);
    const float r = H * (0.16f + 0.22f * K0), ratio = 1.0f + (float)(int32_t)(K2 * 4.0f), a = T * (1.5f + ratio);
    const float mx = cx + fm_cosf(a) * r, my = cy + fm_sinf(a) * r;
    ic_disc(cx, cy, H * 0.11f * (1.0f + 0.3f * HIT), C_AMB);
    if (!ic->small)
        ic_circle(cx, cy, r, C_DIM, 1);
    ic_line(cx, cy, mx, my, C_DIM);
    ic_disc(mx, my, ic->small ? 1.0f : 2.0f, HIT > 0.7f ? C_WHITE : C_HI);
    if (K1 > 0.05f && !ic->small)
        ic_circle(mx, my, 3.0f + K1 * 5.0f, C_HI, 1);
    if (!ic->small)
        ic_wave(f_fm, (float)ic->y + H * 0.88f, H * 0.09f, C_HI);
}

static void i_analog(void)
{
    ic_wave(f_pulse, (float)ic->y + H * 0.3f, H * 0.17f, C_AMB);
    ic_wave(f_saw, (float)ic->y + H * 0.72f, H * 0.17f, C_HI);
}

static void i_modal(void)                /* a struck plate, its rings spreading as the note rings out */
{
    const float cx = CX, cy = CY, r = H * 0.46f;
    for (int32_t i = 0; i < 3; i++) {
        const float s = fm_minf(1.0f, (1.0f - HIT) + 0.25f * (float)i);
        ic_circle(cx, cy, r * s * (0.4f + 0.6f * K1), i == 0 ? C_HI : i == 1 ? C_AMB : C_DIM, 0);
    }
    ic_rect(cx - W * (0.1f + 0.12f * K2), cy - 1.0f, W * (0.2f + 0.24f * K2), 2.0f, HIT > 0.6f ? C_WHITE : C_HI);
}

static void i_grain(void) { ic_line(ic->x, CY, ic->x + W, CY, C_LINE); ic_wave(f_grains, CY, H * 0.42f, C_HI); }

static void i_additive(void)             /* harmonics in bumps: where (K0), how wide (K1), how many (K2) */
{
    const int32_t n = ic->small ? 8 : 14;
    const float bw = W / (float)n, centre = K0 * (float)(n - 1), width = 1.0f + K1 * 4.0f, bumps = 1.0f + K2 * 3.0f;
    for (int32_t i = 0; i < n; i++) {
        const float d = ((float)i - centre) * bumps / (float)n * 3.0f, rep = d - fm_floorf(d + 0.5f);
        const float a = fm_expf(-rep * rep * 30.0f / width) * (0.75f + 0.25f * fm_sinf(T * 3.0f + (float)i));
        const float bh = H * (0.08f + 0.85f * a * LIVE);
        ic_rect((float)ic->x + bw * (float)i + 0.5f, (float)ic->y + H - bh, fm_maxf(1.0f, bw - 1.0f), bh,
                a > 0.6f ? C_HI : C_AMB);
    }
}

static void i_wavetable(void)            /* rows of waves in perspective: the row (K0) lit, its shape (K1) */
{
    const int32_t rows = ic->small ? 3 : 5;
    for (int32_t j = 0; j < rows; j++) {
        const float y0 = (float)ic->y + H * (0.25f + 0.62f * (float)j / (float)(rows - 1)), sh = (float)j * 3.0f;
        const int lit = (int32_t)(K0 * (float)(rows - 1) + 0.5f) == j;
        float px = (float)ic->x + sh, py = y0;
        for (int32_t i = 1; i <= ic->w - (int32_t)sh * 2; i++) {
            const float u = (float)i / (W - sh * 2.0f), m = fm_minf(1.0f, K1 + (float)j * 0.15f);
            const float s = fm_sinf(u * IC_TAU * 2.0f + T * (lit ? 2.0f : 0.5f));
            const float v = s * (1.0f - m) + (s > 0.0f ? 1.0f : -1.0f) * m;
            const float x = (float)ic->x + sh + (float)i, y = y0 - v * H * 0.1f * (lit ? LIVE * 1.4f : 0.7f);
            ic_line(px, py, x, y, lit ? C_HI : C_DIM);
            px = x;
            py = y;
        }
    }
}

static void i_chords(void)               /* the chord's notes as bars: its type (K2), its inversion (K0) */
{
    static const uint8_t TYPES[5][4] = {{0, 4, 7, 12}, {0, 3, 7, 10}, {0, 5, 7, 12}, {0, 4, 7, 11}, {0, 2, 7, 14}};
    int32_t iv[4], lo = 99;
    const uint8_t *ty = TYPES[(int32_t)fm_minf(4.0f, K2 * 5.0f)];
    const int32_t inv = (int32_t)(K0 * 3.99f);
    for (int32_t i = 0; i < 4; i++) {
        iv[i] = ty[(i + inv) & 3] + ((i + inv) >= 4 ? 12 : 0);
        lo = iv[i] < lo ? iv[i] : lo;
    }
    const float bw = W / 6.0f;
    for (int32_t i = 0; i < 4; i++) {
        const float bounce = 1.0f + 0.12f * HIT * fm_sinf(T * 9.0f + (float)i);
        const float bh = H * (0.25f + 0.045f * (float)(iv[i] - lo)) * bounce;
        const float bx = (float)ic->x + bw * (0.8f + (float)i * 1.15f), by = (float)ic->y + H - bh;
        if (K1 > 0.5f)
            ic_rect(bx, by, bw * 0.8f, bh, i & 1 ? C_AMB : C_HI);
        else {
            ic_line(bx, by, bx + bw * 0.8f - 1.0f, by, C_HI);
            ic_line(bx, by, bx, by + bh - 1.0f, C_HI);
            ic_line(bx + bw * 0.8f - 1.0f, by, bx + bw * 0.8f - 1.0f, by + bh - 1.0f, C_HI);
        }
    }
}

static void i_speech(void)               /* a mouth opening on the words, its formants below */
{
    const float cx = CX, cy = (float)ic->y + H * (ic->small ? 0.5f : 0.38f);
    const float open = H * (0.04f + 0.22f * fm_fabsf(fm_sinf(T * 7.0f + K1 * 3.0f)) * LIVE);
    const float wide = W * (0.18f + 0.1f * K0);
    for (int32_t i = 0; i <= 20; i++) {  /* the lips: two arcs */
        const float a0 = IC_PI * (float)i / 20.0f, a1 = IC_PI * (float)(i + 1) / 20.0f;
        if (i < 20) {
            const float x0 = cx - fm_cosf(a0) * wide, x1 = cx - fm_cosf(a1) * wide;
            const float o0 = fm_sinf(a0) * open, o1 = fm_sinf(a1) * open;
            ic_line(x0, cy - o0, x1, cy - o1, C_HI);
            ic_line(x0, cy + o0, x1, cy + o1, C_AMB);
        }
    }
    if (!ic->small)
        for (int32_t i = 0; i < 3; i++) {  /* the formants */
            const float drift = 0.08f * fm_sinf(T * 2.0f + (float)i + K2 * 4.0f);
            const float fx = (float)ic->x + W * (0.2f + 0.3f * (float)i + drift);
            const float fh = H * (0.08f + 0.12f * LIVE * (1.0f - 0.3f * (float)i));
            ic_rect(fx - 1.0f, (float)ic->y + H - fh, 3.0f, fh, i ? C_AMB : C_HI);
        }
}

static void i_swarm(void)                /* the swarm's dots: how many (K0), how far apart (K2) */
{
    const int32_t n = ic->small ? 8 : 10 + (int32_t)(K0 * 40.0f);
    for (int32_t i = 0; i < n; i++) {
        const float a = T * (0.4f + (float)(i % 7) * 0.12f) + (float)i * 2.39f;
        const float breathe = (0.85f + 0.15f * fm_sinf(T * 3.0f + (float)i)) * (1.0f + 0.3f * HIT);
        const float r = (0.15f + 0.75f * ic_rnd((uint32_t)i)) * H * 0.5f * breathe;
        const float j = K2 * 4.0f * fm_sinf(T * 7.0f + (float)i * 1.7f);
        ic_rect(CX + fm_cosf(a) * r * 1.4f + j, CY + fm_sinf(a) * r + j, 2.0f, 2.0f, i % 3 ? C_AMB : C_HI);
    }
}

/* noise under a filter's curve: LP, BP, HP (K2), cutoff (K0), resonance (K1) */
static void i_noise(void)
{
    const uint32_t frame = (uint32_t)(T * 20.0f);
    const int32_t n = ic->small ? 20 : 120;
    for (int32_t i = 0; i < n; i++)
        ic_pset(ic->x + (int32_t)(ic_rnd(frame * 977u + (uint32_t)i) * W),
                ic->y + (int32_t)(ic_rnd(frame * 131u + (uint32_t)i + 7777u) * H), C_DIM);
    const int mode = K2 < 0.33f ? 0 : K2 < 0.66f ? 1 : 2;
    float px = 0.0f, py = 0.0f;
    for (int32_t i = 0; i <= ic->w; i++) {
        const float u = (float)i / W, d = (u - (0.1f + 0.8f * K0)) * 8.0f;
        const float lp = 1.0f / (1.0f + fm_expf(d * 1.5f)), hp = 1.0f - lp, peak = K1 * 0.6f * fm_expf(-d * d);
        const float g = (mode == 0 ? lp : mode == 1 ? fm_expf(-d * d * 0.5f) : hp) + peak;
        const float x = (float)(ic->x + i);
        const float y = (float)ic->y + H * (0.9f - 0.7f * fm_minf(1.2f, g) * (0.7f + 0.3f * LIVE));
        if (i)
            ic_line(px, py, x, y, C_HI);
        px = x;
        py = y;
    }
}

static void i_particle(void)             /* sparks falling, a flash at each note */
{
    const int32_t n = ic->small ? 5 : 6 + (int32_t)(K0 * 20.0f);
    for (int32_t i = 0; i < n; i++) {
        const float sp = 0.3f + 0.7f * ic_rnd((uint32_t)i * 3u), ph = T * sp + ic_rnd((uint32_t)i * 3u + 1u);
        const float y = (float)ic->y + (ph - fm_floorf(ph)) * H, x = (float)ic->x + ic_rnd((uint32_t)i * 3u + 2u) * W;
        ic_line(x, y - 3.0f * sp - 1.0f, x, y, C_DIM);
        ic_pset((int32_t)x, (int32_t)y, HIT > 0.6f && (i & 1) ? C_WHITE : C_HI);
    }
}

static void i_string(void)               /* a string between two pegs, ringing after each pluck */
{
    const float x0 = (float)ic->x + 4.0f, x1 = (float)ic->x + W - 4.0f, cy = CY, a = (0.06f + 0.94f * HIT) * H * 0.36f;
    ic_rect(x0 - 3.0f, cy - 4.0f, 3.0f, 8.0f, C_GRAY);
    ic_rect(x1, cy - 4.0f, 3.0f, 8.0f, C_GRAY);
    float px = x0, py = cy;
    for (int32_t i = 1; i <= (int32_t)(x1 - x0); i++) {
        const float u = (float)i / (x1 - x0);
        const float d = a * fm_sinf(IC_PI * u) * fm_sinf(T * 38.0f) +
                        a * 0.5f * K2 * fm_sinf(3.0f * IC_PI * u) * fm_sinf(T * 61.0f);
        ic_line(px, py, x0 + (float)i, cy - d, C_HI);
        px = x0 + (float)i;
        py = cy - d;
    }
}

static void i_kick(void)                 /* a membrane's rings jumping at each hit, the pitch's sweep beside */
{
    const float cx = (float)ic->x + W * (ic->small ? 0.5f : 0.36f), cy = CY, r = H * 0.32f;
    for (int32_t i = 2; i >= 0; i--)
        ic_circle(cx, cy, r * (0.45f + 0.28f * (float)i) * (1.0f + 0.3f * HIT * (1.0f - (float)i * 0.25f)),
                  i == 0 ? C_HI : i == 1 ? C_AMB : C_DIM, 0);
    ic_disc(cx, cy, r * 0.22f * (1.0f + HIT), HIT > 0.7f ? C_WHITE : C_HI);
    if (ic->small)
        return;
    const float sx = (float)ic->x + W * 0.7f, sw = W * 0.27f, base = (float)ic->y + H * 0.85f;
    ic_line(sx, base, sx + sw, base, C_LINE);
    float px = sx, py = base - H * 0.7f;
    for (int32_t i = 1; i <= (int32_t)sw; i++) {
        const float u = (float)i / sw, y = base - H * 0.7f * fm_expf(-u * (2.0f + K2 * 10.0f));
        ic_line(px, py, sx + (float)i, y, C_HI);
        px = sx + (float)i;
        py = y;
    }
}

static void i_syn_kick(void)
{
    ic_line(ic->x, CY, ic->x + W, CY, C_LINE);
    ic_wave(f_chirp, CY, H * 0.44f, C_HI);
}

static void i_snare(void)                /* a drum side on: its head jumps, its wires buzz (SNAPPY) */
{
    const float x0 = (float)ic->x + W * 0.15f, x1 = (float)ic->x + W * 0.85f, top = (float)ic->y + H * 0.3f;
    const float bot = (float)ic->y + H * 0.62f, bulge = H * 0.08f * HIT;
    for (int32_t i = 0; i < 8; i++) {    /* the head, bowed by the hit */
        const float u0 = (float)i / 8.0f, u1 = (float)(i + 1) / 8.0f;
        ic_line(x0 + (x1 - x0) * u0, top + bulge * fm_sinf(IC_PI * u0),
                x0 + (x1 - x0) * u1, top + bulge * fm_sinf(IC_PI * u1), HIT > 0.7f ? C_WHITE : C_HI);
    }
    ic_line(x0, top, x0, bot, C_AMB);
    ic_line(x1, top, x1, bot, C_AMB);
    ic_line(x0, bot, x1, bot, C_AMB);
    const float buzz = H * 0.08f * (0.15f + K2) * HIT;
    for (int32_t i = 0; i < (ic->small ? 4 : 12); i++) {   /* the wires */
        const float x = x0 + (x1 - x0) * ((float)i + 0.5f) / (ic->small ? 4.0f : 12.0f);
        const float b = buzz * fm_sinf(T * 50.0f + (float)i);
        ic_line(x - 2.0f, bot + 3.0f + b, x + 2.0f, bot + 3.0f - b, C_HI);
    }
}

static void i_syn_snare(void)            /* an FM drum, its noise bursting (SNAPPY) */
{
    ic_circle(CX, CY, H * 0.24f * (1.0f + 0.25f * HIT), C_HI, 0);
    ic_circle(CX, CY, H * 0.12f, C_AMB, 0);
    const int32_t n = (int32_t)((ic->small ? 10.0f : 50.0f) * (0.2f + K2) * HIT);
    const uint32_t frame = (uint32_t)(T * 25.0f);
    for (int32_t i = 0; i < n; i++) {
        const float a = ic_rnd(frame * 31u + (uint32_t)i) * IC_TAU;
        const float r = H * (0.28f + 0.2f * ic_rnd(frame * 57u + (uint32_t)i));
        ic_pset((int32_t)(CX + fm_cosf(a) * r * 1.3f), (int32_t)(CY + fm_sinf(a) * r), C_GRAY);
    }
}

static void i_hat(void)                  /* six square waves' metal: spokes shimmering at each hit */
{
    const float cx = CX, cy = CY;
    for (int32_t i = 0; i < 8; i++) {
        const float a = (float)i / 8.0f * IC_TAU + T * 0.6f, r1 = H * 0.12f;
        const float r2 = H * (0.2f + 0.25f * HIT * (0.6f + 0.4f * fm_sinf(T * 23.0f + (float)i * 5.0f)));
        ic_line(cx + fm_cosf(a) * r1, cy + fm_sinf(a) * r1, cx + fm_cosf(a) * r2, cy + fm_sinf(a) * r2,
                i & 1 ? C_AMB : C_HI);
    }
    ic_disc(cx, cy, H * 0.08f, HIT > 0.7f ? C_WHITE : C_HI);
    const int32_t m = ic->small ? 0 : (int32_t)(K2 * 30.0f * HIT);
    for (int32_t i = 0; i < m; i++)
        ic_pset((int32_t)(cx + fm_sinf((float)i * 12.9f + T * 40.0f) * H * 0.45f),
                (int32_t)(cy + fm_cosf((float)i * 7.3f + T * 31.0f) * H * 0.4f), C_GRAY);
}

static void i_ring_hat(void)             /* two rings beating against each other */
{
    const float d = W * (0.08f + 0.06f * fm_sinf(T * 9.0f) * (0.3f + HIT)), r = H * 0.26f * (1.0f + 0.15f * HIT);
    ic_circle(CX - d, CY, r, C_HI, 0);
    ic_circle(CX + d, CY, r, C_AMB, 0);
    if (!ic->small)
        for (int32_t i = 0; i < (int32_t)(20.0f * HIT * (0.3f + K2)); i++)
            ic_pset((int32_t)(CX + (ic_rnd((uint32_t)i + (uint32_t)(T * 30.0f) * 13u) - 0.5f) * W * 0.6f),
                    (int32_t)(CY + (ic_rnd((uint32_t)i * 7u + (uint32_t)(T * 30.0f)) - 0.5f) * H * 0.6f), C_WHITE);
}

static void i_vcf(void)                  /* a saw under a low-pass's curve: cutoff (K0), resonance (K2) */
{
    float px = 0.0f, py = 0.0f;
    for (int32_t i = 0; i <= ic->w; i++) {
        const float u = (float)i / W, v = u * 3.0f + T * 0.5f, p = v - fm_floorf(v);
        if (!ic->small)
            ic_pset(ic->x + i, ic->y + (int32_t)(H * (0.55f - 0.25f * (p * 2.0f - 1.0f))), C_LINE);
        const float fc = 0.15f + 0.7f * K0 + 0.04f * fm_sinf(T * 1.5f) * LIVE, d = (u - fc) * 10.0f;
        const float g = 1.0f / (1.0f + fm_expf(d)) + K2 * 0.9f * fm_expf(-d * d * 1.5f);
        const float x = (float)(ic->x + i), y = (float)ic->y + H * (0.9f - 0.62f * g);
        if (i)
            ic_line(px, py, x, y, C_HI);
        px = x;
        py = y;
    }
}

static void i_phase_dist(void)
{
    if (!ic->small)
        ic_wave(f_sine, CY, H * 0.38f, C_LINE);
    ic_wave(f_bent, CY, H * 0.38f * LIVE, C_HI);
}

static void i_six_op(uint32_t bank)      /* six operators in an algorithm: the patch (K2) picks its layout */
{
    static const uint8_t ALGS[3][6][2] = {{{0, 0}, {1, 0}, {2, 0}, {0, 1}, {1, 1}, {2, 1}},
                                          {{1, 0}, {0, 1}, {2, 1}, {0, 2}, {1, 2}, {2, 2}},
                                          {{0, 0}, {0, 1}, {0, 2}, {2, 0}, {2, 1}, {2, 2}}};
    const uint8_t (*l)[2] = ALGS[((int32_t)(K2 * 31.99f) + (int32_t)bank) % 3];
    const float bw = ic->small ? 4.0f : W * 0.2f, bh = ic->small ? 3.0f : H * 0.17f;
    const float gx = (W - 3.0f * bw) / 4.0f, gy = (H - 3.0f * bh) / 4.0f;
    float px[6], py[6];
    for (int32_t i = 0; i < 6; i++) {
        px[i] = (float)ic->x + gx + (float)l[i][0] * (bw + gx);
        py[i] = (float)ic->y + gy + (float)l[i][1] * (bh + gy);
    }
    for (int32_t i = 0; i < 5; i++)
        ic_line(px[i] + bw * 0.5f, py[i] + bh, px[i + 1] + bw * 0.5f, py[i + 1], C_DIM);
    for (int32_t i = 0; i < 6; i++) {
        const float lit = K0 * 0.6f + 0.4f * HIT * (0.5f + 0.5f * fm_sinf(T * 4.0f - (float)i * 0.7f));
        ic_rect(px[i], py[i], bw, bh, lit > 0.55f ? C_HI : lit > 0.3f ? C_AMB : C_DIM);
    }
}

static void i_terrain(void)              /* a terrain's contours, a path across it (radius K0, offset K1) */
{
    for (int32_t j = 0; j < (ic->small ? 2 : 4); j++) {
        float px = 0.0f, py = 0.0f;
        for (int32_t i = 0; i <= ic->w; i += 2) {
            const float u = (float)i / W;
            const float y = (float)ic->y + H * (0.2f + 0.2f * (float)j) +
                            H * 0.06f * fm_sinf(u * 9.0f + (float)j * 1.3f + K2 * 4.0f);
            if (i)
                ic_line(px, py, (float)(ic->x + i), y, C_DIM);
            px = (float)(ic->x + i);
            py = y;
        }
    }
    const float ox = CX + W * 0.2f * (K1 - 0.5f), rx = W * (0.12f + 0.25f * K0), ry = H * (0.12f + 0.25f * K0);
    float px = ox + rx, py = CY;
    for (int32_t i = 1; i <= 24; i++) {
        const float a = IC_TAU * (float)i / 24.0f, x = ox + rx * fm_cosf(a), y = CY + ry * fm_sinf(a);
        ic_line(px, py, x, y, C_AMB);
        px = x;
        py = y;
    }
    ic_disc(ox + rx * fm_cosf(T * 2.0f), CY + ry * fm_sinf(T * 2.0f), 2.0f, HIT > 0.7f ? C_WHITE : C_HI);
}

static void i_string_machine(void)       /* four strings, detuned, shimmering (chorus K0) */
{
    for (int32_t j = 0; j < 4; j++) {
        const float y0 = (float)ic->y + H * (0.2f + 0.2f * (float)j);
        float px = (float)ic->x, py = y0;
        for (int32_t i = 1; i <= ic->w; i++) {
            const float u = (float)i / W;
            const float ph = u * IC_TAU * (2.0f + (float)j * 0.3f) + T * (2.0f + K0 * 3.0f * (float)j);
            const float y = y0 - H * 0.06f * LIVE * fm_sinf(ph);
            ic_line(px, py, (float)(ic->x + i), y, j & 1 ? C_AMB : C_HI);
            px = (float)(ic->x + i);
            py = y;
        }
    }
}

static void i_chiptune(void) { ic_wave(f_chip, CY, H * 0.38f * (0.6f + 0.4f * LIVE), C_HI); }

/* an engine's icon (GRAV_ENGINE_*), drawn into i's box */
static void icon_draw(uint32_t engine, const icon_t *i)
{
    ic = i;
    switch (engine) {
    case GRAV_ENGINE_WAVESHAPING: i_waveshape(); break;
    case GRAV_ENGINE_FM: i_fm(); break;
    case GRAV_ENGINE_VIRTUAL_ANALOG: i_analog(); break;
    case GRAV_ENGINE_MODAL: i_modal(); break;
    case GRAV_ENGINE_GRAIN: i_grain(); break;
    case GRAV_ENGINE_ADDITIVE: i_additive(); break;
    case GRAV_ENGINE_WAVETABLE: i_wavetable(); break;
    case GRAV_ENGINE_CHORDS: i_chords(); break;
    case GRAV_ENGINE_SPEECH: i_speech(); break;
    case GRAV_ENGINE_SWARM: i_swarm(); break;
    case GRAV_ENGINE_NOISE: i_noise(); break;
    case GRAV_ENGINE_PARTICLE: i_particle(); break;
    case GRAV_ENGINE_STRING: i_string(); break;
    case GRAV_ENGINE_BASS_DRUM: i_kick(); break;
    case GRAV_ENGINE_SNARE_DRUM: i_snare(); break;
    case GRAV_ENGINE_HI_HAT: i_hat(); break;
    case GRAV_ENGINE_VA_VCF: i_vcf(); break;
    case GRAV_ENGINE_PHASE_DISTORTION: i_phase_dist(); break;
    case GRAV_ENGINE_SIX_OP_1: i_six_op(0); break;
    case GRAV_ENGINE_SIX_OP_2: i_six_op(1); break;
    case GRAV_ENGINE_SIX_OP_3: i_six_op(2); break;
    case GRAV_ENGINE_WAVE_TERRAIN: i_terrain(); break;
    case GRAV_ENGINE_STRING_MACHINE: i_string_machine(); break;
    case GRAV_ENGINE_CHIPTUNE: i_chiptune(); break;
    case GRAV_ENGINE_SYN_KICK: i_syn_kick(); break;
    case GRAV_ENGINE_SYN_SNARE: i_syn_snare(); break;
    case GRAV_ENGINE_RING_HAT: i_ring_hat(); break;
    default: break;
    }
}

/* an engine in a line: on its splash, beside its icon on its sound page, and for a screen reader */
static const char *engine_desc(uint32_t engine)
{
    static const char *const D[GRAV_ENGINE_ALL] = {
        "A sine through a wavefolder: fold, asymmetry, curve",
        "Two operators: one bends the other's phase",
        "Two analog waves, a pulse and a saw, detuned",
        "A struck body ringing: plates, bells, bars",
        "Grains of sine shaped by two formants",
        "Harmonics added up in bumps: where, how wide",
        "A grid of waves: its row and its column",
        "Four voices, one chord: its type, its inversion",
        "Formants, vowels and words",
        "Eight sawtooth swarms drifting apart",
        "Clocked noise through a resonant filter",
        "Dust through resonant filters: rain, sparks",
        "A plucked string: excitation, decay, material",
        "An analog kick: a pulse into a resonant filter",
        "An analog snare: the drum's modes and its wires",
        "Six square waves through a band-pass",
        "A virtual analog voice through a filter",
        "A sine bent by phase distortion",
        "Six operators: DX7 basses and synths",
        "Six operators: DX7 keys, plucks, percussion",
        "Six operators: DX7 organs, pads, strings",
        "A path across a wave terrain",
        "A string machine: chorused, filtered chords",
        "Square waves and arpeggios, 8-bit",
        "A digital kick: a sine with a pitch envelope",
        "A digital snare: an FM drum and noise",
        "Ring-modulated noise: a metallic hi-hat",
    };
    return D[engine < GRAV_ENGINE_ALL ? engine : 0];
}

#undef CX
#undef CY
#undef W
#undef H
#undef K0
#undef K1
#undef K2
#undef T
#undef HIT
#undef LIVE
