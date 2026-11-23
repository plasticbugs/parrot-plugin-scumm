// speechdac.c -- see speechdac.h.
#include "speechdac.h"

#include <math.h>
#include <string.h>

// The DAC: two or three tones at a period of 1, their attenuators as
// measured on the user's console (coleco/ladder.json: Star Wars' table for
// tones 1 and 2), summed. Level i of the DAC's levels is i / (levels - 1)
// of its top; silence, while it's up, is the middle one.
static const float ladder[3][16] = {
    {1.0f, 0.7914f, 0.6320f, 0.5084f, 0.4019f, 0.3161f, 0.2459f, 0.1912f, 0.1396f, 0.1080f, 0.0844f, 0.0672f, 0.0524f, 0.0421f, 0.0310f, 0.0f},
    {1.0f, 0.7816f, 0.6125f, 0.4850f, 0.3925f, 0.3109f, 0.2416f, 0.1872f, 0.1373f, 0.1058f, 0.0828f, 0.0653f, 0.0533f, 0.0412f, 0.0306f, 0.0f},
    {1.0f, 0.7979f, 0.6229f, 0.4921f, 0.3942f, 0.3004f, 0.2348f, 0.1874f, 0.1419f, 0.1106f, 0.0846f, 0.0677f, 0.0534f, 0.0420f, 0.0328f, 0.0f}};
// The voice: GAIN times the mixer's, rounded off above KNEE of the DAC's
// swing (to 1 at most). Fate of Atlantis's MONSTER.SOU at ScummVM's speech
// volume is -22 dBFS RMS, its loudest 24,576: so it is -12 dB of the DAC's
// swing, a few of its peaks rounded.
#define GAIN       3.0f
#define KNEE       0.85f
#define SDAC_HOLD  6                // lists the DAC stays after the voice has gone by: 0.1 s
#define LOWPASS_HZ 2500.0f          // below the samples' 2,981 Hz half
// Each level's nearest settings of the attenuators (K of them: 16 with
// three tones, 4 with two), of which a sample takes the one whose writes,
// 52 T apart, stray least from the level on the way there too (the time
// between samples: about 600 T). On FOA's voice, 200-2,500 Hz, what the
// DAC adds is then 34 dB under the speech with three tones, 27 with two;
// 17 with 0.2.11's nearest pair (the levels between its two writes).
#ifndef SDAC_K
#define SDAC_K     16
#endif
#define K          SDAC_K
#define K2         (SDAC_K < 4 ? SDAC_K : 4)
#define T_WRITE    52.0f
#define T_SAMPLE   600.0f

static uint16_t cand[256][K];       // tone 0's attenuation << 8 | tone 1's << 4 | tone 2's
static int built, levels, mid;
static float top;

float sdac_tone_level(int tone, int att) { return ladder[tone % 3][att & 15]; }

static float value(int tones, uint16_t a) {
    return (tones == 3 ? ladder[0][a >> 8] : 0.0f) + ladder[1][a >> 4 & 15] + ladder[2][a & 15];
}

float sdac_mid(const sdac_t *d) { return value(d->tones, cand[mid][0]); }

static void build(int tones) {
    if (built == tones) return;
    levels = tones == 3 ? 256 : 128;
    top = tones == 3 ? 2.10f : 1.40f;
    mid = levels / 2;
    for (int i = 0; i < levels; i++) {
        const float want = top * (float)i / (float)(levels - 1);
        float dist[K];
        int n = 0;
        for (int a0 = 0; a0 < (tones == 3 ? 16 : 1); a0++)
            for (int a1 = 0; a1 < 16; a1++)
                for (int a2 = 0; a2 < 16; a2++) {
                    const uint16_t a = (uint16_t)((tones == 3 ? a0 : 15) << 8 | a1 << 4 | a2);
                    const float e = fabsf(value(tones, a) - want);
                    if (n == K && e >= dist[K - 1]) continue;
                    int j = n < K ? n++ : K - 1;
                    for (; j > 0 && dist[j - 1] > e; j--) {
                        dist[j] = dist[j - 1];
                        cand[i][j] = cand[i][j - 1];
                    }
                    dist[j] = e;
                    cand[i][j] = a;
                }
    }
    built = tones;
}

void sdac_init(sdac_t *d, int rate, int tones, int lead_ms) {
    tones = tones == 3 ? 3 : 2;
    build(tones);
    memset(d, 0, sizeof *d);
    d->tones = (uint8_t)tones;
    // the mixer's samples a DAC sample: rate x a list's time / its samples
    d->list_t = tones == 3 ? SDAC_LIST_T3 : SDAC_LIST_T;
    d->step = ((uint64_t)rate * d->list_t << 32) / (3579545ull * SDAC_N);
    const uint32_t list = (uint32_t)((d->step * SDAC_N) >> 32);
    // kept ahead: the lead, or a list's worth (the next list's samples are
    // all made when it's built, as the console starts the one before) and
    // 6 ms more; room left in the ring for a mix (256) and two lists
    d->target = lead_ms > 0 ? (uint32_t)((uint64_t)rate * (uint32_t)lead_ms / 1000) : list + (uint32_t)rate / 170;
    if (d->target > SDAC_RING - 2 * list - 512) d->target = SDAC_RING - 2 * list - 512;
    // the DAC stays while the lead's voice plays out, and a little longer
    d->hold_lists = (uint8_t)(SDAC_HOLD + (lead_ms > 0 ? (d->target + list - 1) / list : 0));
    d->att[0] = d->att[1] = d->att[2] = 15;
    d->stats.ahead_min = 0x7FFFFFFF;
    // a fourth-order Butterworth low-pass, as two biquads (bilinear)
    const float k = tanf(3.14159265f * LOWPASS_HZ / (float)rate);
    static const float q[2] = {0.54119610f, 1.30656296f};
    for (int i = 0; i < 2; i++) {
        float n = 1.0f / (1.0f + k / q[i] + k * k);
        d->c[i][0] = k * k * n;
        d->c[i][1] = 2.0f * k * k * n;
        d->c[i][2] = k * k * n;
        d->c[i][3] = 2.0f * (k * k - 1.0f) * n;
        d->c[i][4] = (1.0f - k / q[i] + k * k) * n;
    }
}

void sdac_in(sdac_t *d, const int16_t *st, int frames, bool speaking) {
    if (speaking) d->spoke = true;
    for (int i = 0; i < frames; i++) {
        float x = ((float)st[2 * i] + (float)st[2 * i + 1]) * 0.5f;
        for (int s = 0; s < 2; s++) {
            const float *c = d->c[s];
            float *z = d->z[s];
            float y = c[0] * x + z[0];
            z[0] = c[1] * x - c[3] * y + z[1];
            z[1] = c[2] * x - c[4] * y;
            x = y;
        }
        int v = (int)lrintf(x);
        d->ring[d->wr++ & (SDAC_RING - 1)] = (int16_t)(v > 32767 ? 32767 : v < -32768 ? -32768 : v);
    }
}

void sdac_late(sdac_t *d, int lists) {
    d->stats.late += (uint32_t)lists;
    // with no lead, those samples' time has gone; with one, they play next
    // (the lead a list longer for each, which the trim takes back)
    const uint32_t list = (uint32_t)((d->step * SDAC_N) >> 32);
    if (d->on && d->target < 2 * list) d->rd += d->step * SDAC_N * (uint64_t)lists;
}

// a sample's attenuators: of the level's nearest settings, the one (and the
// order of its writes) that keeps closest to want, on the way there too
static void choose(sdac_t *d, int lvl, float want, uint8_t *out) {
    const int t = d->tones, c0 = t == 3 ? 0 : 1;
    const float w_end = T_SAMPLE - (float)(t - 1) * T_WRITE;
    const int kn = t == 3 ? K : K2;
    float now = 0.0f;
    for (int c = c0; c < 3; c++) now += ladder[c][d->att[c]];
    float best = 1e30f;
    uint16_t best_a = cand[lvl][0];
    int first = c0, last = 2;
    for (int j = 0; j < kn; j++) {
        const uint16_t a = cand[lvl][j];
        const uint8_t na[3] = {(uint8_t)(a >> 8), (uint8_t)(a >> 4 & 15), (uint8_t)(a & 15)};
        const float v = value(t, a), r = (v - want) * (v - want) * w_end;
        if (r >= best) continue;
        float dl[3];
        for (int c = c0; c < 3; c++) dl[c] = ladder[c][na[c]] - ladder[c][d->att[c]];
        if (t == 2) {
            const float e1 = now + dl[1] - want, e2 = now + dl[2] - want;
            const float cost = r + T_WRITE * (e1 * e1 < e2 * e2 ? e1 * e1 : e2 * e2);
            if (cost < best) {
                best = cost;
                best_a = a;
                first = e1 * e1 < e2 * e2 ? 1 : 2;
                last = 3 - first;
            }
        } else {
            // the first write's tone x, the last's z (the middle's the other):
            // the levels on the way, now + dl[x] and v - dl[z]
            float e1[3], e2[3];
            for (int c = 0; c < 3; c++) {
                e1[c] = (now + dl[c] - want) * (now + dl[c] - want);
                e2[c] = (v - dl[c] - want) * (v - dl[c] - want);
            }
            for (int x = 0; x < 3; x++)
                for (int z = 0; z < 3; z++) {
                    if (x == z) continue;
                    const float cost = r + T_WRITE * (e1[x] + e2[z]);
                    if (cost < best) {
                        best = cost;
                        best_a = a;
                        first = x;
                        last = z;
                    }
                }
        }
    }
    const uint8_t na[3] = {(uint8_t)(best_a >> 8), (uint8_t)(best_a >> 4 & 15), (uint8_t)(best_a & 15)};
    int n = 0;
    out[n++] = (uint8_t)(0x90 | first << 5 | na[first]);
    if (t == 3) {
        const int middle = 3 - first - last;
        out[n++] = (uint8_t)(0x90 | middle << 5 | na[middle]);
    }
    out[n++] = (uint8_t)(0x90 | last << 5 | na[last]);
    d->att[0] = na[0];
    d->att[1] = na[1];
    d->att[2] = na[2];
}

bool sdac_list(sdac_t *d, uint8_t *out, uint32_t owed) {
    const bool heard = d->spoke;
    d->spoke = false;
    if (heard) d->hold = d->hold_lists;
    else if (d->hold) d->hold--;
    const bool on = d->hold > 0 || d->gain > 0;
    const int32_t list = (int32_t)((d->step * SDAC_N) >> 32);
    // where the mixer is, made or due: rd this far behind it
    const int64_t home = (int64_t)d->wr + (int64_t)owed - (int64_t)d->target;
    if (!on || !d->on) {                    // off, or just coming up
        if (d->on) d->base = d->trim;       // (going off: the console's pace so far, for the next time)
        d->rd = (uint64_t)(home > 0 ? home : 0) << 32;
        d->err = 0;
        d->att[0] = d->att[1] = d->att[2] = 15;
    }
    d->on = on;
    if (!on) return false;
    d->stats.lists++;
    if (heard) d->stats.speaking_lists++;
    int32_t made = (int32_t)(d->wr - (uint32_t)(d->rd >> 32));
    if (made < d->stats.ahead_min) d->stats.ahead_min = made;
    int32_t ahead = made + (int32_t)owed;
    // far ahead (after late lists): back to the lead
    if (ahead > (int32_t)d->target + 4 * list) {
        d->stats.skipped += (uint32_t)(ahead - (int32_t)d->target);
        d->rd = (uint64_t)(home > 0 ? home : 0) << 32;
        ahead = (int32_t)d->target;
        d->err = 0;
    }
    // a little quicker or slower by how far it has been from the lead, on
    // average (half a second: a list built late or early doesn't move it),
    // up to 1.5 %: the console's lists may be a little quicker or slower than
    // the kernel's costs (MAME's are 0.6 % quicker), which the trim learns
    d->err += ((float)(ahead - (int32_t)d->target) - d->err) * (1.0f / 32);
    float trim = d->base + d->err * 1e-4f;
    trim = trim > 0.015f ? 0.015f : trim < -0.015f ? -0.015f : trim;
    d->trim = trim;
    const uint64_t step = d->step + (uint64_t)((int64_t)((float)(d->step >> 16) * trim) * 65536);
    const int t = d->tones, half = levels / 2 - 1;
    for (int k = 0; k < SDAC_N; k++) {
        const uint32_t i = (uint32_t)(d->rd >> 32), f = (uint32_t)(d->rd >> 16) & 0xFFFF;
        int32_t s;
        if ((int32_t)(d->wr - i) < 2) {     // not made yet: the last held (the voice waits)
            s = d->last;
            d->stats.short_in++;
        } else {
            const int32_t a = d->ring[i & (SDAC_RING - 1)], b = d->ring[(i + 1) & (SDAC_RING - 1)];
            s = a + (int32_t)(((int64_t)(b - a) * f) >> 16);
            d->rd += step;
            d->last = s;
        }
        if (d->hold > 2) { if (d->gain < 32 && !(k & 3)) d->gain++; }
        else if (d->gain && !(k & 3)) d->gain--;
        float y = (float)s * (GAIN / 32768.0f);
        if (y > KNEE) { y = KNEE + (1.0f - KNEE) * tanhf((y - KNEE) * (1.0f / (1.0f - KNEE))); d->stats.clipped++; }
        else if (y < -KNEE) { y = -KNEE - (1.0f - KNEE) * tanhf((-y - KNEE) * (1.0f / (1.0f - KNEE))); d->stats.clipped++; }
        const float lv = ((float)mid + y * (float)half) * (float)d->gain * (1.0f / 32);
        int lvl = (int)lrintf(lv);
        lvl = lvl < 0 ? 0 : lvl >= levels ? levels - 1 : lvl;
        const float want = top * lv / (float)(levels - 1);
#ifdef SDAC_EVAL_HOOK
        SDAC_EVAL_HOOK(k, want);            // tests: the level each sample wanted
#endif
        choose(d, lvl, want, out + k * t);
    }
    return true;
}
