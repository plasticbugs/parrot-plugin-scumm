// speechdac: the game's speech on the PSG, as samples. Portable C, used by
// the cart (plugin/cart.cpp) and the Mac test (test/scumm_host.cpp).
//
// The mixer's output (MONSTER.SOU's voice in Fate of Atlantis, and the few
// digital effects) goes through a low-pass at 2.5 kHz and is resampled to
// SDAC_N samples a list: 5,962 a second while the console runs the lists
// back to back (nescv.h's NESCV_PCM_T). The DAC is two or three of the PSG's
// tones, parked by psgmix at a period of 1: their levels, as measured on the
// user's console (coleco/ladder.json), sum to the sample's (Star Wars' DAC,
// ../starwars/swcv.c). Two: tones 1 and 2, 128 levels (the music keeps tone
// 0). Three: tones 0, 1 and 2, 256 levels, while a computer has the music
// (MIDI). Each sample is a byte for each tone's attenuator, in the order
// that strays least on the way (the console writes them 52 T apart). The
// voice is three times as loud as the mixer has it, its peaks rounded off.
//
// The DAC plays the voice a lead behind the mixer: when the engine keeps
// the cart from building a list (cart.cpp's timer builds it then), the
// lists go on with what the mixer has made, and the mixer catches up after.
//
//   once:              sdac_init(d, mixer_rate, tones, lead_ms)
//   after each mix:    sdac_in(d, stereo, frames, speaking)
//   each list:         on = sdac_list(d, samples, owed)   (false: none in this one)
//                      owed: the mixer's samples due by now, not made yet
//   a list the console ran twice (late): sdac_late(d, lists)
#pragma once
#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define SDAC_N      100             // samples a list (nescv.h's NESCV_PCM_N)
#define SDAC_LIST_T 60036           // a list's T (NESCV_PCM_T), two bytes a sample
#define SDAC_LIST_T3 60336          // three (NESCV_PCM_T3)
#define SDAC_RING   4096            // the mixer's samples kept (a power of two)
#define SDAC_MAX_B  3               // bytes a sample, at most

typedef struct {
    uint32_t lists;                 // lists with samples
    uint32_t speaking_lists;        // of those, the ones a voice played in
    uint32_t short_in;              // samples the mixer hadn't made when a list wanted them (the last held)
    uint32_t skipped;               // samples skipped (the mixer far ahead: after late lists)
    uint32_t clipped;               // samples rounded off at the top or bottom
    uint32_t late;                  // lists the console ran twice (sdac_late)
    int32_t ahead_min;              // the least the mixer had made ahead of a list's start (samples)
} sdac_stats_t;

typedef struct {
    int16_t ring[SDAC_RING];        // the mixer's output, mono, low-passed
    uint32_t wr;                    // samples written
    uint64_t rd;                    // the next sample's place, << 32
    uint64_t step;                  // the mixer's samples a DAC sample, << 32
    uint32_t list_t;                // a list's T
    uint32_t target;                // the mixer's samples ahead of rd (the lead)
    float trim, err, base;          // the step's trim, the lead's error it follows, and the trim learned
    float z[2][2];                  // the low-pass: two biquads' states
    float c[2][5];                  // and coefficients
    bool spoke;                     // a voice played since the last list
    bool on;
    uint8_t tones;                  // 2 or 3
    uint8_t hold, hold_lists;       // lists the DAC stays after the voice stops
    uint8_t gain;                   // the DAC's level, 0-32 (it comes and goes over a few lists)
    uint8_t att[3];                 // the attenuators as the last sample left them (tones 0, 1, 2)
    int32_t last;                   // the last sample, held while the mixer is short
    sdac_stats_t stats;
} sdac_t;

// tones: 2 (tones 1 and 2) or 3 (tones 0-2); lead_ms: the voice this far
// behind the mixer (0: a list's worth and 6 ms, as 0.2.11)
void sdac_init(sdac_t *d, int mixer_rate, int tones, int lead_ms);
// the mixer's output, interleaved stereo; speaking: a voice (or a digital
// effect) is playing
void sdac_in(sdac_t *d, const int16_t *stereo, int frames, bool speaking);
// the next list's samples, if it has any: SDAC_N of d->tones bytes each
// ($9x, $Bx and $Dx, the attenuators of tones 0, 1 and 2, in their order)
bool sdac_list(sdac_t *d, uint8_t *samples, uint32_t owed);
// the console ran the last list again, lists times (its samples twice)
void sdac_late(sdac_t *d, int lists);
// a tone's level at an attenuation, by the measured ladder (tests)
float sdac_tone_level(int tone, int att);
// the level the DAC rests at while it's up (tests)
float sdac_mid(const sdac_t *d);

#ifdef __cplusplus
}
#endif
