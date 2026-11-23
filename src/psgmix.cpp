// psgmix.cpp: see psgmix.h.
#include "psgmix.h"

#include <math.h>
#include <string.h>

#include "audio/audiodev/opl.h"
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
    // which CMS voice (or OPL channel) each PSG tone channel plays (-1: none)
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

// ---- the AdLib driver's OPL2, as its registers (SCUMM v5: iMUSE) ---------------------
//
// Fate of Atlantis and Monkey Island 2 have no PCjr or CMS music. Their iMUSE
// plays the AdLib arrangement on ScummVM's AdLib driver (audio/adlib.cpp),
// whose OPL2 is this: no FM synthesis, only the registers (OPL::g_movieCartOPL
// makes it in place of an emulator). Each of the 9 channels' carrier (and its
// modulator, when the two are added) has its envelope followed in dB as the
// chip would run it, from the samples the mixer asks for. Each TV frame, as
// with the CMS voices: the highest and the lowest of the loud channels (the
// tune and the bass, as a rule), then the loudest of the rest, go to the
// PSG's tone channels, and the loudest noisy one (a strong modulator with
// high feedback: drums, cymbals) to its noise channel. Measured against
// ScummVM's AdLib emulator on FOA's first two minutes (the Mac test): chroma
// correlation 0.86, against 0.27 for the same sound 5 s off.

#define OPL_SILENT 96.0f
#define OPL_ATT0   (-1)     // a channel at full level: this PSG attenuation (FOA's music is then about as loud as MI1's)
#define OPL_GONE   30.0f    // dB: a modulator this far down barely changes the carrier's tone
#define OPL_NOISE  6.0f     // dB: a modulator this strong, with feedback 6 or 7, makes a noise
#define OPL_SPAN   4        // the top and bottom voices within this much (2 dB steps) of the loudest are kept

enum { EG_OFF, EG_ATTACK, EG_DECAY, EG_SUSTAIN, EG_RELEASE };

// a channel's modulator slot (its carrier is 3 on)
static const uint8_t OPL_SLOT[9] = {0, 1, 2, 8, 9, 10, 16, 17, 18};
// MULT, in halves of the note's frequency
static const uint8_t OPL_MULT2[16] = {1, 2, 4, 6, 8, 10, 12, 14, 16, 18, 20, 20, 24, 24, 30, 30};
// key scaling at 6 dB an octave, block 7, by the top 4 bits of the frequency number
static const float OPL_KSL[16] = {0, 9, 12, 13.875f, 15, 16.125f, 16.875f, 17.625f,
                                  18, 18.75f, 19.125f, 19.5f, 19.875f, 20.25f, 20.625f, 21};
static const float OPL_KSL_SCALE[4] = {0, 0.5f, 0.25f, 1};

static struct {
    bool live;              // the AdLib driver opened the tap
    uint8_t reg[256];
    float env[22];          // each slot's envelope, dB down
    uint8_t eg[22];
    uint32_t rate;          // the mixer's
    // each channel as the PSG would play it: now, the loudest since the last
    // TV frame (a drum hit can be shorter than a frame), and for this frame
    struct voice { int loud, period, att; float pitch; bool noisy; } now[9], peak[9], v[9];
} O;

static void opl_reset(void) {
    memset(&O, 0, sizeof O);
    for (int i = 0; i < 22; i++) O.env[i] = OPL_SILENT;
}

static void opl_key(int ch, bool on) {
    for (int k = 0; k < 2; k++) {
        const int s = OPL_SLOT[ch] + 3 * k;
        if (on) O.eg[s] = EG_ATTACK;                    // from where it is, as the chip does
        else if (O.eg[s] != EG_OFF) O.eg[s] = EG_RELEASE;
    }
}

void (*psgmix_opl_log)(int reg, int val);
void (*psgmix_voice_log)(int ch, int voice, float pitch, int period, int att);

static void opl_write(int r, int v) {
    r &= 0xFF;
    if (psgmix_opl_log) psgmix_opl_log(r, v);
    if (r >= 0xB0 && r <= 0xB8) {
        const bool was = O.reg[r] & 0x20, now = v & 0x20;
        O.reg[r] = (uint8_t)v;
        if (now != was) opl_key(r - 0xB0, now);
        return;
    }
    O.reg[r] = (uint8_t)v;
}

// a rate (0-15) with key scaling as the chip's 0-63; 0 stays 0 (no change)
static int opl_rate(int r4, int s, int ch) {
    if (!r4) return 0;
    const int fnum = O.reg[0xA0 + ch] | (O.reg[0xB0 + ch] & 3) << 8, block = (O.reg[0xB0 + ch] >> 2) & 7;
    const int kc = block * 2 + ((fnum >> ((O.reg[8] & 0x40) ? 8 : 9)) & 1);
    const int p = 4 * r4 + ((O.reg[0x20 + s] & 0x10) ? kc : kc >> 2);
    return p > 63 ? 63 : p;
}

// ms for the whole 96 dB (the YM3812's tables: rate 1 is 39.28 s to decay,
// 2.83 s to attack, each step twice as fast, and a quarter step for each key
// scaling step); at 60 and up, 2.4 ms to decay and no attack
static float opl_ms(int p, float rate1) {
    return rate1 * 4 / (float)((4 + (p & 3)) << ((p >> 2) - 1));
}

static void opl_step(int ch, int s, float ms) {
    float &e = O.env[s];
    switch (O.eg[s]) {
    case EG_ATTACK: {
        const int p = opl_rate(O.reg[0x60 + s] >> 4, s, ch);
        if (!p) break;
        // the attack falls in dB as an exponential: most of it at once
        if (p >= 60) e = 0;
        else e *= expf(-ms * 4.85f / opl_ms(p, 2826.24f));
        if (e < 0.1f) {
            e = 0;
            O.eg[s] = EG_DECAY;
        }
        break;
    }
    case EG_DECAY: {
        const int sl = O.reg[0x80 + s] >> 4;
        const float sl_db = sl == 15 ? 93.0f : sl * 3.0f;
        const int p = opl_rate(O.reg[0x60 + s] & 15, s, ch);
        if (p) e += 96 * ms / (p >= 60 ? 2.4f : opl_ms(p, 39280.64f));
        if (e >= sl_db) {
            e = sl_db;
            O.eg[s] = (O.reg[0x20 + s] & 0x20) ? EG_SUSTAIN : EG_RELEASE;   // held, or it goes on falling
        }
        break;
    }
    case EG_RELEASE: {
        const int p = opl_rate(O.reg[0x80 + s] & 15, s, ch);
        if (p) e += 96 * ms / (p >= 60 ? 2.4f : opl_ms(p, 39280.64f));
        if (e >= OPL_SILENT) {
            e = OPL_SILENT;
            O.eg[s] = EG_OFF;
        }
        break;
    }
    default:
        break;
    }
}

static void opl_voices(void);

// the mixer asked for this many samples: the envelopes move on, and the channels
// are looked at (the AdLib driver's timer runs between these, at 250 Hz)
static void opl_advance(int samples) {
    if (!O.rate) return;
    const float ms = samples * 1000.0f / (float)O.rate;
    for (int ch = 0; ch < 9; ch++)
        for (int k = 0; k < 2; k++) {
            const int s = OPL_SLOT[ch] + 3 * k;
            if (O.eg[s] != EG_OFF) opl_step(ch, s, ms);
        }
    opl_voices();
}

// a slot's level, dB down: total level and key scaling (and the envelope: opl_level)
static float opl_tl(int ch, int s) {
    const int fnum = O.reg[0xA0 + ch] | (O.reg[0xB0 + ch] & 3) << 8, block = (O.reg[0xB0 + ch] >> 2) & 7;
    float ksl = OPL_KSL[fnum >> 6] - 6.0f * (7 - block);
    if (ksl < 0) ksl = 0;
    return (O.reg[0x40 + s] & 63) * 0.75f + ksl * OPL_KSL_SCALE[O.reg[0x40 + s] >> 6];
}

static float opl_level(int ch, int s) { return O.env[s] + opl_tl(ch, s); }

static int gcd(int a, int b) {
    while (b) {
        const int t = a % b;
        a = b;
        b = t;
    }
    return a;
}

// each channel as the PSG would play it now (O.now): loudness 0 (silent) -
// 15, attenuation, period (raised by octaves into the PSG's range), pitch (the
// period before that: lower is higher), and whether it's a noise
static void opl_voices(void) {
    for (int ch = 0; ch < 9; ch++) {
        auto &o = O.now[ch];
        const int mod = OPL_SLOT[ch], car = mod + 3;
        const bool added = O.reg[0xC0 + ch] & 1;
        const float lc = opl_level(ch, car), lm = opl_level(ch, mod);
        const float db = added && lm < lc ? lm : lc;
        int att = OPL_ATT0 + (int)(db * 0.5f + 0.5f);
        if (att < 0) att = 0;
        if (att > 15) att = 15;
        o.att = att;
        o.loud = 15 - att;
        o.noisy = false;
        if (!o.loud) continue;
        // the pitch: the note times the carrier's MULT, or with a modulator
        // that changes the tone, their common factor (the sidebands' spacing)
        const int fnum = O.reg[0xA0 + ch] | (O.reg[0xB0 + ch] & 3) << 8, block = (O.reg[0xB0 + ch] >> 2) & 7;
        const int mc = OPL_MULT2[O.reg[0x20 + car] & 15], mm = OPL_MULT2[O.reg[0x20 + mod] & 15];
        int m = mc;
        if (added) m = lm < lc ? mm : mc;
        else if (lm < OPL_GONE) m = gcd(mc, mm);
        // strong feedback on a modulator set strong: a noise, not a note (the
        // patch, whatever its modulator's envelope does)
        o.noisy = !added && ((O.reg[0xC0 + ch] >> 1) & 7) >= 6 && opl_tl(ch, mod) < OPL_NOISE;
        // Hz = fnum * 49716 / 2^(20 - block) * m / 2; the PSG's period = 3579545 / 32 / Hz
        const float hz = fnum * 49715.9f / (float)(1 << (20 - block)) * m * 0.5f;
        float p = hz > 0 ? 111860.8f / hz : 1023;
        o.pitch = p;
        while (p > 1023) p *= 0.5f;
        o.period = p < 1 ? 1 : (int)(p + 0.5f);
    }
    for (int ch = 0; ch < 9; ch++)
        if (O.now[ch].loud > O.peak[ch].loud) O.peak[ch] = O.now[ch];
}

// this TV frame's channels (O.v): each the loudest it was since the last
static void opl_frame(void) {
    for (int ch = 0; ch < 9; ch++) {
        O.v[ch] = O.peak[ch];
        O.peak[ch] = O.now[ch];
        if (O.v[ch].loud && O.v[ch].pitch > 1023) psgmix_stats.bass_raised++;
    }
}

class TapOPL : public OPL::EmulatedOPL {
public:
    bool init() override {
        opl_reset();
        O.live = true;
        O.rate = (uint32_t)getRate();
        return true;
    }
    void reset() override { init(); }
    void write(int a, int v) override {
        if (a & 1) opl_write(_addr, v);
        else _addr = v;
    }
    byte read(int) override { return 0; }
    void writeReg(int r, int v) override { opl_write(r, v); }
    uint getChannels() const override { return 1; }
    ~TapOPL() override { O.live = false; }

protected:
    void generateSamples(int16 *buffer, int length) override {
        memset(buffer, 0, (size_t)length * sizeof *buffer);
        opl_advance(length);
    }

private:
    int _addr = 0;
};

static OPL::OPL *make_opl(OPL::Config::OplType type) {
    return type == OPL::Config::kOpl2 ? new TapOPL() : 0;
}

// the tone channels psgmix has (psgmix_tones); the others, parked, are the DAC
static int tones_free = 3;
static bool dac_parked;
// the tone and the noise under the speech this many 2 dB steps quieter
#define SPEECH_DUCK 5

void psgmix_tones(int n) { tones_free = n < 0 ? 0 : n > 3 ? 3 : n; }

void psgmix_install(void) {
    reset();
    tones_free = 3;
    dac_parked = false;
    Scumm::g_movieCartPCjrTap = pcjr_tap;
    Scumm::g_movieCartCMSTap = cms_tap;
    Scumm::g_movieCartPreferAD = prefer_ad;
    Scumm::g_movieCartMusicEngine = make_engine;
    opl_reset();
    OPL::g_movieCartOPL = make_opl;
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

// the voices the PCjr's channels leave free: the CMS player's 8, or the OPL's 9
// channels. Their loudness 0 (silent) - 15, PSG period and attenuation.
static int voice_loud(int v) { return O.live ? O.v[v].loud : loudness(v); }
static int voice_period(int v) { return O.live ? O.v[v].period : cms_period(v); }
static int voice_att(int v) { return O.live ? O.v[v].att : AMP_ATT[loudness(v)]; }

int psgmix_opl(void) { return O.live; }

int psgmix_frame(uint8_t *out) {
    psgmix_stats.frames++;
    if (S.seen) {
        S.seen = false;
        S.stale = 0;
    } else if (++S.stale >= PCJR_STALE) {
        for (int c = 0; c < 4; c++) S.on[c] = false;
    }
    const int nv = O.live ? 9 : 8;
    if (O.live) opl_frame();

    int period[3], att[4];
    bool free_ch[3];
    int nfree = 0;
    // speech (speechdac.h): tones 1 and 2 are the DAC, the music has tone 0
    // and the noise, a little quieter; or (tones_free 0) all three tones are
    // the DAC and the music has nothing. After it, all of the PSG again,
    // every register sent anew
    const bool dac = tones_free < 3;
    for (int c = 0; c < 3; c++) {
        if (dac && c >= tones_free) {
            free_ch[c] = false;
            att[c] = 15;
            period[c] = 0;
            S.voice[c] = -1;
            continue;
        }
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
    bool noise_on = S.on[3];
    int noise = S.noise;

    // the OPL's loudest noisy channel on the noise channel (the PCjr's own first)
    if (O.live && !S.on[3] && !(dac && !tones_free)) {
        int best = -1;
        for (int v = 0; v < 9; v++)
            if (O.v[v].noisy && O.v[v].loud && (best < 0 || O.v[v].loud > O.v[best].loud)) best = v;
        if (best >= 0) {
            noise_on = true;
            att[3] = O.v[best].att;
            // white noise; its rate by the channel's pitch
            noise = 4 | (O.v[best].period < 112 ? 0 : O.v[best].period < 373 ? 1 : 2);
            psgmix_stats.opl_noise_frames++;
        }
    }

    // the voices, loudest first; one already on a channel counts a little
    // louder, so a voice holds its channel against one as loud
    int order[9], score[9], n = 0;
    bool kept[9] = {false};
    for (int c = 0; c < 3; c++)
        if (free_ch[c] && S.voice[c] >= 0 && voice_loud(S.voice[c])) kept[S.voice[c]] = true;
    for (int v = 0; v < nv; v++) {
        int a = voice_loud(v);
        if (!a || (O.live && O.v[v].noisy)) continue;   // (a noise is never a tone)
        score[v] = a * 2 + (kept[v] ? 3 : 0);
        int i = n++;
        while (i > 0 && score[order[i - 1]] < score[v]) {
            order[i] = order[i - 1];
            i--;
        }
        order[i] = v;
    }
    // a voice on the pitch of a louder one (the driver doubles notes) is left
    // out; the OPL's doubles are often detuned a little: within 3 %
    int per[9];
    for (int i = 0; i < n; i++) per[order[i]] = voice_period(order[i]);
    int m = 0;
    for (int i = 0; i < n; i++) {
        bool dup = false;
        for (int j = 0; j < m; j++) {
            const int a = per[order[j]], b = per[order[i]];
            if (a == b || (O.live && 32 * (a > b ? a - b : b - a) <= a)) dup = true;
        }
        if (!dup) order[m++] = order[i];
    }
    n = m;
    // more OPL voices than channels: the top and the bottom of the loud ones
    // (a tune and its bass, as a rule) come first, then the rest by loudness
    if (O.live && n > nfree && nfree >= 1) {      // (one channel, under speech: the top)
        int top = -1, bottom = -1, loudest = 0;
        for (int i = 0; i < n; i++)
            if (voice_loud(order[i]) > loudest) loudest = voice_loud(order[i]);
        for (int i = 0; i < n; i++) {
            const int v = order[i];
            if (voice_loud(v) < loudest - OPL_SPAN) continue;
            if (top < 0 || O.v[v].pitch < O.v[order[top]].pitch) top = i;
            if (bottom < 0 || O.v[v].pitch > O.v[order[bottom]].pitch) bottom = i;
        }
        int first[2] = {order[top], order[bottom]}, k = 0;
        int rest[9];
        for (int i = 0; i < n; i++)
            if (i != top && i != bottom) rest[k++] = order[i];
        m = 0;
        order[m++] = first[0];
        if (bottom != top) order[m++] = first[1];
        for (int i = 0; i < k; i++) order[m++] = rest[i];
    }
    if (n) {
        if (O.live) psgmix_stats.opl_frames++;
        else psgmix_stats.cms_frames++;
    }
    if (n > nfree) {
        if (O.live) psgmix_stats.opl_voices_dropped += (uint32_t)(n - nfree);
        else psgmix_stats.cms_voices_dropped += (uint32_t)(n - nfree);
    }
    bool picked[9] = {false};
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
            att[c] = voice_att(S.voice[c]);
            if (psgmix_voice_log && O.live) psgmix_voice_log(c, S.voice[c], O.v[S.voice[c]].pitch, period[c], att[c]);
        }

    if (dac) {
        if (!tones_free) noise_on = false, att[3] = 15;
        for (int c = 0; c < 4; c++)
            if ((c < tones_free || c == 3) && att[c] < 15) att[c] = att[c] + SPEECH_DUCK < 15 ? att[c] + SPEECH_DUCK : 15;
    }

    // what changed, as PSG bytes: periods first, then the noise, then the
    // attenuations. The DAC's tones parked first, when it comes; after it,
    // theirs are all sent again.
    int k = 0;
    if (dac && !dac_parked) {
        for (int c = tones_free; c < 3; c++) {
            out[k++] = (uint8_t)(0x80 | c << 5 | 1);
            out[k++] = 0;
            S.sent_period[c] = 1;
            S.sent_att[c] = -1;
        }
        dac_parked = true;
    } else if (!dac && dac_parked) {
        for (int c = 0; c < 3; c++) {
            S.sent_period[c] = -1;
            S.sent_att[c] = -1;
        }
        dac_parked = false;
    }
    for (int c = 0; c < 3; c++)
        if (dac && c >= tones_free) continue;
        else if (att[c] < 15 && period[c] != S.sent_period[c]) {
            out[k++] = (uint8_t)(0x80 | c << 5 | (period[c] & 15));
            out[k++] = (uint8_t)(period[c] >> 4);
            S.sent_period[c] = period[c];
        }
    if (noise_on && noise != S.sent_noise) {
        out[k++] = (uint8_t)(0xE0 | noise);
        S.sent_noise = noise;
    }
    for (int c = 0; c < 4; c++)
        if (dac && c >= tones_free && c < 3) continue;
        else if (att[c] != S.sent_att[c]) {
            out[k++] = (uint8_t)(0x90 | c << 5 | att[c]);
            S.sent_att[c] = att[c];
        }
    return k;
}
