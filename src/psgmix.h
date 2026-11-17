// psgmix: SCUMM v3-v4 sound on the ColecoVision's PSG, an SN76489: the
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
//   before the engine starts:  psgmix_install()  (music_driver "pcjr")
//   each TV frame:             n = psgmix_frame(bytes)  (bytes[12])
#pragma once
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

void psgmix_install(void);
int psgmix_frame(uint8_t *out);

// for tests: the CMS voices' pitches and loudness as the PSG would play them
typedef struct {
    uint32_t frames, cms_frames, cms_voices_dropped, bass_raised;
} psgmix_stats_t;
extern psgmix_stats_t psgmix_stats;

#ifdef __cplusplus
}
#endif
