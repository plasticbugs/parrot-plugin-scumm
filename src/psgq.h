// psgq: psgmix's music in the mixer's time. Portable C, used by the cart
// (plugin/cart.cpp) and the Mac test (test/scumm_host.cpp).
//
// psgmix reads the AdLib driver's OPL, which moves only as the mixer runs.
// Called once a list, it gives the music as it stands when the list is
// built, so when the engine keeps the cart from building lists for a while,
// the music holds and then jumps (the notes in between are lost). With
// psgq the mixer calls psgmix at the end of each TV frame's worth of its
// samples and queues the bytes, and each list takes one frame's, a lead
// behind. The lists go on while the engine is away (cart.cpp's timer), and
// the music with them, from what the mixer made ahead.
//
// A gap longer than the lead empties the queue (the music holds); the mixer
// then catches up and the queue is far ahead: the frames more than the lead
// are run into the PSG's registers at once (a jump, as before, but only past
// the lead). The mixer's clock and the console's differ a little: every few
// seconds a frame is merged into the next, or a list waits a frame, to keep
// the lead.
//
//   once:                 psgq_init(q, lead_frames)
//   each TV frame of the mixer's samples:  psgq_push(q, bytes, n)
//   each list:            n = psgq_pop(q, bytes)   (bytes[PSGQ_OUT])
//
// push and pop must not run at once (the cart: push with interrupts off,
// pop from core 0's code or the timer's interrupt, never both).
#pragma once
#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define PSGQ_N   128            // TV frames kept, a power of two (2.1 s)
#define PSGQ_B   12             // bytes a frame (psgmix's, at most 11 without the DAC)
#define PSGQ_OUT 16             // bytes a list's pop gives, at most
#define PSGQ_JUMP 3             // frames past the lead: run them in at once
#define PSGQ_WIN 256            // lists between the clocks' checks (4.3 s)

typedef struct {
    uint32_t pushed;            // frames in
    uint32_t popped;            // frames out as they were
    uint32_t waited;            // lists before the queue first had its lead
    uint32_t empty;             // lists with the queue empty (the music held)
    uint32_t jumps, jumped;     // jumps, and the frames they ran in
    uint32_t full;              // frames run in because the queue was full
    uint32_t merged, held;      // the clocks: frames merged, lists that waited
    uint32_t depth_min;         // the fewest frames queued at a list, once primed
} psgq_stats_t;

typedef struct {
    uint8_t n[PSGQ_N];
    uint8_t b[PSGQ_N][PSGQ_B];
    volatile uint32_t wr, rd;
    uint32_t lead;
    bool primed, dirty;          // dirty: frames run in, so the console's registers aren't want
    uint16_t want[8], sent[8];   // the PSG's registers (0 2 4 periods, 6 the noise, odd the attenuations):
    uint8_t latch_w, latch_s;    // as the music has them, and as the console has been sent
    uint32_t win, dmin, dmax;
    psgq_stats_t stats;
} psgq_t;

void psgq_init(psgq_t *q, int lead_frames);
void psgq_push(psgq_t *q, const uint8_t *bytes, int n);
int psgq_pop(psgq_t *q, uint8_t *out);
static inline uint32_t psgq_depth(const psgq_t *q) { return q->wr - q->rd; }

#ifdef __cplusplus
}
#endif
