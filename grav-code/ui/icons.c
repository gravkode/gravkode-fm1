/* SPDX-License-Identifier: MPL-2.0 */
/* GRAV CODE's engine icons: each engine drawn by code, alive (0.20; redrawn in 0.28 as neon pictures, each engine in
 * its own colour). An icon is drawn into a box of the canvas (device/gfx.c, in scope: the app is part of the unity
 * build), clipped to it, from the time, the engine's three knobs (a drum's TONE, DECAY and SHAPE's first) and a
 * note's strike, so that turning a knob or playing a note shows on it: FOLD folds WAVESHAPE's ribbons further, INDEX
 * bends FM's loops, a kick's rings jump at each hit. Light is added, not painted (the box is black under it): lines
 * crossing grow brighter, towards white, as neon does; a big icon's lines glow round them. Small (the tracks' list,
 * the presets' browser): fewer details, no glow. A few hundred bytes of code an icon, no bitmaps. Included by app.c. */

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
static uint16_t ic_halo;                 /* the glow added round each pixel of a line (0: none) */

static uint16_t ic_dim(uint16_t c, int32_t t)   /* c at t/256 */
{
    return (uint16_t)(((uint32_t)((c >> 11) * t >> 8) << 11) | ((uint32_t)(((c >> 5) & 63) * t >> 8) << 5) |
                      (uint32_t)((c & 31) * t >> 8));
}

static uint16_t ic_mix(uint16_t a, uint16_t b, float u)   /* a to b, u 0..1 */
{
    const int32_t t = (int32_t)(fm_clampf(u, 0.0f, 1.0f) * 256.0f);
    const int32_t ar = a >> 11, ag = (a >> 5) & 63, ab = a & 31, br = b >> 11, bg = (b >> 5) & 63, bb = b & 31;
    return (uint16_t)(((ar + (br - ar) * t / 256) << 11) | ((ag + (bg - ag) * t / 256) << 5) | (ab + (bb - ab) * t / 256));
}

/* light added to the pixel (clipped to the box): saturating, so that crossings brighten */
static void ic_add(int32_t x, int32_t y, uint16_t c)
{
    if (x < ic->x || x >= ic->x + ic->w || y < ic->y || y >= ic->y + ic->h)
        return;
    const int32_t yy = y + cv_oy;
    if ((uint32_t)x >= cv_w || yy < cv_y0 || yy >= cv_y1)
        return;
    uint16_t *p = &cv_px[(uint32_t)yy * cv_w + (uint32_t)x];
    const uint32_t a = swap16(*p), r = (a >> 11) + (uint32_t)(c >> 11), g = ((a >> 5) & 63u) + ((c >> 5) & 63u),
                   b = (a & 31u) + (c & 31u);
    *p = swap16((r > 31u ? 31u : r) << 11 | (g > 63u ? 63u : g) << 5 | (b > 31u ? 31u : b));
}

static void ic_lit(int32_t x, int32_t y, uint16_t c)   /* a pixel of a line, and its glow */
{
    ic_add(x, y, c);
    if (ic_halo) {
        ic_add(x - 1, y, ic_halo);
        ic_add(x + 1, y, ic_halo);
        ic_add(x, y - 1, ic_halo);
        ic_add(x, y + 1, ic_halo);
    }
}

static void ic_glow(uint16_t c, int32_t t)   /* the lines that follow glow (big icons alone), t/256 of c */
{
    ic_halo = ic->small || ic->h < 30 ? 0 : ic_dim(c, t);
}

static int32_t ic_round(float v) { return (int32_t)(v < 0.0f ? v - 0.5f : v + 0.5f); }

static float ic_penx, ic_peny;           /* a path's pen */
static int ic_pen_new;

/* a line to (x, y) from the pen, its first pixel left out but for a path's first (light is added: each pixel once) */
static void ic_to(float fx, float fy, uint16_t c)
{
    int32_t x0 = ic_round(ic_penx), y0 = ic_round(ic_peny);
    const int32_t x1 = ic_round(fx), y1 = ic_round(fy);
    const int32_t dx = x1 > x0 ? x1 - x0 : x0 - x1, sx = x0 < x1 ? 1 : -1;
    const int32_t dy = y1 > y0 ? y0 - y1 : y1 - y0, sy = y0 < y1 ? 1 : -1;
    int32_t err = dx + dy, guard = 600, first = ic_pen_new;
    ic_penx = fx;
    ic_peny = fy;
    ic_pen_new = 0;
    while (guard--) {
        const int32_t e2 = 2 * err;
        if (first)
            ic_lit(x0, y0, c);
        first = 1;
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

static void ic_move(float x, float y)
{
    ic_penx = x;
    ic_peny = y;
    ic_pen_new = 1;
}

static void ic_line(float x0, float y0, float x1, float y1, uint16_t c)
{
    ic_move(x0, y0);
    ic_to(x1, y1, c);
}

static void ic_disc(float cx, float cy, float r, uint16_t c)
{
    for (int32_t j = ic_round(cy - r); j <= ic_round(cy + r); j++)
        for (int32_t i = ic_round(cx - r); i <= ic_round(cx + r); i++) {
            const float dx = (float)i - cx, dy = (float)j - cy;
            if (dx * dx + dy * dy <= r * r + 0.5f)
                ic_add(i, j, c);
        }
}

/* an ellipse about (cx, cy), radii rx and ry, turned by a radians; dotted: every other stretch */
static void ic_ellipse(float cx, float cy, float rx, float ry, float a, uint16_t c, int dotted)
{
    const int32_t n = (int32_t)((rx + ry) * 3.2f) + 8;
    const float ca = fm_cosf(a), sa = fm_sinf(a);
    for (int32_t i = 0; i <= n; i++) {
        const float t = IC_TAU * (float)i / (float)n, ex = rx * fm_cosf(t), ey = ry * fm_sinf(t);
        const float x = cx + ex * ca - ey * sa, y = cy + ex * sa + ey * ca;
        if (i == 0 || (dotted && (i & 2)))
            ic_move(x, y);
        else
            ic_to(x, y, c);
    }
}

/* a wave across the box: f(u) in -1..1 for u 0..1, drawn about row cy, amp pixels up and down */
static void ic_wave(float (*f)(float u), float cy, float amp, uint16_t c)
{
    ic_move((float)ic->x, cy - f(0.0f) * amp);
    for (int32_t i = 1; i <= ic->w; i++)
        ic_to((float)(ic->x + i), cy - f((float)i / (float)ic->w) * amp, c);
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

static float ic_gauss(uint32_t i)        /* -1..1, gathered towards 0 */
{
    return (ic_rnd(i) + ic_rnd(i + 0x9E37u) + ic_rnd(i + 0x7F4Au)) * (2.0f / 3.0f) - 1.0f;
}

#define CX ((float)ic->x + (float)ic->w * 0.5f)
#define CY ((float)ic->y + (float)ic->h * 0.5f)
#define W ((float)ic->w)
#define H ((float)ic->h)
#define X0 ((float)ic->x)
#define Y0 ((float)ic->y)
#define K0 (ic->k[0])
#define K1 (ic->k[1])
#define K2 (ic->k[2])
#define T (ic->t)
#define HIT (ic->hit)
#define LIVE (0.45f + 0.55f * HIT)       /* a wave's amplitude: still, a little less; playing, full */
#define SMALL (ic->small || ic->h < 16)

/* each engine's colour, as on the engines' poster (GRAV_ENGINE_*) */
static const uint16_t ENGINE_HUE[GRAV_ENGINE_ALL] = {
    RGB(200, 90, 255),                   /* WAVESHAPE: violet */
    RGB(50, 170, 255),                   /* FM 2-OP: blue */
    RGB(255, 150, 40),                   /* ANALOG: orange */
    RGB(60, 175, 255),                   /* MODAL: sky blue */
    RGB(40, 220, 200),                   /* GRAIN: teal */
    RGB(255, 150, 40),                   /* ADDITIVE: orange to pink */
    RGB(170, 80, 255),                   /* WAVETABLE: purple */
    RGB(190, 80, 255),                   /* CHORDS: purple */
    RGB(60, 140, 255),                   /* SPEECH: blue */
    RGB(255, 140, 30),                   /* SWARM: orange */
    RGB(190, 215, 255),                  /* NOISE: white blue */
    RGB(230, 70, 230),                   /* PARTICLE: magenta */
    RGB(255, 90, 40),                    /* STRING: red orange */
    RGB(255, 50, 40),                    /* KICK: red */
    RGB(90, 150, 255),                   /* SNARE: blue white */
    RGB(255, 200, 40),                   /* HI-HAT: gold */
    RGB(60, 230, 110),                   /* VA VCF: green */
    RGB(225, 70, 240),                   /* PHASE DIST: magenta */
    RGB(50, 110, 255),                   /* 6-OP FM 1: blue */
    RGB(40, 210, 200),                   /* 6-OP FM 2: teal */
    RGB(150, 80, 255),                   /* 6-OP FM 3: purple */
    RGB(40, 210, 140),                   /* TERRAIN: green */
    RGB(255, 160, 30),                   /* STRING MCH: gold */
    RGB(40, 130, 255),                   /* CHIPTUNE: blue */
    RGB(255, 70, 50),                    /* SYN KICK: red */
    RGB(90, 150, 255),                   /* SYN SNARE: blue */
    RGB(255, 200, 40),                   /* RING HAT: gold */
};
static uint16_t ic_hue;                     /* the engine being drawn's */

/* ------------------------------------------------------------- the waves --- */
static float f_fold(float u)             /* WAVESHAPE: a sine through a folder */
{
    const float s = fm_sinf(u * IC_TAU * 1.2f + T * 1.6f) * LIVE;
    return fm_sinf((s * (1.0f + K0 * 5.0f) + (K1 - 0.5f) * 1.4f) * (0.8f + K2 * 0.6f));
}

static float f_analog(float u)           /* ANALOG: a saw and a pulse in turn, its width K0, the saw's slope K1 */
{
    const float v = u * 3.0f + T * 0.4f, p = v - fm_floorf(v), half = v * 0.5f - fm_floorf(v * 0.5f);
    if (half < 0.5f)                     /* the pulse */
        return p < 0.15f + 0.7f * K0 ? 0.9f : -0.9f;
    const float a = 0.55f + 0.44f * K1;  /* the saw, rising to a drop */
    return p < a ? p / a * 1.8f - 0.9f : 0.9f - (p - a) / (1.0f - a) * 1.8f;
}

static float f_chirp(float u)            /* SYN KICK: a sine, its pitch falling (PUNCH the depth) */
{
    const float sweep = (1.0f + 6.0f * K2) * (1.0f - fm_expf(-u * 6.0f)) / 6.0f;
    const float env = fm_expf(-u * (1.0f + 4.0f * (1.0f - K1))) * (0.3f + 0.7f * HIT);
    return fm_sinf(IC_TAU * (1.5f * u + sweep * 6.0f)) * env;
}

/* ------------------------------------------------------------- the icons --- */
static void i_analog(void)               /* a square and saw wave, neon; detuned (K2): a second one drifting by */
{
    const float amp = H * 0.36f * LIVE;
    ic_glow(ic_hue, 60);
    ic_wave(f_analog, CY, amp, ic_hue);
    if (!SMALL && K2 > 0.05f) {
        ic_glow(ic_hue, 0);
        const float shift = K2 * 0.08f * fm_sinf(T * 1.3f);
        ic_move(X0, CY - f_analog(shift) * amp);
        for (int32_t i = 1; i <= ic->w; i++)
            ic_to(X0 + (float)i, CY - f_analog((float)i / W + shift) * amp, ic_dim(ic_hue, 90));
    }
}

static void i_waveshape(void)            /* a ribbon of folded waves */
{
    const int32_t n = SMALL ? 2 : 5;
    for (int32_t j = 0; j < n; j++) {
        const float sc = 1.0f - 0.07f * (float)j, ph = (float)j * 0.035f;
        ic_glow(ic_hue, j == 0 ? 50 : 0);
        ic_move(X0, CY - f_fold(ph) * H * 0.4f * sc);
        for (int32_t i = 1; i <= ic->w; i++)
            ic_to(X0 + (float)i, CY - f_fold((float)i / W + ph) * H * 0.4f * sc, ic_dim(ic_hue, j == 0 ? 256 : 130 - 18 * j));
    }
}

static void i_fm(void)                   /* lens-shaped loops: as many as the ratio (K2), bent by the index (K0) */
{
    const int32_t n = SMALL ? 2 : 5;
    const float lobes = 2.0f + (float)(int32_t)(K2 * 3.0f), fb = K1 * 0.35f;
    for (int32_t j = 0; j < n; j++) {
        const float a = H * 0.42f * (1.0f - 0.16f * (float)j) * (0.75f + 0.25f * LIVE);
        const uint16_t c = ic_dim(ic_hue, j == 0 ? 256 : 150 - 20 * j);
        ic_glow(ic_hue, j == 0 ? 55 : 0);
        for (int32_t side = -1; side <= 1; side += 2) {
            for (int32_t i = 0; i <= ic->w; i++) {
                const float u = (float)i / W;
                const float ph = IC_PI * lobes * u + K0 * LIVE * fm_sinf(IC_TAU * lobes * u + T * 2.0f) * 0.6f;
                const float s = fm_sinf(ph), y = CY + (float)side * a * (s + fb * s * s);
                if (i == 0)
                    ic_move(X0, y);
                else
                    ic_to(X0 + (float)i, y, c);
            }
        }
    }
}

static void i_grain(void)                /* grains, a cloud of dots along a wave: its formant (K0), its width (K1) */
{
    const int32_t n = SMALL ? 14 : 60 + (int32_t)W;
    const float spread = H * (0.1f + 0.24f * K1);
    if (!SMALL) {                        /* the path, faint */
        ic_glow(ic_hue, 0);
        ic_move(X0, CY - H * 0.26f * fm_sinf(T * 0.7f) * LIVE);
        for (int32_t i = 1; i <= ic->w; i++)
            ic_to(X0 + (float)i, CY - H * 0.26f * fm_sinf(IC_TAU * (1.0f + K0) * (float)i / W + T * 0.7f) * LIVE,
                  ic_dim(ic_hue, 50));
    }
    for (int32_t i = 0; i < n; i++) {
        float u = ic_rnd((uint32_t)i * 5u) + T * 0.04f;
        u -= fm_floorf(u);
        const float path = CY - H * 0.26f * fm_sinf(IC_TAU * (1.0f + K0) * u + T * 0.7f) * LIVE;
        const float y = path + ic_gauss((uint32_t)i * 5u + 1u) * spread;
        const float tw = 0.5f + 0.5f * fm_sinf(T * 5.0f + (float)i * 1.7f);
        const uint16_t c = ic_dim(ic_hue, 130 + (int32_t)(126.0f * tw));
        ic_add(ic_round(X0 + u * W), ic_round(y), c);
        if (!SMALL && (i % 3) == 0)
            ic_add(ic_round(X0 + u * W) + 1, ic_round(y), ic_dim(c, 160));
    }
}

static void i_additive(void)             /* the harmonics as lines, orange to pink: where (K0), how wide (K1), how many
                                          * bumps (K2) */
{
    const uint16_t pink = RGB(255, 60, 190);
    const int32_t n = SMALL ? 8 : ic->w / 3;
    const float bw = W / (float)n, centre = K0 * (float)(n - 1), width = 1.0f + K1 * 4.0f, bumps = 1.0f + K2 * 3.0f;
    ic_glow(ic_hue, 0);
    for (int32_t i = 0; i < n; i++) {
        const float d = ((float)i - centre) * bumps / (float)n * 3.0f, rep = d - fm_floorf(d + 0.5f);
        const float a = fm_expf(-rep * rep * 30.0f / width) * (0.75f + 0.25f * fm_sinf(T * 3.0f + (float)i));
        const float bh = H * (0.08f + 0.85f * a * LIVE), x = X0 + bw * ((float)i + 0.5f), base = Y0 + H - 1.0f;
        const uint16_t c = ic_mix(ic_hue, pink, (float)i / (float)n);
        ic_halo = SMALL || ic->h < 30 ? 0 : ic_dim(c, 45);
        ic_line(x, base, x, base - bh, c);
        ic_add(ic_round(x), ic_round(base - bh) - 1, c);
    }
}

static void i_wavetable(void)            /* sheets of waves in perspective: the row (K0) lit, the shape (K1), the
                                          * bank (K2) */
{
    const int32_t rows = SMALL ? 2 : 4;
    const float dx = W * 0.14f, dy = H * 0.14f, w = W - dx - 2.0f;
    for (int32_t j = rows - 1; j >= 0; j--) {
        const float y0 = Y0 + H * (0.38f + 0.5f * (float)j / (float)(rows > 1 ? rows - 1 : 1)) - H * 0.12f;
        const int lit = (int32_t)(K0 * (float)(rows - 1) + 0.5f) == j;
        const uint16_t c = lit ? ic_hue : ic_dim(ic_hue, 160);
        float fy[2][48];
        const int32_t m = w > 46.0f ? 46 : (int32_t)w;
        for (int32_t i = 0; i <= m; i++)
            for (int32_t b = 0; b < 2; b++) {
                const float u = (float)i / (float)m, mm = fm_minf(1.0f, K1 + (float)j * 0.12f);
                const float s = fm_sinf(u * IC_TAU * (1.5f + K2) + T * (lit ? 1.6f : 0.4f) + (float)b * 0.9f);
                const float v = s * (1.0f - mm) + (s > 0.0f ? 1.0f : -1.0f) * mm;
                fy[b][i] = y0 - (float)b * dy - v * H * 0.1f * (lit ? LIVE * 1.3f : 0.8f);
            }
        ic_glow(ic_hue, lit ? 50 : 0);
        for (int32_t b = 0; b < 2; b++) {
            ic_move(X0 + 1.0f + (float)b * dx, fy[b][0]);
            for (int32_t i = 1; i <= m; i++)
                ic_to(X0 + 1.0f + (float)b * dx + w * (float)i / (float)m, fy[b][i], b ? ic_dim(c, 150) : c);
        }
        if (!SMALL)
            for (int32_t i = 0; i <= m; i += m / 6 > 0 ? m / 6 : 1)   /* the sheet's mesh */
                ic_line(X0 + 1.0f + w * (float)i / (float)m, fy[0][i], X0 + 1.0f + dx + w * (float)i / (float)m,
                        fy[1][i], ic_dim(c, 90));
    }
}

static void i_chords(void)               /* the chord's notes as stacked planes: how many (its type, K2), the
                                          * inversion (K0) the brightest, spreading as it plays */
{
    static const uint8_t NOTES[5] = {4, 4, 3, 4, 3};
    const int32_t n = SMALL ? 3 : NOTES[(int32_t)fm_minf(4.0f, K2 * 5.0f)];
    const int32_t top = (int32_t)(K0 * 3.99f) % n;
    const float rx = fm_minf(W * 0.3f, H * 0.7f), ry = rx * 0.42f;
    const float gap = H * (0.13f + 0.04f * HIT * fm_sinf(T * 9.0f) + 0.05f * K1);
    const float y0 = CY + gap * (float)(n - 1) * 0.5f;
    for (int32_t i = 0; i < n; i++) {
        const float y = y0 - gap * (float)i, cx = CX;
        const uint16_t c = i == top ? ic_hue : ic_dim(ic_hue, 150);
        ic_glow(ic_hue, i == top ? 60 : 30);
        ic_move(cx - rx, y);
        ic_to(cx, y - ry, c);
        ic_to(cx + rx, y, c);
        ic_to(cx, y + ry, c);
        ic_to(cx - rx, y, c);
        ic_halo = 0;
        if (!SMALL)                      /* its face, faint */
            for (float k = -0.6f; k <= 0.61f; k += 0.3f)
                ic_line(cx - rx * (1.0f - fm_fabsf(k)) + 1.0f, y + ry * k, cx + rx * (1.0f - fm_fabsf(k)) - 1.0f,
                        y + ry * k, ic_dim(ic_hue, 40));
    }
}

static void i_speech(void)               /* a face of dots in profile, speaking (its jaw opening on the words), the
                                          * words' wave beside it */
{
    static const int8_t FACE[][2] = {    /* the profile, facing right, brow to chin: x, y in 1/34 of the height */
        {-2, -15}, {2, -14}, {4, -11}, {5, -8}, {5, -6}, {7, -3}, {9, 0}, {7, 1}, {6, 3}, {7, 4}, {6, 6},
        {6, 7}, {6, 9}, {4, 12}, {0, 14}};
    const int32_t nf = (int32_t)(sizeof FACE / sizeof FACE[0]);
    const float s = H / 34.0f, fx = X0 + (SMALL ? W * 0.3f : W * 0.3f);
    const float open = (0.3f + 1.4f * fm_fabsf(fm_sinf(T * 7.0f + K1 * 3.0f)) * LIVE) * s;
    float px[16], py[16];
    for (int32_t i = 0; i < nf; i++) {   /* (below the lips: the jaw, opening) */
        px[i] = fx + (float)FACE[i][0] * s;
        py[i] = CY + (float)FACE[i][1] * s + (FACE[i][1] > 5 ? open : 0.0f);
    }
    if (!SMALL) {                        /* the head behind the profile: a scatter of dots */
        for (int32_t i = 0; i < 90; i++) {
            const float v = ic_rnd((uint32_t)i * 3u), y = CY + (v * 29.0f - 15.0f) * s;
            int32_t k = 0;
            while (k + 2 < nf && py[k + 1] < y)
                k++;
            const float back = fx - 11.0f * s * fm_sqrtf(fm_maxf(0.0f, 1.0f - ((v - 0.45f) * 2.0f) * ((v - 0.45f) * 2.0f)));
            const float x = back + (px[k] - back) * ic_rnd((uint32_t)i * 3u + 1u);
            ic_add(ic_round(x), ic_round(y), ic_dim(ic_hue, 70 + (int32_t)(150.0f * (x - back) / fm_maxf(1.0f, px[k] - back))));
        }
    }
    ic_glow(ic_hue, 60);                 /* the profile itself, dotted, bright */
    for (int32_t i = 0; i + 1 < nf; i++) {
        const float d = fm_maxf(fm_fabsf(px[i + 1] - px[i]), fm_fabsf(py[i + 1] - py[i]));
        const int32_t m = SMALL ? 1 : (int32_t)(d / 2.0f) + 1;
        for (int32_t k = 0; k < m; k++) {
            const float u = (float)k / (float)m;
            ic_lit(ic_round(px[i] + (px[i + 1] - px[i]) * u), ic_round(py[i] + (py[i + 1] - py[i]) * u), ic_hue);
        }
    }
    const float wx = X0 + W * (SMALL ? 0.5f : 0.48f), ww = W - (wx - X0) - 1.0f;   /* the words: bursts of lines */
    const uint16_t cyan = RGB(70, 200, 255);
    ic_glow(cyan, 40);
    for (int32_t i = 0; i < (int32_t)ww; i += SMALL ? 2 : 1) {
        const float u = (float)i / ww, syl = fm_fabsf(fm_sinf(IC_PI * (u * (2.0f + K0 * 2.0f) - T * 1.5f)));
        const float a = H * 0.42f * syl * (0.35f + 0.65f * ic_rnd((uint32_t)i + (uint32_t)(T * 20.0f) * 101u)) * LIVE;
        ic_line(wx + (float)i, CY - a, wx + (float)i, CY + a, ic_dim(cyan, 120 + (int32_t)(136.0f * syl)));
    }
}

static void i_swarm(void)                /* a spiral of dots turning, the inner faster: how many (K0), how loose (K2) */
{
    const int32_t n = SMALL ? 14 : 80 + (int32_t)(K0 * 100.0f);
    const float R = fm_minf(W * 0.48f, H * 0.95f);
    if (!SMALL) {
        ic_glow(ic_hue, 0);
        ic_ellipse(CX, CY, R * 0.95f, R * 0.32f, -0.25f, ic_dim(ic_hue, 60), 1);
        ic_ellipse(CX, CY, R * 0.6f, R * 0.2f, -0.25f, ic_dim(ic_hue, 70), 1);
    }
    for (int32_t i = 0; i < n; i++) {
        const float r = fm_sqrtf(ic_rnd((uint32_t)i * 7u)) * (1.0f + 0.15f * HIT), arm = (float)(i & 1) * IC_PI;
        const float a = arm + r * 4.0f + T * (0.3f + 0.9f * (1.0f - r)) + ic_gauss((uint32_t)i * 7u + 3u) * (0.3f + K2);
        const float x = CX + fm_cosf(a) * r * R, y = CY + fm_sinf(a) * r * R * 0.36f;
        const uint16_t c = ic_dim(ic_hue, 256 - (int32_t)(100.0f * r));
        ic_add(ic_round(x), ic_round(y), c);
        if (!SMALL && r < 0.35f)
            ic_add(ic_round(x) + 1, ic_round(y), ic_dim(c, 140));
    }
    ic_disc(CX, CY, SMALL ? 1.0f : 2.0f + HIT, ic_dim(RGB(255, 230, 180), 200));
}

static void i_noise(void)                /* a cloud of white noise, new each frame: its spread (K0, the cutoff),
                                          * a ring in it (K1, the resonance) */
{
    const uint32_t frame = (uint32_t)(T * 20.0f);
    const int32_t n = SMALL ? 18 : 120 + (int32_t)(W * 2.0f);
    const float rx = W * (0.3f + 0.4f * K0) * (0.85f + 0.15f * LIVE), ry = H * (0.42f + 0.3f * K0);
    for (int32_t i = 0; i < n; i++) {
        const uint32_t s = frame * 977u + (uint32_t)i * 3u;
        float gx = ic_gauss(s), gy = ic_gauss(s + 1u);
        if (K1 > 0.5f && (i & 3) == 0) { /* resonance: some on a ring */
            const float a = ic_rnd(s + 2u) * IC_TAU;
            gx = fm_cosf(a) * 0.8f;
            gy = fm_sinf(a) * 0.8f;
        }
        const float lum = 1.0f - 0.6f * fm_fabsf(gx) - 0.3f * fm_fabsf(gy);
        ic_add(ic_round(CX + gx * rx), ic_round(CY + gy * ry), ic_dim(ic_hue, (int32_t)(110.0f + 146.0f * lum * ic_rnd(s + 5u))));
    }
}

static void i_particle(void)             /* orbs on tilted orbits, sparkling: how many (K0), a flash at each note */
{
    const int32_t orbs = SMALL ? 3 : 4 + (int32_t)(K0 * 6.0f);
    const float R = fm_minf(W * 0.46f, H * 0.95f);
    if (!SMALL) {
        ic_glow(ic_hue, 0);
        ic_ellipse(CX, CY, R, R * 0.34f, 0.35f, ic_dim(ic_hue, 110), 0);
        ic_ellipse(CX, CY, R * 0.7f, R * 0.42f, -0.5f, ic_dim(ic_hue, 90), 0);
    }
    for (int32_t i = 0; i < orbs; i++) {
        const float a = T * (0.6f + 0.25f * (float)(i % 3)) + (float)i * 2.1f, tilt = i & 1 ? -0.5f : 0.35f;
        const float rx = (i & 1 ? 0.7f : 1.0f) * R, ry = (i & 1 ? 0.42f : 0.34f) * R;
        const float ex = rx * fm_cosf(a), ey = ry * fm_sinf(a);
        const float x = CX + ex * fm_cosf(tilt) - ey * fm_sinf(tilt), y = CY + ex * fm_sinf(tilt) + ey * fm_cosf(tilt);
        const float size = SMALL ? 0.6f : 1.2f + 1.2f * ic_rnd((uint32_t)i) + 1.5f * HIT;
        ic_disc(x, y, size + 1.0f, ic_dim(ic_hue, 70));
        ic_disc(x, y, size, ic_mix(ic_hue, C_WHITE, 0.25f + 0.5f * HIT));
    }
    if (!SMALL)
        for (int32_t i = 0; i < 16; i++) {   /* sparks */
            const uint32_t s = (uint32_t)i * 13u + (uint32_t)(T * 8.0f) * 131u;
            ic_add(ic_round(CX + ic_gauss(s) * R), ic_round(CY + ic_gauss(s + 1u) * R * 0.5f), ic_dim(ic_hue, 160));
        }
}

static void i_string(void)               /* a ribbon of strings swaying, ringing after each pluck: the material (K2)
                                          * a shimmer on it */
{
    const int32_t n = SMALL ? 2 : 6;
    const float a = H * (0.12f + 0.24f * HIT);
    for (int32_t j = 0; j < n; j++) {
        const float off = ((float)j - (float)(n - 1) * 0.5f) * (SMALL ? 2.0f : 1.6f);
        ic_glow(ic_hue, j == n / 2 ? 55 : 0);
        for (int32_t i = 0; i <= ic->w; i++) {
            const float u = (float)i / W;
            const float y = CY + off + a * fm_sinf(IC_PI * u * 1.6f + T * 1.6f + (float)j * 0.18f) +
                            a * 0.3f * K2 * fm_sinf(5.0f * IC_PI * u + T * 9.0f);
            if (i == 0)
                ic_move(X0, y);
            else
                ic_to(X0 + (float)i, y, ic_dim(ic_hue, j == n / 2 ? 256 : 120 + 15 * j));
        }
    }
}

static void i_modal(void)                /* a struck dome, its ripples spreading round it as it rings */
{
    const float R = fm_minf(W * 0.3f, H * 0.62f), base = CY + H * 0.18f;
    if (!SMALL)
        for (int32_t i = 0; i < 4; i++) {   /* the ripples, travelling out */
            float s = T * (0.25f + 0.25f * K1) + (float)i * 0.25f + HIT * 0.2f;
            s -= fm_floorf(s);
            ic_glow(ic_hue, 0);
            ic_ellipse(CX, base, R * (0.9f + 0.9f * s), R * (0.9f + 0.9f * s) * 0.24f, 0.0f,
                       ic_dim(ic_hue, (int32_t)(220.0f * (1.0f - s))), 0);
        }
    const float d = R * (0.7f + 0.08f * HIT * fm_sinf(T * 30.0f));   /* the dome, its latitudes */
    ic_glow(ic_hue, 60);
    for (int32_t k = 0; k <= (SMALL ? 1 : 3); k++) {
        const float lat = (float)k / 4.0f, rr = d * fm_sqrtf(1.0f - lat * lat), yy = base - d * 1.1f * lat;
        ic_move(CX - rr, yy);
        for (int32_t i = 1; i <= 16; i++) {
            const float t = IC_PI * (float)i / 16.0f;
            ic_to(CX - rr * fm_cosf(t), yy - rr * 0.22f * fm_sinf(t) * (k == 0 ? 0.0f : 1.0f), k == 0 ? ic_hue : ic_dim(ic_hue, 170));
        }
    }
    ic_move(CX - d, base);                   /* its outline */
    for (int32_t i = 1; i <= 20; i++) {
        const float t = IC_PI * (float)i / 20.0f;
        ic_to(CX - d * fm_cosf(t), base - d * 1.1f * fm_sinf(t), HIT > 0.7f ? ic_mix(ic_hue, C_WHITE, 0.5f) : ic_hue);
    }
}

static void i_kick(void)                 /* rings round a glowing core, jumping out at each hit: the punch (K2)
                                          * spreads them, the decay (K1) keeps them lit */
{
    const float R = fm_minf(W * 0.48f, H * 0.48f);
    const int32_t n = SMALL ? 2 : 4;
    for (int32_t i = n - 1; i >= 0; i--) {
        const float r = R * (0.3f + (0.7f / (float)n) * (float)(i + 1) * (0.85f + 0.3f * K2)) *
                        (1.0f + 0.25f * HIT * (1.0f - (float)i * 0.15f));
        ic_glow(ic_hue, 60);
        ic_ellipse(CX, CY, r, r, 0.0f, ic_dim(ic_hue, (int32_t)(256.0f - (float)i * (70.0f - 40.0f * K1))), 0);
    }
    ic_disc(CX, CY, R * 0.22f * (1.0f + 0.5f * HIT) + 1.0f, ic_dim(ic_hue, 120));
    ic_disc(CX, CY, R * 0.16f * (1.0f + 0.5f * HIT), ic_mix(ic_hue, C_WHITE, 0.2f + 0.5f * HIT));
}

static void i_syn_kick(void)             /* SYN KICK: its sweep, a sine falling in pitch, and its core */
{
    ic_glow(ic_hue, 60);
    ic_wave(f_chirp, CY, H * 0.44f, ic_hue);
    if (!SMALL)
        ic_disc(X0 + 4.0f, CY, 2.0f + 2.0f * HIT, ic_mix(ic_hue, C_WHITE, 0.4f));
}

static void i_snare(void)                /* a starburst at each hit: its rays (SNAPPY: more, sparks at their ends) */
{
    const float R = fm_minf(W * 0.48f, H * 0.5f);
    const int32_t n = SMALL ? 8 : 28 + (int32_t)(K2 * 20.0f);
    const uint32_t frame = (uint32_t)(T * 12.0f);
    for (int32_t i = 0; i < n; i++) {
        const float a = IC_TAU * ((float)i + 0.3f * ic_rnd(frame * 7u + (uint32_t)i)) / (float)n;
        const float len = R * (0.25f + 0.75f * ic_rnd((uint32_t)i * 11u + frame)) * (SMALL ? 0.7f + 0.3f * HIT : 0.35f + 0.65f * HIT);
        ic_glow(ic_hue, 0);
        ic_line(CX, CY, CX + fm_cosf(a) * len * (W > H ? 1.25f : 1.0f), CY + fm_sinf(a) * len,
                ic_dim(ic_hue, 140 + (int32_t)(116.0f * ic_rnd((uint32_t)i))));
        if (!SMALL && K2 > 0.3f && (i & 3) == 0)
            ic_add(ic_round(CX + fm_cosf(a) * len * 1.35f), ic_round(CY + fm_sinf(a) * len * 1.1f), C_WHITE);
    }
    ic_disc(CX, CY, SMALL ? 1.0f : 2.0f + 2.0f * HIT, ic_mix(ic_hue, C_WHITE, 0.6f));
}

static void i_syn_snare(void)            /* SYN SNARE: an FM drum's ring, its noise bursting round it (SNAPPY) */
{
    const float R = fm_minf(W * 0.3f, H * 0.32f) * (1.0f + 0.25f * HIT);
    ic_glow(ic_hue, 60);
    ic_ellipse(CX, CY, R, R, 0.0f, ic_hue, 0);
    ic_ellipse(CX, CY, R * 0.5f, R * 0.5f, 0.0f, ic_dim(ic_hue, 160), 0);
    const int32_t n = (int32_t)((SMALL ? 10.0f : 60.0f) * (0.2f + K2) * (0.2f + HIT));
    const uint32_t frame = (uint32_t)(T * 25.0f);
    for (int32_t i = 0; i < n; i++) {
        const float a = ic_rnd(frame * 31u + (uint32_t)i) * IC_TAU, r = R * (1.2f + 0.9f * ic_rnd(frame * 57u + (uint32_t)i));
        ic_add(ic_round(CX + fm_cosf(a) * r * 1.3f), ic_round(CY + fm_sinf(a) * r), ic_dim(RGB(220, 230, 255), 200));
    }
}

static void i_hat(void)                  /* metal's spikes, up and down from the middle, at each hit: the decay (K1)
                                          * how far they spread */
{
    const uint32_t frame = (uint32_t)(T * 18.0f);
    const float spread = 0.12f + 0.3f * K1;
    ic_glow(ic_hue, 0);
    for (int32_t i = 0; i < ic->w; i += SMALL ? 2 : 1) {
        const float u = (float)i / W - 0.5f, env = fm_expf(-u * u / (spread * spread));
        const float a = H * 0.46f * env * (0.25f + 0.75f * ic_rnd(frame * 61u + (uint32_t)i)) * (SMALL ? 0.7f + 0.3f * HIT : 0.3f + 0.7f * HIT);
        ic_line(X0 + (float)i, CY - a, X0 + (float)i, CY + a, ic_dim(ic_hue, 110 + (int32_t)(146.0f * env)));
    }
    ic_line(X0, CY, X0 + W - 1.0f, CY, ic_dim(ic_hue, 70));
}

static void i_ring_hat(void)             /* RING HAT: two rings beating against each other, sparks between */
{
    const float d = W * (0.08f + 0.06f * fm_sinf(T * 9.0f) * (0.3f + HIT)), r = fm_minf(W, H) * 0.3f * (1.0f + 0.15f * HIT);
    ic_glow(ic_hue, 60);
    ic_ellipse(CX - d, CY, r, r, 0.0f, ic_hue, 0);
    ic_ellipse(CX + d, CY, r, r, 0.0f, ic_dim(ic_hue, 190), 0);
    if (!SMALL)
        for (int32_t i = 0; i < (int32_t)(24.0f * HIT * (0.3f + K2)); i++)
            ic_add(ic_round(CX + (ic_rnd((uint32_t)i + (uint32_t)(T * 30.0f) * 13u) - 0.5f) * W * 0.6f),
                   ic_round(CY + (ic_rnd((uint32_t)i * 7u + (uint32_t)(T * 30.0f)) - 0.5f) * H * 0.6f), C_WHITE);
}

static void i_vcf(void)                  /* a low-pass's curve, its resonant peak (K2) at the cutoff (K0), the light
                                          * under it falling away */
{
    float px = 0.0f, py = 0.0f;
    for (int32_t i = 0; i <= ic->w; i++) {
        const float u = (float)i / W, fc = 0.18f + 0.62f * K0 + 0.05f * fm_sinf(T * 1.5f) * LIVE, d = (u - fc) * 10.0f;
        const float g = 1.0f / (1.0f + fm_expf(d)) + (0.25f + K2 * 0.8f) * fm_expf(-d * d * 1.5f);
        const float x = X0 + (float)i, y = Y0 + H * (0.92f - 0.62f * g);
        if (!SMALL && (i % 3) == 0)          /* the light under it */
            ic_line(x, y + 2.0f, x, Y0 + H - 1.0f, ic_dim(ic_hue, (int32_t)(30.0f + 50.0f * g)));
        if (i) {
            ic_glow(ic_hue, 60);
            ic_move(px, py);
            ic_to(x, y, ic_hue);
        }
        px = x;
        py = y;
    }
}

static void i_phase_dist(void)           /* a spiral turning in, wound tighter by the amount (K0), bent by the
                                          * asymmetry (K1) */
{
    const float R = fm_minf(W * 0.46f, H * 0.5f), turns = 1.5f + 2.5f * K0 * LIVE;
    const int32_t lines = SMALL ? 1 : 3;
    for (int32_t j = 0; j < lines; j++) {
        const int32_t n = (int32_t)(turns * 40.0f);
        ic_glow(ic_hue, j == 0 ? 55 : 0);
        for (int32_t i = 0; i <= n; i++) {
            const float f = (float)i / (float)n, a = f * turns * IC_TAU + T * 1.2f + (float)j * 0.12f;
            const float r = R * (1.0f - f) * (1.0f + 0.25f * (K1 - 0.5f) * fm_cosf(a));
            const float x = CX + fm_cosf(a) * r * (W > H ? 1.35f : 1.0f), y = CY + fm_sinf(a) * r;
            if (i == 0)
                ic_move(x, y);
            else
                ic_to(x, y, ic_dim(ic_hue, j == 0 ? 256 : 110));
        }
    }
}

static void i_six_op(uint32_t bank)      /* a sphere of wire loops turning: as many as the brightness (K0) lights,
                                          * the patch (K2) their tilts */
{
    const int32_t n = SMALL ? 2 : 4 + (int32_t)(K0 * 2.0f);
    const float R = fm_minf(W * 0.42f, H * 0.46f) * (1.0f + 0.08f * HIT);
    for (int32_t k = 0; k < n; k++) {
        const float psi = IC_PI * (float)k / (float)n + (float)bank * 0.4f + K2 * 1.3f;
        const float ry = R * fm_fabsf(fm_cosf(T * 0.6f + (float)k * 1.1f + K2 * 2.0f));
        ic_glow(ic_hue, k == 0 ? 55 : 30);
        ic_ellipse(CX, CY, R * (W > H ? 1.25f : 1.0f), ry + 1.0f, psi, ic_dim(ic_hue, k == 0 ? 256 : 180), 0);
    }
}

static void i_terrain(void)              /* wire mountains in perspective, a light travelling the path (radius K0,
                                          * offset K1) across them */
{
    const int32_t rows = SMALL ? 2 : 5, cols = SMALL ? 6 : 12;
    float yy[5][13], xx[5][13];
    for (int32_t j = 0; j < rows; j++)
        for (int32_t i = 0; i <= cols; i++) {
            const float depth = (float)j / (float)(rows > 1 ? rows - 1 : 1), shrink = 1.0f - 0.35f * depth;
            const float u = (float)i / (float)cols;
            const float hgt = fm_fabsf(fm_sinf(u * 7.0f + (float)j * 1.7f + K2 * 4.0f + T * 0.15f)) *
                              fm_fabsf(fm_sinf(u * 3.0f + (float)j * 0.9f)) * (0.7f + 0.3f * LIVE);
            xx[j][i] = CX + (u - 0.5f) * W * shrink;
            yy[j][i] = Y0 + H * (0.95f - 0.13f * (float)j) - hgt * H * 0.5f * shrink;
        }
    ic_glow(ic_hue, 0);
    for (int32_t j = rows - 1; j >= 0; j--) {
        ic_move(xx[j][0], yy[j][0]);
        for (int32_t i = 1; i <= cols; i++)
            ic_to(xx[j][i], yy[j][i], ic_dim(ic_hue, 256 - 30 * j));
        if (j > 0 && !SMALL)
            for (int32_t i = 0; i <= cols; i += 2)
                ic_line(xx[j][i], yy[j][i], xx[j - 1][i], yy[j - 1][i], ic_dim(ic_hue, 70));
    }
    const float a = T * 1.2f, ox = CX + W * 0.25f * (K1 - 0.5f), r = W * (0.1f + 0.2f * K0);
    ic_disc(ox + fm_cosf(a) * r, Y0 + H * 0.45f + fm_sinf(a) * r * 0.3f, SMALL ? 1.0f : 1.5f + HIT,
            ic_mix(ic_hue, C_WHITE, 0.5f));
}

static void i_string_machine(void)       /* a bundle of detuned waves, chorused (K0) */
{
    const int32_t n = SMALL ? 2 : 6;
    for (int32_t j = 0; j < n; j++) {
        const float det = 1.0f + ((float)j - 2.5f) * 0.03f * (0.3f + K0);
        const float a = H * 0.36f * (0.8f + 0.2f * fm_sinf(T * 0.7f + (float)j)) * LIVE;
        ic_glow(ic_hue, j == 0 ? 50 : 0);
        for (int32_t i = 0; i <= ic->w; i++) {
            const float u = (float)i / W, y = CY - a * fm_sinf(u * IC_TAU * 1.6f * det + T * (1.2f + 0.3f * (float)j));
            if (i == 0)
                ic_move(X0, y);
            else
                ic_to(X0 + (float)i, y, ic_dim(ic_hue, j == 0 ? 256 : 140 - 10 * j));
        }
    }
}

static void i_chiptune(void)             /* pixels stepping up an arpeggio, 8-bit: its pulse width (K1) their size */
{
    static const int8_t ARP[8] = {0, 2, 4, 2, 5, 3, 1, 3};
    const float s = SMALL ? 2.0f : fm_maxf(3.0f, H / 6.0f), step = s + 2.0f;
    const int32_t n = (int32_t)(W / step), shift = (int32_t)(T * (4.0f + K0 * 4.0f));
    for (int32_t i = 0; i < n; i++) {
        const int32_t v = ARP[(i + shift) & 7];
        const float x = X0 + (float)i * step, y = CY - s * 0.5f + ((float)v - 2.5f) * (H * 0.15f) * LIVE;
        const float sz = s * (0.7f + 0.3f * K1);
        ic_halo = 0;
        for (int32_t j = 0; j < (int32_t)sz; j++)
            for (int32_t k = 0; k < (int32_t)sz; k++)
                ic_add(ic_round(x) + k, ic_round(y) + j, (j == 0 || k == 0) ? ic_dim(ic_hue, 160) : ic_hue);
        if (!SMALL && i + 1 < n) {       /* the step to the next, faint */
            const float ny = CY - s * 0.5f + ((float)ARP[(i + 1 + shift) & 7] - 2.5f) * (H * 0.15f) * LIVE;
            ic_line(x + sz, y + sz * 0.5f, x + step, y + sz * 0.5f, ic_dim(ic_hue, 70));
            ic_line(x + step, y + sz * 0.5f, x + step, ny + sz * 0.5f, ic_dim(ic_hue, 70));
        }
        if (!SMALL && ic_rnd((uint32_t)(i * 31 + shift)) > 0.7f)   /* a stray pixel near */
            ic_add(ic_round(x), ic_round(y - step * 1.5f), ic_dim(ic_hue, 110));
    }
}

/* an engine's icon (GRAV_ENGINE_*), drawn into i's box */
static void icon_draw(uint32_t engine, const icon_t *i)
{
    ic = i;
    ic_hue = ENGINE_HUE[engine < GRAV_ENGINE_ALL ? engine : 0];
    ic_halo = 0;
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
    ic_halo = 0;
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
#undef X0
#undef Y0
#undef K0
#undef K1
#undef K2
#undef T
#undef HIT
#undef LIVE
#undef SMALL
