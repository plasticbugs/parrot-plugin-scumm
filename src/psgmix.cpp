// psgmix.cpp: see psgmix.h.
#include "psgmix.h"

#include <string.h>

#include "audio/mixer.h"
#include "scumm/music.h"
#include "scumm/players/player_v2.h"
#include "scumm/players/player_v2cms.h"
#include "scumm/scumm.h"
#include "scumm/sound.h"

psgmix_stats_t psgmix_stats;

#define PCJR_STALE 3     // TV frames without the PCjr player rendering: its notes stop
#define SONG_PRIORITY 120 // a PCjr sound this high is a song (MI1: songs 127, its sound effects 100)

// ---- the state the players leave ----------------------------------------------------

static struct {
    // the PCjr player's four channels, as the PSG's registers
    int period[3], noise, att[4];
    bool on[4], seen;
    int stale;
    // the CMS player's 8 music voices
    uint8_t ampl[8], freq[8], oct[8];
    // which CMS voice each PSG tone channel plays (-1: none)
    int voice[3];
    // what the console's PSG has
    int sent_period[3], sent_noise, sent_att[4];
} S;

static void reset(void) {
    memset(&S, 0, sizeof S);
    for (int c = 0; c < 4; c++) {
        S.att[c] = 15;
        S.sent_att[c] = -1;
    }
    for (int c = 0; c < 3; c++) {
        S.voice[c] = -1;
        S.sent_period[c] = -1;
    }
    S.noise = 0;
    S.sent_noise = -1;
}

static void pcjr_tap(int ch, int freq, int att, bool on) {
    if (ch < 0 || ch > 3) return;
    S.seen = true;
    S.on[ch] = on;
    S.att[ch] = on ? att : 15;
    if (!on) return;
    if (ch < 3) S.period[ch] = freq & 0x3FF;
    else S.noise = freq & 7;
}

static void cms_tap(int chip, const byte *ampl, const byte *freq, const byte *octave) {
    if (chip < 0) {                         // all off
        memset(S.ampl, 0, sizeof S.ampl);
        return;
    }
    for (int i = 0; i < 4; i++) {
        int v = chip * 4 + i;
        S.ampl[v] = ampl[i];
        S.freq[v] = freq[i];
        S.oct[v] = (octave[i >> 1] >> (i & 1 ? 4 : 0)) & 7;
    }
}

// ---- the AdLib track for a placeholder PCjr song, and both players --------------

static bool prefer_ad(uint32 wa_size, uint32 ad_size) { return wa_size * 8 <= ad_size; }

class MusicMix : public Scumm::MusicEngine {
public:
    MusicMix(Scumm::ScummEngine *vm, Audio::Mixer *mixer) : _vm(vm) {
        _pcjr = new Scumm::Player_V2(vm, mixer, true);
        _cms = new Scumm::Player_V2CMS(vm, mixer);
    }
    ~MusicMix() override {
        delete _pcjr;
        delete _cms;
    }
    void setMusicVolume(int vol) override {
        _pcjr->setMusicVolume(vol);
        _cms->setMusicVolume(vol);
    }
    // One driver had one song at a time: a new song replaced the last. Here a
    // song plays on either player, so one starting stops the other's. (MI1
    // starts the lookout's theme, an AD track, and never stops it: "Part
    // One", on the PCjr, was what replaced it.) A PCjr sound effect plays
    // over a CMS song.
    void startSound(int nr) override {
        // an AD track as readSoundResourceSmallHeader loads it for us: "AD", then data[6] = $80
        const byte *d = _vm->getResourceAddress(Scumm::rtSound, nr);
        if (d && d[2] == 'A' && d[3] == 'D' && d[6] == 0x80) {
            if (_pcjrSong && _pcjr->getSoundStatus(_pcjrSong)) _pcjr->stopSound(_pcjrSong);
            _pcjrSong = 0;
            _cmsSong = nr;
            _cms->startSound(nr);
        } else {
            const int header = (_vm->_game.features & Scumm::GF_OLD_BUNDLE) ? 4 : 6;
            if (d && d[header] >= SONG_PRIORITY) {
                if (_cmsSong && _cms->getSoundStatus(_cmsSong)) _cms->stopSound(_cmsSong);
                _cmsSong = 0;
                _pcjrSong = nr;
            }
            _pcjr->startSound(nr);
        }
    }
    void stopSound(int nr) override {
        _pcjr->stopSound(nr);
        _cms->stopSound(nr);
    }
    void stopAllSounds() override {
        _pcjr->stopAllSounds();
        _cms->stopAllSounds();
    }
    int getSoundStatus(int nr) const override {
        return _pcjr->getSoundStatus(nr) || _cms->getSoundStatus(nr);
    }
    int getMusicTimer() override {
        return _cmsSong && _cms->getSoundStatus(_cmsSong) ? _cms->getMusicTimer() : _pcjr->getMusicTimer();
    }

private:
    Scumm::ScummEngine *_vm;
    Scumm::Player_V2 *_pcjr;
    Scumm::Player_V2CMS *_cms;
    int _cmsSong = 0, _pcjrSong = 0;    // the song each last started
};

static Scumm::MusicEngine *make_engine(Scumm::ScummEngine *vm, Audio::Mixer *mixer) {
    return new MusicMix(vm, mixer);
}

void psgmix_install(void) {
    reset();
    Scumm::g_movieCartPCjrTap = pcjr_tap;
    Scumm::g_movieCartCMSTap = cms_tap;
    Scumm::g_movieCartPreferAD = prefer_ad;
    Scumm::g_movieCartMusicEngine = make_engine;
}

// ---- each TV frame -------------------------------------------------------------------

// an SAA1099 amplitude (0-15, linear) as the PSG's attenuation (2 dB
// steps), 6 dB down: the CMS songs' voices sit near full amplitude, and the
// PCjr arrangements around 6 dB below it (MI1, measured on the Mac test)
static const uint8_t AMP_ATT[16] = {15, 15, 11, 9, 8, 7, 6, 5, 5, 4, 4, 4, 3, 3, 3, 3};

// a CMS voice's tone as a PSG period. The SAA1099 plays (clock / 512 <<
// octave) / (511 - freq) Hz; the Game Blaster's clock was 7.159 MHz, twice
// the PSG's (ScummVM's emulator takes 8 MHz, two semitones sharp of the
// AdLib version), and the PSG plays clock / 32 / period, so the period is
// 8 (511 - freq) >> octave. Below the PSG's lowest (109 Hz), octaves up.
static int cms_period(int v) {
    uint32_t p = ((8u * (511u - S.freq[v])) + (1u << S.oct[v] >> 1)) >> S.oct[v];
    if (p < 1) p = 1;
    if (p > 1023) {
        psgmix_stats.bass_raised++;
        while (p > 1023) p = (p + 1) / 2;
    }
    return (int)p;
}

static int loudness(int v) {
    int l = S.ampl[v] & 15, r = S.ampl[v] >> 4;
    return l > r ? l : r;
}

int psgmix_frame(uint8_t *out) {
    psgmix_stats.frames++;
    if (S.seen) {
        S.seen = false;
        S.stale = 0;
    } else if (++S.stale >= PCJR_STALE) {
        for (int c = 0; c < 4; c++) S.on[c] = false;
    }

    int period[3], att[4];
    bool free_ch[3];
    int nfree = 0;
    for (int c = 0; c < 3; c++) {
        free_ch[c] = !S.on[c];
        if (free_ch[c]) {
            nfree++;
            att[c] = 15;
            period[c] = S.sent_period[c] >= 0 ? S.sent_period[c] : 0;
        } else {
            att[c] = S.att[c];
            period[c] = S.period[c];
            S.voice[c] = -1;
        }
    }
    att[3] = S.on[3] ? S.att[3] : 15;

    // the CMS voices, loudest first; one already on a channel counts a little
    // louder, so a voice holds its channel against one as loud
    int order[8], score[8], n = 0;
    bool kept[8] = {false};
    for (int c = 0; c < 3; c++)
        if (free_ch[c] && S.voice[c] >= 0 && loudness(S.voice[c])) kept[S.voice[c]] = true;
    for (int v = 0; v < 8; v++) {
        int a = loudness(v);
        if (!a) continue;
        score[v] = a * 2 + (kept[v] ? 3 : 0);
        int i = n++;
        while (i > 0 && score[order[i - 1]] < score[v]) {
            order[i] = order[i - 1];
            i--;
        }
        order[i] = v;
    }
    // a voice on the pitch of a louder one (the driver doubles notes) is left out
    int per[8];
    for (int i = 0; i < n; i++) per[order[i]] = cms_period(order[i]);
    int m = 0;
    for (int i = 0; i < n; i++) {
        bool dup = false;
        for (int j = 0; j < m; j++)
            if (per[order[j]] == per[order[i]]) dup = true;
        if (!dup) order[m++] = order[i];
    }
    n = m;
    if (n) psgmix_stats.cms_frames++;
    if (n > nfree) psgmix_stats.cms_voices_dropped += (uint32_t)(n - nfree);
    bool picked[8] = {false};
    for (int i = 0; i < n && i < nfree; i++) picked[order[i]] = true;
    // the picked voices keep their channels; the rest take the free ones left
    for (int c = 0; c < 3; c++)
        if (free_ch[c] && (S.voice[c] < 0 || !picked[S.voice[c]])) S.voice[c] = -1;
    for (int i = 0; i < n && i < nfree; i++) {
        int v = order[i];
        bool placed = false;
        for (int c = 0; c < 3; c++)
            if (free_ch[c] && S.voice[c] == v) placed = true;
        for (int c = 0; c < 3 && !placed; c++)
            if (free_ch[c] && S.voice[c] < 0) {
                S.voice[c] = v;
                placed = true;
            }
    }
    for (int c = 0; c < 3; c++)
        if (free_ch[c] && S.voice[c] >= 0) {
            period[c] = per[S.voice[c]];
            att[c] = AMP_ATT[loudness(S.voice[c])];
        }

    // what changed, as PSG bytes: periods first, then the noise, then the attenuations
    int k = 0;
    for (int c = 0; c < 3; c++)
        if (att[c] < 15 && period[c] != S.sent_period[c]) {
            out[k++] = (uint8_t)(0x80 | c << 5 | (period[c] & 15));
            out[k++] = (uint8_t)(period[c] >> 4);
            S.sent_period[c] = period[c];
        }
    if (S.on[3] && S.noise != S.sent_noise) {
        out[k++] = (uint8_t)(0xE0 | S.noise);
        S.sent_noise = S.noise;
    }
    for (int c = 0; c < 4; c++)
        if (att[c] != S.sent_att[c]) {
            out[k++] = (uint8_t)(0x90 | c << 5 | att[c]);
            S.sent_att[c] = att[c];
        }
    return k;
}
