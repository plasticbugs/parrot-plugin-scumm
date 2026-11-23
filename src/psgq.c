// psgq: psgmix's music in the mixer's time (psgq.h)
#include "psgq.h"
#include <string.h>

void psgq_init(psgq_t *q, int lead_frames) {
    memset(q, 0, sizeof *q);
    q->lead = lead_frames < 1 ? 1 : lead_frames > PSGQ_N - 8 ? PSGQ_N - 8 : (uint32_t)lead_frames;
    for (int r = 0; r < 8; r++) q->want[r] = q->sent[r] = 0xFFFF;   // (not known: the same in both)
    q->dmin = UINT32_MAX;
    q->stats.depth_min = UINT32_MAX;
}

// PSG bytes into a register image: a latch byte (1rrrdddd) sets a register's
// low bits (a period's low 4, the others all of theirs), a data byte
// (0-dddddd) a period's high 6
static void apply(uint16_t *reg, uint8_t *latch, const uint8_t *b, int n) {
    for (int i = 0; i < n; i++) {
        const uint8_t x = b[i];
        if (x & 0x80) {
            const int r = x >> 4 & 7;
            *latch = (uint8_t)r;
            if (!(r & 1) && r < 6) reg[r] = (uint16_t)((reg[r] & 0x3F0) | (x & 15));
            else reg[r] = (uint16_t)(x & (r == 6 ? 7 : 15));
        } else {
            const int r = *latch;
            if (!(r & 1) && r < 6) reg[r] = (uint16_t)((reg[r] & 15) | (x & 0x3F) << 4);
            else reg[r] = (uint16_t)(x & 15);
        }
    }
}

// the oldest frame into the music's registers, not sent (a jump)
static void run_in(psgq_t *q) {
    const uint32_t i = q->rd % PSGQ_N;
    apply(q->want, &q->latch_w, q->b[i], q->n[i]);
    q->rd++;
    q->dirty = true;
}

void psgq_push(psgq_t *q, const uint8_t *bytes, int n) {
    if (n > PSGQ_B) n = PSGQ_B;                 // (never: psgmix's are 11 at most)
    if (q->wr - q->rd >= PSGQ_N) {              // full (a long load behind): the oldest goes in now
        run_in(q);
        q->stats.full++;
    }
    const uint32_t i = q->wr % PSGQ_N;
    q->n[i] = (uint8_t)(n > 0 ? n : 0);
    if (n > 0) memcpy(q->b[i], bytes, (size_t)n);
    q->wr++;
    q->stats.pushed++;
}

// the registers the console hasn't been sent as the music has them: periods,
// the noise, then the attenuations (psgmix's order)
static int send_diff(psgq_t *q, uint8_t *out) {
    int k = 0;
    for (int c = 0; c < 3; c++) {
        const int r = 2 * c;
        if (q->want[r] != q->sent[r] && q->want[r] != 0xFFFF) {
            out[k++] = (uint8_t)(0x80 | c << 5 | (q->want[r] & 15));
            out[k++] = (uint8_t)(q->want[r] >> 4 & 0x3F);
        }
    }
    if (q->want[6] != q->sent[6] && q->want[6] != 0xFFFF) out[k++] = (uint8_t)(0xE0 | q->want[6]);
    for (int c = 0; c < 4; c++) {
        const int r = 2 * c + 1;
        if (q->want[r] != q->sent[r] && q->want[r] != 0xFFFF) out[k++] = (uint8_t)(0x90 | c << 5 | q->want[r]);
    }
    apply(q->sent, &q->latch_s, out, k);
    q->dirty = false;
    return k;
}

int psgq_pop(psgq_t *q, uint8_t *out) {
    uint32_t d = q->wr - q->rd;
    if (!q->primed) {
        if (d < q->lead) {
            q->stats.waited++;
            return 0;
        }
        q->primed = true;
    }
    if (d < q->stats.depth_min) q->stats.depth_min = d;
    // the clocks: a frame merged when the queue has stayed ahead of the lead
    // all through a window, a list held when it has stayed behind
    bool hold = false;
    if (d < q->dmin) q->dmin = d;
    if (d > q->dmax) q->dmax = d;
    if (++q->win >= PSGQ_WIN) {
        if (q->dmin > q->lead + 1 && d > q->lead) {
            run_in(q);
            d--;
            q->stats.merged++;
        } else if (q->dmax + 1 < q->lead && d) {
            hold = true;
            q->stats.held++;
        }
        q->win = 0;
        q->dmin = UINT32_MAX;
        q->dmax = 0;
    }
    // far ahead (the engine was away longer than the lead): a jump to the lead
    if (d > q->lead + PSGQ_JUMP) {
        q->stats.jumps++;
        q->stats.jumped += d - q->lead;
        while (d > q->lead) {
            run_in(q);
            d--;
        }
    }
    if (!d || hold) {
        if (!d) q->stats.empty++;
        return q->dirty ? send_diff(q, out) : 0;
    }
    const uint32_t i = q->rd % PSGQ_N;
    const int n = q->n[i];
    memcpy(out, q->b[i], (size_t)n);
    q->rd++;
    apply(q->want, &q->latch_w, out, n);
    if (q->dirty) return send_diff(q, out);
    apply(q->sent, &q->latch_s, out, n);
    q->stats.popped++;
    return n;
}
