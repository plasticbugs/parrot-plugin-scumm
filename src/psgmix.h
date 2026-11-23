// psgmix: SCUMM v3-v5 sound on the ColecoVision's PSG, an SN76489: the
// PCjr's SN76496 at the same 3.58 MHz clock. Used by the cart (plugin/
// cart.cpp) and the Mac test (test/scumm_host.cpp, SCUMM_MUSIC=psg).
//
// Songs and effects with a PCjr version play through Player_V2 in PCjr
// mode, whose four channels are the PSG's own registers. Eight of MI1's
// floppy VGA songs (the lookout, the bar, ...) have a 176-byte placeholder
// for PCjr data; for those (WA data an eighth of the AdLib track's size or
// less) the AdLib music track is loaded instead, and Player_V2CMS plays it:
// LucasArts' Game Blaster driver, 8 square-wave voices on two SAA1099s.
// Its chip writes are cut down to the tones the PCjr player isn't using:
// the loudest voices, each kept on its PSG channel while it sounds.
//
// SCUMM v5 games with iMUSE (Fate of Atlantis, Monkey Island 2) have no PCjr
// or CMS music: "pcjr" isn't theirs, so the engine picks the AdLib, and
// iMUSE plays the AdLib arrangement (music and sound effects) on ScummVM's
// AdLib driver. Its OPL2 is psgmix's: no synthesis, the registers only. The
// nine channels' levels are followed as the chip's envelopes would run, and
// each TV frame the top and bottom notes and the next loudest go to the
// PSG's tones, and a drum or cymbal (a noisy FM patch) to its noise.
//
//   before the engine starts:  psgmix_install()  (music_driver "pcjr")
//   each TV frame:             n = psgmix_frame(bytes)  (bytes[12])
//
// While the game talks (speechdac.h), the tones the music doesn't have are
// the DAC: psgmix_tones(1) before psgmix_frame() leaves the music tone 0
// and the noise (the top of the loud notes, 10 dB down), tones 1 and 2 the
// DAC; psgmix_tones(0) leaves it nothing, all three tones the DAC and the
// noise off. The first frame so parks the DAC's tones at a period of 1, and
// the first with psgmix_tones(3) again sends all their registers anew.
#pragma once
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

void psgmix_install(void);
int psgmix_frame(uint8_t *out);
void psgmix_tones(int n);           // 3 (as installed), 1 or 0
int psgmix_opl(void);               // the AdLib driver's OPL is psgmix's (a v5 game): its music moves with the mixer

// for tests: the CMS voices' pitches and loudness as the PSG would play them
typedef struct {
    uint32_t frames, cms_frames, cms_voices_dropped, bass_raised;
    uint32_t opl_frames, opl_voices_dropped, opl_noise_frames;   // the AdLib driver's (SCUMM v5)
} psgmix_stats_t;
extern psgmix_stats_t psgmix_stats;
// for tests: each register the AdLib driver writes to the OPL tap
extern void (*psgmix_opl_log)(int reg, int val);
// for tests: each TV frame, each tone channel playing an OPL channel: its pitch
// (the PSG period before the bass's octaves up) and the period sent
extern void (*psgmix_voice_log)(int ch, int voice, float pitch, int period, int att);

#ifdef __cplusplus
}
#endif
