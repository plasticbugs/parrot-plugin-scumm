// scummcv.c -- see scummcv.h.
#include "scummcv.h"

#include <math.h>
#include <string.h>

// as cvengine::video::PALETTE and nescv.c have them
const uint8_t scummcv_tms_rgb[16][3] = {
    {0, 0, 0},       {0, 0, 0},       {33, 200, 66},   {94, 220, 120},
    {84, 85, 237},   {125, 118, 252}, {212, 82, 77},   {66, 235, 245},
    {252, 85, 84},   {255, 121, 120}, {212, 193, 84},  {230, 206, 128},
    {33, 176, 59},   {201, 91, 186},  {204, 204, 204}, {255, 255, 255},
};

// Colours compared in OKLab, dots mixed in linear light (the eye averages
// light, not sRGB numbers: half black, half blue looks brighter than the
// sRGB average). Each colour becomes the nearest of the 15 colours and the
// 15 x 14 / 2 pairs mixed in 16ths, plus a penalty for mixing colours far
// apart (a grey as black and white dots is a worse grey than the TMS9918's
// own), after Yliluoma's pattern dithering.
typedef struct { float l, a, b; } lab_t;

static float lin(int v) {
    float c = v / 255.0f;
    return c <= 0.04045f ? c / 12.92f : powf((c + 0.055f) / 1.055f, 2.4f);
}

static lab_t oklab(float r, float g, float b) {
    float l = cbrtf(0.4122214708f * r + 0.5363325363f * g + 0.0514459929f * b);
    float m = cbrtf(0.2119034982f * r + 0.6806995451f * g + 0.1073969566f * b);
    float s = cbrtf(0.0883024619f * r + 0.2817188376f * g + 0.6299787005f * b);
    lab_t o = {0.2104542553f * l + 0.7936177850f * m - 0.0040720468f * s,
               1.9779984951f * l - 2.4285922050f * m + 0.4505937099f * s,
               0.0259040371f * l + 0.7827717662f * m - 0.8086757660f * s};
    return o;
}

static float d2(lab_t x, lab_t y) {
    float l = x.l - y.l, a = x.a - y.a, b = x.b - y.b;
    return l * l + a * a + b * b;
}

#define NMIX (15 + 105 * 15)
typedef struct { lab_t lab; float penalty; uint8_t a, b, m, grey; } mix_t;
static mix_t MIX[NMIX];
static int nmix;
float scummcv_penalty = 3.0f;
float scummcv_lweight = 0.1f;   // a lightness difference's share of the penalty
float scummcv_lift = 0.6f;      // OKLab lightness to this power: the darks come up
float scummcv_sat = 1.7f;       // and the chroma times this
int scummcv_sparse = 4;         // a pair far apart in lightness mixes this many 16ths of each at least
float scummcv_sparse_dl = 0.15f;  // (far apart: this much OKLab lightness)

static bool is_grey(int c) { return c == 1 || c == 14 || c == 15; }

static void mix_init(void) {
    if (nmix) return;
    float lr[16], lg[16], lb[16];
    lab_t lab[16];
    for (int c = 1; c < 16; c++) {
        lr[c] = lin(scummcv_tms_rgb[c][0]);
        lg[c] = lin(scummcv_tms_rgb[c][1]);
        lb[c] = lin(scummcv_tms_rgb[c][2]);
        lab[c] = oklab(lr[c], lg[c], lb[c]);
        MIX[nmix++] = (mix_t){lab[c], 0, (uint8_t)c, (uint8_t)c, 0, is_grey(c)};
    }
    for (int c = 1; c < 16; c++)
        for (int d = c + 1; d < 16; d++)
            for (int m = 1; m < 16; m++) {
                // a few dots of a light colour on a dark one (or the other way)
                // don't read as a tone: the 4 x 4 pattern makes a grid of them
                int few = m < 16 - m ? m : 16 - m;
                if (few < scummcv_sparse && fabsf(lab[c].l - lab[d].l) > scummcv_sparse_dl) continue;
                float t = m / 16.0f;
                lab_t o = oklab(lr[c] + (lr[d] - lr[c]) * t, lg[c] + (lg[d] - lg[c]) * t, lb[c] + (lb[d] - lb[c]) * t);
                // the penalty counts a difference in hue and chroma far more than
                // one in lightness: dark yellow and black dots read as brown,
                // red and green dots as noise
                float dl = lab[c].l - lab[d].l, da = lab[c].a - lab[d].a, db = lab[c].b - lab[d].b;
                float far = scummcv_lweight * dl * dl + da * da + db * db;
                MIX[nmix++] = (mix_t){o, far * t * (1 - t), (uint8_t)c, (uint8_t)d, (uint8_t)m, is_grey(c) && is_grey(d)};
            }
}

void scummcv_palette(scummcv_pal_t *p, const uint8_t *rgb, int dither) {
    mix_init();
    const int n = dither ? nmix : 15;
    const float k = scummcv_penalty;
    for (int i = 0; i < 256; i++) {
        lab_t c = oklab(lin(rgb[i * 3]), lin(rgb[i * 3 + 1]), lin(rgb[i * 3 + 2]));
        // the grade: SCUMM's art is darker and greyer than the TMS9918's colours
        c.l = c.l > 0 ? powf(c.l, scummcv_lift) : 0;
        c.a *= dither == 2 ? 0 : scummcv_sat;
        c.b *= dither == 2 ? 0 : scummcv_sat;
        float best = 1e30f;
        int bi = 0;
        for (int j = 0; j < n; j++) {
            if (dither == 2 && !MIX[j].grey) continue;
            float e = d2(c, MIX[j].lab) + k * MIX[j].penalty;
            if (e < best) {
                best = e;
                bi = j;
            }
        }
        p->a[i] = MIX[bi].a;
        p->b[i] = MIX[bi].b;
        p->mix[i] = MIX[bi].m;
    }
}

// one window's left edge, moved when the pointer is within 32 pixels of it
static int pan(int x, int mx) {
    if (mx - x < 32) x = (mx - 32) & ~7;
    if (mx - x > 256 - 32) x = (mx - (256 - 32) + 7) & ~7;
    if (x < 0) x = 0;
    if (x > SCUMMCV_W - 256) x = SCUMMCV_W - 256;
    return x;
}

void scummcv_view(scummcv_view_t *v, int mx, int my) {
    if (my < v->split) {
        v->x = pan(v->x, mx);
        v->bx = 0;                          // the verbs and the choices start at the left
    } else {
        v->bx = pan(v->bx, mx);
    }
    if (my >= SCUMMCV_H - 16) v->y = SCUMMCV_H - 192;
    else if (my < 16) v->y = 0;
}

void scummcv_view_keep(scummcv_view_t *v, int left, int right) {
    if (right <= left) return;
    int lo = right - 256, hi = left;
    if (lo > hi) lo = hi = (left + right) / 2 - 128;
    lo = (lo + 7) & ~7;
    hi &= ~7;
    if (lo > hi) lo = hi;
    if (v->x < lo) v->x = lo;
    if (v->x > hi) v->x = hi;
    if (v->x < 0) v->x = 0;
    if (v->x > SCUMMCV_W - 256) v->x = SCUMMCV_W - 256;
}

static const uint8_t BAYER[4][4] = {{0, 8, 2, 10}, {12, 4, 14, 6}, {3, 11, 1, 9}, {15, 7, 13, 5}};

void scummcv_convert(const scummcv_pal_t *p, const uint8_t *pixels, int pitch, const scummcv_view_t *v, uint8_t *out) {
    for (int y = 0; y < 192; y++) {
        const int line = v->y + y;
        const uint8_t *s = pixels + (long)line * pitch + (line >= v->split ? v->bx : v->x);
        const uint8_t *th = BAYER[line & 3];
        uint8_t *o = out + y * 256;
        for (int x = 0; x < 256; x++) {
            const uint8_t c = s[x];
            o[x] = p->mix[c] > th[x & 3] ? p->b[c] : p->a[c];
        }
    }
}

// ---- the pointer -------------------------------------------------------------------

static void set_px(uint8_t *pat, int x, int y) { pat[(x >= 8 ? 16 : 0) + y] |= (uint8_t)(0x80 >> (x & 7)); }

static int window_at(int lo, int hi) {   // the shape's extent [lo, hi]: the 16 kept, from
    return hi - lo + 1 <= 14 ? lo - 1 : lo;
}

bool scummcv_cursor(scummcv_cursor_t *c, const uint8_t *buf, int w, int h, int pitch, int hotx, int hoty, uint8_t key) {
    int x0 = w, x1 = -1, y0 = h, y1 = -1;
    uint16_t count[256] = {0};
    for (int y = 0; y < h; y++)
        for (int x = 0; x < w; x++) {
            uint8_t v = buf[y * pitch + x];
            if (v == key) continue;
            count[v]++;
            if (x < x0) x0 = x;
            if (x > x1) x1 = x;
            if (y < y0) y0 = y;
            if (y > y1) y1 = y;
        }
    uint8_t pat[64] = {0};
    c->empty = x1 < 0;
    if (!c->empty) {
        int best = 0;
        for (int i = 1; i < 256; i++)
            if (count[i] > count[best]) best = i;
        c->index = (uint8_t)best;
        int wx = window_at(x0, x1), wy = window_at(y0, y1);
        c->ox = wx - hotx;
        c->oy = wy - hoty;
        for (int y = 0; y < 16; y++)
            for (int x = 0; x < 16; x++) {
                int bx = wx + x, by = wy + y;
                if (bx < 0 || by < 0 || bx >= w || by >= h || buf[by * pitch + bx] == key) continue;
                set_px(pat, x, y);
                for (int dy = -1; dy <= 1; dy++)
                    for (int dx = -1; dx <= 1; dx++)
                        if (x + dx >= 0 && x + dx < 16 && y + dy >= 0 && y + dy < 16) set_px(pat + 32, x + dx, y + dy);
            }
    }
    bool changed = memcmp(pat, c->pat, sizeof pat) != 0;
    memcpy(c->pat, pat, sizeof pat);
    return changed;
}

void scummcv_pointer_sat(const scummcv_cursor_t *c, const scummcv_pal_t *p, const scummcv_view_t *v, int mx, int my,
                 bool shown, uint8_t sat[SCUMMCV_SAT]) {
    memset(sat, 0, SCUMMCV_SAT);
    sat[0] = sat[4] = sat[8] = 0xD0;          // none
    if (!shown || c->empty) return;
    const int sx = mx + c->ox - (my >= v->split ? v->bx : v->x), sy = my + c->oy - v->y;
    if (sx < -15 || sx > 255 || sy < -15 || sy > 191) return;
    // the shape's colour, kept light (a dark one would vanish into its
    // outline: SCUMM's pointers cycle through greys), round it black
    const uint8_t i = c->index;
    uint8_t col = p->mix[i] > 8 ? p->b[i] : p->a[i];
    const uint8_t *rgb = scummcv_tms_rgb[col];
    if (col < 2 || (rgb[0] * 3 + rgb[1] * 6 + rgb[2]) / 10 < 100) col = 14;
    const uint8_t line = 1;
    const uint8_t y = (uint8_t)(sy - 1), x = (uint8_t)(sx < 0 ? sx + 32 : sx), ec = sx < 0 ? 0x80 : 0;
    const uint8_t s[SCUMMCV_SAT] = {y, x, 0, (uint8_t)(ec | col), y, x, 4, (uint8_t)(ec | line), 0xD0};
    memcpy(sat, s, SCUMMCV_SAT);
}
