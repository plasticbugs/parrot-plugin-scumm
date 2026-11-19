// scumm -- SCUMM games on the ColecoVision (Monkey Island first): pick a
// game's 000.LFL (or MONKEY2.000) in the menu, press A, and the cart runs
// ScummVM's SCUMM engine (vendor/frank-quest) and sends the console what it
// shows and plays: each picture through scummcv (the colours, a 256 x 192
// window at 1:1) and nescv (cells and the Game Boy kernel's lists), the
// PCjr music and sound as SN76489 bytes in the same lists.
//
//   core 1  serves the kernel and a list a TV frame (cvframes.c)
//   core 0  the engine, on a stack of its own; every call it makes to the
//           OSystem below also serves the console (service(): the next
//           list each TV frame, the engine's timers, the mixer)
//
// The engine runs from flash (XIP); its heap is the PSRAM (heap.c); the
// files are read and the saves written with FatFs (diskio.c).
//
// The controller: the joystick moves the pointer (faster as it's held),
// left fire = the left button, right fire = the right; * = Esc (skip a
// cutscene), 0 = . (skip a line); 7 held a second saves (slot 1), 9 held a
// second loads it; # held half a second opens the save and load screen
// (savemenu.h: ten slots, 0 the autosave, made every AUTOSAVE_S); 4 and 6
// nudge the room's window left and right (held,
// they repeat) and it stays there; 5 centres it and at once lets it follow
// again (the pointer, a cutscene, an actor's message). RESET goes back to the menu.
//
// The USB port: plugged into a computer, the cart is its MIDI device,
// "MovieCart MIDI", and the game's MT-32 music goes there as MIDI (for Munt,
// say; ../mac/cvmidi.c routes it); the console's PSG plays none. Otherwise
// (a hub, or nothing) it's a host for a USB keyboard and mouse.
#include <cstdio>
#include <cstdlib>
#include <cctype>
#include <cstring>

#include "pico/stdlib.h"
#include "hardware/watchdog.h"

extern "C" {
#include "cvframes.h"
#include "cvpsram.h"
#include "cvxip.h"
#include "cvdiag.h"
#include "vdp.h"
#include "nescv.h"
#include "scummcv.h"
#include "psgmix.h"
#include "savemenu.h"
#include "usbinput.h"
#include "usbmidi_dev.h"
#include "frank_quest_fs.h"
bool cvsd_card_in(void);
void heap_init(void);
uint32_t heap_used(void);
}
#include "gbkernel.h"
#include "handoff.h"
#include "scummconf.h"

#include "common/scummsys.h"
#include "common/system.h"
#include "common/events.h"
#include "common/config-manager.h"
#include "common/fs.h"
#include "common/archive.h"
#include "common/textconsole.h"
#include "graphics/surface.h"
#include "graphics/palette.h"
#include "backends/events/default/default-events.h"
#include "backends/timer/default/default-timer.h"
#include "backends/saves/default/default-saves.h"
#include "backends/fs/rp2350/rp2350-fs-factory.h"
#include "backends/audiocd/rp2350/rp2350-audiocd.h"
#include "audio/mixer_intern.h"
#include "audio/mpu401.h"
#include "audio/musicplugin.h"
#include "base/plugins.h"
#include "engines/engine.h"
#include "engines/metaengine.h"
#include "scumm/scumm.h"
#include "scumm/players/player_v2.h"

namespace Scumm { extern Common::Rect g_movieCartMsgRect; }   // string.cpp (MovieCart)

#define OPEN_US      20000          // serve the console this long before the card's (blocking) start
#define WATCHDOG_MS  8000           // a hang reboots into the menu (a room's files load in well under)
#define MIX_RATE     11025          // the mixer runs (Player_V2 plays in it) and its output is dropped
#define STACK_BYTES  (128 * 1024)   // the engine's: 19 KB on the Mac (test/run.sh); the menu note's "s..K" says the cart's
#define STACK_GUARD  1024           // MSPLIM this far above its bottom: an overflow faults (cvdiag.h) with room to report it
#define HOLD_US      1000000        // keypad 7 / 9 held this long: save / load
#define MENU_HOLD_US 500000         // keypad # held this long: the save and load screen
#define AUTOSAVE_S   300            // the engine saves to slot 0 this often
#define SAVE_TARGET  "cart"         // the game domain: its saves are cart.s00 - cart.s09 in its folder
#define NUDGE        8              // keypad 4 / 6: the room's window moves this far
#define NUDGE_HOLD_US 300000        // and held, again after this
#define NUDGE_REP_US 70000          // and every this
#define SPLIT        144            // SCUMM v3-v5: the verb area starts here, as a rule (split() has where it is now)
#define STILL_US     500000         // the pointer this still: the window holds an actor's message
#define QUEUE        32             // events waiting for the engine
#define INPUT_US     4000           // the controller (and USB's) read this often (the console sends it each TV frame)
#define NOTE_US      2000000        // the menu's note, should RESET come, this often
#define MOUSE_SCALE  64             // a USB mouse's count: this many 256ths of a pixel (128 was too quick, the user said)
#define MIDI_WAIT_US 1500000        // a computer on the USB port configures the MIDI device in this (macOS: ~0.3 s)
#define MIDI_ASK_US  700000         // a computer there, the device not taken yet: it's asking its user (the TV says so)
#define MIDI_ALLOW_US 60000000      // and that user has this long to allow it (or fire goes on without)

// ---- the console, a list a TV frame (as ../../nes/plugin/nes.cpp) ------------------

static nescv_t enc;
static uint8_t list[NESCV_LIST_MAX];
static size_t list_len;
static bool list_pending;
static uint32_t seen_frames;
static uint64_t pic_index;
static uint8_t ident_lut[256];
// the pointer: two sprites (scummcv_cursor), placed each TV frame in the
// window of the picture the list shows (each picture's window, by index)
static void (*pointer_hook)(void);
static scummcv_view_t pic_views[16];
static char game_name[33], note[33];
static bool xip_quad;                   // cvxip_fast() switched the flash to quad reads
static bool usb_midi;                   // the USB port is a MIDI device, a computer on it (usbmidi_dev.h)

static void publish(void) {
    if (list_pending && cvf_publish(list, list_len)) list_pending = false;
}

static bool new_frame(void) {
    uint32_t t = time_us_32();
    cvf_task(t);
    watchdog_update();
    publish();
    if (list_pending) return false;
    uint32_t f = cvf_frames();
    if (f == seen_frames) return false;
    seen_frames = f;
    return true;
}

// ---- the sound: psgmix (../psgmix.h) turns the engine's players into PSG
// bytes, a TV frame's at a time

static void send_frame(void) {
    uint8_t b[16];
    int n = psgmix_frame(b);
    if (pointer_hook) pointer_hook();
    list_len = nescv_tv_frame(&enc, b, n, list);
    list_pending = true;
    publish();
}

// ---- messages before (or instead of) the game: a canvas as the picture --------------

static canvas_t msg_canvas;
static bool msg_shown, msg_waiting;

static void offer_msg(void) {
    nescv_picture_t p;
    p.pixels = &msg_canvas.px[0][0];
    p.pitch = VDP_W;
    p.lut = ident_lut;
    for (int b = 0; b < NESCV_ROWS; b++) {
        p.line[b] = (uint8_t)(8 * b);
        p.col[b] = 0;
    }
    p.backdrop = C_BLACK;
    msg_waiting = !nescv_offer(&enc, &p, ++pic_index);
}

static void centre(int y, const char *s, uint8_t colour, int scale) {
    int w = (int)strlen(s) * 8 * scale;
    canvas_text(&msg_canvas, w < VDP_W ? (VDP_W - w) / 2 : 0, y, s, colour, scale);
}

static void message(const char *head, const char *l1, const char *l2) {
    canvas_init(&msg_canvas);
    centre(48, "SCUMM", C_LYELLOW, 3);
    if (game_name[0]) centre(88, game_name, C_WHITE, 1);
    if (head) centre(112, head, C_LYELLOW, 1);
    if (l1) centre(128, l1, C_GRAY, 1);
    if (l2) centre(140, l2, C_GRAY, 1);
    centre(176, "RESET: BACK TO THE MENU", C_DBLUE, 1);
    msg_shown = true;
    offer_msg();
}

static void serve_for(uint32_t us) {
    uint32_t t0 = time_us_32();
    while (!us || time_us_32() - t0 < us) {
        if (!new_frame()) continue;
        if (msg_waiting) offer_msg();
        send_frame();
    }
}

// while a file loads (diskio.c, each sector): the console's frames go on,
// and the watchdog is fed; not from inside service() (the engine's own)
static bool in_service;
extern "C" void diskio_wait(void) {
    static bool busy;
    if (busy || in_service) return;
    busy = true;
    if (new_frame()) {
        if (msg_waiting) offer_msg();
        send_frame();
    }
    busy = false;
}

static void __attribute__((noreturn)) fail(const char *why, const char *more, const char *menu_note) {
    message("IT CAN'T RUN", why, more);
    if (menu_note) cvf_note(menu_note);
    serve_for(0);
    for (;;) tight_loop_contents();
}

// ---- a USB keyboard's keys (HID usages) as ScummVM's -------------------------------

static bool hid_key(uint8_t u, bool shift, Common::KeyCode *kc, uint16 *ascii) {
	static const char punct[] = "-=[]\\#;'`,./", punct_sh[] = "_+{}|~:\"~<>?";
	*ascii = 0;
	if (u >= 0x04 && u <= 0x1D) {                       // a-z
		*kc = (Common::KeyCode)(Common::KEYCODE_a + (u - 0x04));
		*ascii = (uint16)((shift ? 'A' : 'a') + (u - 0x04));
	} else if (u >= 0x1E && u <= 0x27) {                // 1-9, 0
		static const char sh[] = "!@#$%^&*()";
		int d = u == 0x27 ? 0 : u - 0x1D;
		*kc = (Common::KeyCode)(Common::KEYCODE_0 + d);
		*ascii = (uint16)(shift ? sh[u - 0x1E] : '0' + d);
	} else if (u >= 0x2D && u <= 0x38) {                // - = [ ] \ (#) ; ' ` , . /
		char c = punct[u - 0x2D];
		*kc = (Common::KeyCode)c;
		*ascii = (uint16)(shift ? punct_sh[u - 0x2D] : c);
	} else if (u >= 0x3A && u <= 0x45) {                // F1-F12
		*kc = (Common::KeyCode)(Common::KEYCODE_F1 + (u - 0x3A));
		*ascii = (uint16)(Common::ASCII_F1 + (u - 0x3A));
	} else {
		switch (u) {
		case 0x28: *kc = Common::KEYCODE_RETURN; *ascii = Common::ASCII_RETURN; break;
		case 0x29: *kc = Common::KEYCODE_ESCAPE; *ascii = Common::ASCII_ESCAPE; break;
		case 0x2A: *kc = Common::KEYCODE_BACKSPACE; *ascii = Common::ASCII_BACKSPACE; break;
		case 0x2B: *kc = Common::KEYCODE_TAB; *ascii = Common::ASCII_TAB; break;
		case 0x2C: *kc = Common::KEYCODE_SPACE; *ascii = ' '; break;
		case 0x4F: *kc = Common::KEYCODE_RIGHT; break;
		case 0x50: *kc = Common::KEYCODE_LEFT; break;
		case 0x51: *kc = Common::KEYCODE_DOWN; break;
		case 0x52: *kc = Common::KEYCODE_UP; break;
		default: return false;
		}
	}
	return true;
}

// ---- the OSystem ---------------------------------------------------------------------

class OSystem_Cart;
static OSystem_Cart *g_cart;

class OSystem_Cart : public OSystem, public Common::EventSource, public PaletteManager {
public:
	OSystem_Cart() { _fsFactory = &RP2350FilesystemFactory::instance(); }

	void initBackend() override {
		_eventManager = new DefaultEventManager(this);
		_timerManager = new DefaultTimerManager();
		_mixer = new Audio::MixerImpl(this, MIX_RATE);
		_mixer->setReady(true);
		_audiocdManager = new RP2350AudioCDManager();
		_savefileManager = new DefaultSaveFileManager(_saveDir.c_str());
		OSystem::initBackend();
	}
	void setSaveDir(const Common::String &d) { _saveDir = d; }

	// no settings file: what the cart sets, it sets each start
	Common::SeekableReadStream *createConfigReadStream() override { return 0; }
	Common::WriteStream *createConfigWriteStream() override { return 0; }

	bool hasFeature(Feature f) override { return f == kFeatureCursorPalette; }
	void setFeatureState(Feature f, bool enable) override {
		if (f == kFeatureCursorPalette) _cursorPalOn = enable;
	}
	bool getFeatureState(Feature f) override { return f == kFeatureCursorPalette && _cursorPalOn; }

	const GraphicsMode *getSupportedGraphicsModes() const override {
		static const GraphicsMode modes[] = {{"1x", "Normal", 1}, {0, 0, 0}};
		return modes;
	}
	int getDefaultGraphicsMode() const override { return 1; }
	bool setGraphicsMode(int) override { return true; }
	int getGraphicsMode() const override { return 1; }

	void initSize(uint w, uint h, const Graphics::PixelFormat *) override {
		if (w != SCUMMCV_W || h != SCUMMCV_H)
			warning("cart: initSize(%u, %u): only 320 x 200", w, h);
		_surface.resetWithoutOwnership(SCUMMCV_W, SCUMMCV_H, SCUMMCV_W, _screen, Graphics::PixelFormat::createFormatCLUT8());
		memset(_screen, 0, sizeof _screen);
	}
	int16 getHeight() override { return SCUMMCV_H; }
	int16 getWidth() override { return SCUMMCV_W; }
	PaletteManager *getPaletteManager() override { return this; }

	void setPalette(const byte *c, uint start, uint num) override {
		if (start + num > 256) num = 256 - start;
		memcpy(_pal + start * 3, c, num * 3);
		_palDirty = true;
	}
	void grabPalette(byte *c, uint start, uint num) override {
		if (start + num > 256) num = 256 - start;
		memcpy(c, _pal + start * 3, num * 3);
	}
	void setCursorPalette(const byte *, uint, uint) override { _cursorPalOn = true; }

	void copyRectToScreen(const void *buf, int pitch, int x, int y, int w, int h) override {
		const byte *src = (const byte *)buf;
		if (x < 0) { src -= x; w += x; x = 0; }
		if (y < 0) { src -= y * pitch; h += y; y = 0; }
		if (x + w > SCUMMCV_W) w = SCUMMCV_W - x;
		if (y + h > SCUMMCV_H) h = SCUMMCV_H - y;
		if (w <= 0 || h <= 0) return;
		for (int r = 0; r < h; r++)
			memcpy(_screen + (y + r) * SCUMMCV_W + x, src + r * pitch, w);
		_drawing = true;
	}
	Graphics::Surface *lockScreen() override { _drawing = true; return &_surface; }
	void unlockScreen() override {}
	void fillScreen(uint32 col) override { memset(_screen, (int)col, sizeof _screen); _drawing = true; }
	void setShakePos(int) override {}

	// a whole picture: offered now, or as soon as the console can take one
	// (unless the engine starts drawing again first)
	void updateScreen() override {
		_drawing = false;
		_ready = true;
		service();
		offer();
	}

	void showOverlay() override {}
	void hideOverlay() override {}
	Graphics::PixelFormat getOverlayFormat() const override { return Graphics::PixelFormat::createFormatCLUT8(); }
	void clearOverlay() override {}
	void grabOverlay(void *, int) override {}
	void copyRectToOverlay(const void *, int, int, int, int, int) override {}
	int16 getOverlayHeight() override { return SCUMMCV_H; }
	int16 getOverlayWidth() override { return SCUMMCV_W; }

	bool showMouse(bool visible) override {
		bool prev = _mouseVisible;
		_mouseVisible = visible;
		_ready = true;
		return prev;
	}
	void warpMouse(int x, int y) override {
		_mx = x << 8;
		_my = y << 8;
		_moved = true;
	}
	void setMouseCursor(const void *buf, uint w, uint h, int hotX, int hotY, uint32 key, bool,
	                    const Graphics::PixelFormat *) override {
		if (w > 32) w = 32;
		if (h > 32) h = 32;
		for (uint y = 0; y < h; y++)
			memcpy(_cursor + y * 32, (const byte *)buf + y * w, w);
		_cw = w; _ch = h; _hotX = hotX; _hotY = hotY; _key = (byte)key;
		if (scummcv_cursor(&_ptr, _cursor, (int)w, (int)h, 32, hotX, hotY, (uint8_t)key))
			_ptrPatDirty = true;
	}

	uint32 getMillis() override {
		service();
		return (uint32)(time_us_64() / 1000);
	}
	void delayMillis(uint ms) override {
		uint64_t end = time_us_64() + (uint64_t)ms * 1000;
		while (time_us_64() < end) service();
	}
	void getTimeAndDate(TimeDate &t) const override {
		uint32 s = (uint32)(time_us_64() / 1000000);
		t.tm_sec = s % 60; t.tm_min = s / 60 % 60; t.tm_hour = 12 + s / 3600 % 12;
		t.tm_mday = 1; t.tm_mon = 0; t.tm_year = 126; t.tm_wday = 4;
	}

	MutexRef createMutex() override { return (MutexRef)1; }
	void lockMutex(MutexRef) override {}
	void unlockMutex(MutexRef) override {}
	void deleteMutex(MutexRef) override {}

	Audio::Mixer *getMixer() override { return _mixer; }
	void quit() override { _quit = true; }
	void displayMessageOnOSD(const char *) override {}
	// error(): its text first (logMessage), then fatalError(), which would
	// exit(); instead the text is shown, on the TV and as the menu's note
	void logMessage(LogMessageType::Type type, const char *m) override {
		if (type == LogMessageType::kError) snprintf(_error, sizeof _error, "%s", m);
	}
	void fatalError() override {
		char l1[33], l2[33], n[33];
		size_t len = strcspn(_error, "\n");
		_error[len] = 0;
		snprintf(l1, sizeof l1, "%.32s", _error);
		snprintf(l2, sizeof l2, "%s", len > 32 ? _error + 32 : "");
		snprintf(n, sizeof n, "%.32s", _error);
		for (char *c = l1; *c; c++) *c = (char)toupper((unsigned char)*c);
		for (char *c = l2; *c; c++) *c = (char)toupper((unsigned char)*c);
		fail(l1, l2, n);
	}

	bool pollEvent(Common::Event &event) override;

	// every call from the engine: the console's next list, the timers, the mixer
	void service();
	bool quitting() const { return _quit; }
	// each TV frame, before the list: the pointer's sprites
	void pointer();
	void requestSaveLoad(int what) { _saveLoad = what; }

protected:
	Common::EventSource *getDefaultEventSource() { return this; }

private:
	void offer();
	void choose_view(scummcv_view_t *v);
	void input(uint32_t now);
	void keep_in_window();
	int split() const;

	byte _screen[SCUMMCV_W * SCUMMCV_H];
	Graphics::Surface _surface;
	byte _pal[768];
	bool _palDirty = true, _cursorPalOn = false;
	bool _drawing = false, _ready = false;
	byte _cursor[32 * 32];
	uint _cw = 0, _ch = 0;
	int _hotX = 0, _hotY = 0;
	byte _key = 0;
	bool _mouseVisible = false;
	scummcv_cursor_t _ptr = {};
	bool _ptrPatDirty = false;
	uint8_t _satSent[SCUMMCV_SAT] = {0};

	// the pointer, in 1/256 pixels
	int32 _mx = 160 << 8, _my = 100 << 8;
	bool _moved = false;
	uint32 _movedUs = 0;
	uint32 _held_us = 0, _last_us = 0;
	uint8 _fire = 3, _keyCode = 0xF;
	uint32 _keySince = 0;
	bool _keyActed = false;
	int _manualX = -1;              // the room's window where 4 / 6 put it (-1: it follows)
	bool _viewPinned = false;       // 4 or 6 placed it, and the pointer hasn't moved since
	bool _centreNow = false;        // 5: the next window is the middle (unless a message or cutscene wants it)
	bool _viewPoke = false;         // look at the window again now
	bool _oneWindow = false;        // keypad 1: one window for all 200 lines, verb area or not
	uint32 _nudgeAt = 0;
	void nudge(int dir);
	bool _menuWanted = false, _menuOpen = false;
	int _lastSlot = 1;              // the slot the save screen opens on
	void save_menu();
	Common::Event _queue[QUEUE];
	uint8 _usbButtons = 0;
	int _qHead = 0, _qTail = 0;
	int _saveLoad = 0;

	scummcv_pal_t _cvpal;
	scummcv_view_t _view = {0, 0, SPLIT, 0};
	uint8_t _tms[256 * 192];

	Audio::MixerImpl *_mixer = 0;
	uint64_t _mixed = 0, _mixStart = 0;
	int16 _mixBuf[256 * 2];
	uint32 _timerLast = 0;
	Common::String _saveDir;
	bool _quit = false;
	char _error[256] = "";          // the last error()'s text

	void push(Common::EventType type, Common::KeyCode kc = Common::KEYCODE_INVALID, uint16 ascii = 0, byte flags = 0) {
		if ((_qTail + 1) % QUEUE == _qHead) return;   // full: this one is dropped
		Common::Event &e = _queue[_qTail];
		e = Common::Event();
		e.type = type;
		e.mouse.x = (int16)(_mx >> 8);
		e.mouse.y = (int16)(_my >> 8);
		if (kc != Common::KEYCODE_INVALID) e.kbd = Common::KeyState(kc, ascii, flags);
		_qTail = (_qTail + 1) % QUEUE;
	}
};

void OSystem_Cart::offer() {
	if (!_ready || _drawing || !nescv_wants_picture(&enc)) return;
	if (_palDirty) {
		scummcv_palette(&_cvpal, _pal, 1);
		_palDirty = false;
	}
	choose_view(&_view);
	scummcv_convert(&_cvpal, _screen, SCUMMCV_W, &_view, _tms);
	nescv_picture_t p;
	p.pixels = _tms;
	p.pitch = 256;
	p.lut = ident_lut;
	for (int b = 0; b < NESCV_ROWS; b++) {
		p.line[b] = (uint8_t)(8 * b);
		p.col[b] = 0;
	}
	p.backdrop = C_BLACK;
	if (nescv_offer(&enc, &p, pic_index + 1)) {
		pic_index++;
		pic_views[pic_index & 15] = _view;
		_ready = false;
	}
}

// the window: the pointer's, or, while an actor talks and the pointer is
// still, wherever holds the message. In a cutscene (or any scene the
// player has no say in) the room's at the middle, moved only as far as an
// actor's message needs.
void OSystem_Cart::choose_view(scummcv_view_t *v) {
	const Scumm::ScummEngine *s = static_cast<const Scumm::ScummEngine *>(g_engine);
	const Common::Rect &m = Scumm::g_movieCartMsgRect;
	const int mx = _mx >> 8, my = _my >> 8;
	v->split = split();
	if (_manualX >= 0) {
		// where 4 / 6 put it, but for the pointer pushed against its edge
		scummcv_view_t a = *v;
		a.x = _manualX;
		if (_mouseVisible) scummcv_view(&a, mx, my);
		if (_viewPinned || my >= a.split || (mx >= _manualX + 4 && mx <= _manualX + 251)) a.x = _manualX;
		_manualX = a.x;
		*v = a;
	} else if (s && s->movieCartCutscene()) {
		v->x = (SCUMMCV_W - 256) / 2;
		v->bx = 0;
		if (s->_haveMsg && m.top < v->split) scummcv_view_keep(v, m.left, m.right);
	} else if (s && s->_haveMsg && m.top < v->split && time_us_32() - _movedUs > STILL_US) {
		scummcv_view_keep(v, m.left, m.right);
	} else {
		scummcv_view(v, mx, my);
		if (_centreNow) v->x = (SCUMMCV_W - 256) / 2;   // 5: the middle, then following as ever
	}
	_centreNow = false;
}

// the first line of the verb area's own window: where the game's verb area
// starts (144 in most games), or none (200) while the game gives the room
// all 200 lines (Fate of Atlantis's opening) or keypad 1 asked for one
// window. Rounded up to a whole row of cells.
int OSystem_Cart::split() const {
	const Scumm::ScummEngine *s = static_cast<const Scumm::ScummEngine *>(g_engine);
	if (_oneWindow || !s || s->movieCartVerbHeight() <= 0) return SCUMMCV_H;
	return CLIP((s->movieCartVerbTop() + 7) & ~7, 0, SCUMMCV_H);
}

// ---- the save and load screen (savemenu.h) -----------------------------------------

// a slot's save: true when there is one, its play time, and its thumbnail
// if asked for (RGB565, 160 x 100; *pic false: none to show)
static bool slot_info(int slot, uint32_t *playtime, uint16_t *thumb = nullptr, bool *pic = nullptr) {
	const Scumm::ScummEngine *s = static_cast<const Scumm::ScummEngine *>(g_engine);
	Common::String desc;
	Graphics::Surface *t = nullptr;
	Scumm::SaveStateMetaInfos info = {};
	Scumm::SaveStateMetaInfos *ip = &info;
	if (!Scumm::ScummEngine::querySaveMetaInfos(SAVE_TARGET, slot, s->_game.heversion, desc, t, ip)) {
		delete t;                       // (its pixels with it)
		return false;
	}
	*playtime = ip ? info.playtime : 0;
	bool have = false;
	if (t) {
		if (thumb && t->getFormat().bytesPerPixel == 2 && t->getWidth() == SAVEMENU_THUMB_W && t->getHeight() >= SAVEMENU_THUMB_H) {
			for (int y = 0; y < SAVEMENU_THUMB_H; y++)
				memcpy(thumb + y * SAVEMENU_THUMB_W, t->getBasePtr(0, y), SAVEMENU_THUMB_W * 2);
			have = true;
		}
		delete t;
	}
	if (pic) *pic = have;
	return true;
}

// # held: the game paused under the save screen, the console served, the
// engine asked to save or load once it's gone (the next time round its loop)
void OSystem_Cart::save_menu() {
	Scumm::ScummEngine *s = static_cast<Scumm::ScummEngine *>(g_engine);
	static scummcv_pal_t p332;
	static bool p332_made;
	// the thumbnail (32 KB) from the heap (PSRAM) while the screen is up
	uint16_t *thumb = static_cast<uint16_t *>(malloc(SAVEMENU_THUMB_W * SAVEMENU_THUMB_H * 2));
	if (!p332_made) {
		savemenu_palette332(&p332);
		p332_made = true;
	}
	g_engine->pauseEngine(true);
	_menuOpen = true;
	// its title at once, while the saves are read (each with its picture),
	// the console served between them
	canvas_fill(&msg_canvas, 0, 0, VDP_W, VDP_H, C_BLACK);
	centre(2, "SAVED GAMES", C_LYELLOW, 1);
	offer_msg();
	service();

	uint8_t joy, fire, key;
	savemenu_t m;
	savemenu_init(&m, _lastSlot, 0xFF, 3, 0xF);
	for (int i = 0; i < SAVEMENU_SLOTS; i++) {
		m.used[i] = slot_info(i, &m.playtime[i]);
		service();
	}
	// what's held now isn't a press (# for one, still down)
	cvf_input(&joy, &fire, &key);
	m.joy = joy;
	m.fire = fire;
	m.key = key;
	bool have_thumb = false;
	if (thumb && m.used[m.sel]) slot_info(m.sel, &m.playtime[m.sel], thumb, &have_thumb);
	int act = SAVEMENU_REDRAW;
	for (;;) {
		if (act == SAVEMENU_PICKED) {
			have_thumb = false;
			if (thumb && m.used[m.sel]) slot_info(m.sel, &m.playtime[m.sel], thumb, &have_thumb);
		}
		if (act == SAVEMENU_PICKED || act == SAVEMENU_REDRAW) {
			savemenu_draw(&msg_canvas, &m, have_thumb ? thumb : nullptr, &p332);
			offer_msg();
		}
		if (act == SAVEMENU_SAVE || act == SAVEMENU_LOAD || act == SAVEMENU_CLOSE) break;
		service();                      // the console's frames, USB, the timers
		cvf_input(&joy, &fire, &key);
		act = savemenu_input(&m, joy, fire, key, time_us_32());
	}

	free(thumb);
	_menuOpen = false;
	g_engine->pauseEngine(false);
	if (act == SAVEMENU_SAVE) {
		s->requestSave(m.sel, "MovieCart");
		_lastSlot = m.sel;
	} else if (act == SAVEMENU_LOAD) {
		s->requestLoad(m.sel);
		if (m.sel) _lastSlot = m.sel;
	}
	// back to the game: its picture again, and the controller as it is now
	// (a button still down isn't a click, # still held doesn't open this again)
	cvf_input(&joy, &fire, &key);
	_fire = fire & 3;
	_keyCode = key;
	_keyActed = true;
	_ready = true;
	_viewPoke = true;
}

// keypad 4 / 6: the room's window a step left or right, and the pointer kept in it
void OSystem_Cart::nudge(int dir) {
	const int from = _manualX >= 0 ? _manualX : _view.x;
	_manualX = CLIP(from + dir * NUDGE, 0, SCUMMCV_W - 256);
	_mx = CLIP<int32>(_mx, _manualX << 8, ((_manualX + 255) << 8) | 255);
	_moved = true;
	_viewPinned = true;
	_viewPoke = true;
}

void OSystem_Cart::pointer() {
	if (_ptrPatDirty && nescv_vram(&enc, 0x3800, _ptr.pat, 64)) _ptrPatDirty = false;
	uint8_t sat[SCUMMCV_SAT];
	scummcv_pointer_sat(&_ptr, &_cvpal, &pic_views[nescv_next_shown(&enc) & 15], _mx >> 8, _my >> 8,
	                    _mouseVisible && !_ptrPatDirty && !_menuOpen, sat);
	if (memcmp(sat, _satSent, sizeof sat) && nescv_vram(&enc, 0x1B00, sat, SCUMMCV_SAT))
		memcpy(_satSent, sat, sizeof sat);
}

static void pointer_frame(void) { g_cart->pointer(); }

// the menu's bottom line, should RESET come: how it went (pictures shown a
// second, the heap, the longest the engine went without serving the
// console, keyboards and mice, Q when the flash reads quad, S serial, and
// the engine's stack at its deepest, in KB)
static void update_note(uint32_t now) {
	static uint32_t t_last, shown_last, gap_max, served;
	if (served) gap_max = MAX(gap_max, now - served);
	served = now;
	if (now - t_last < NOTE_US) return;
	unsigned per_s = (unsigned)((enc.stats.shown - shown_last) * 1000000ull / (now - t_last));
	int kb, mice;
	usbin_count(&kb, &mice);
	char io[12];
	if (usb_midi) snprintf(io, sizeof io, "midi%s", usbmidi_dropped() ? "!" : "");
	else snprintf(io, sizeof io, "K%uM%u", (unsigned)MIN(kb, 9), (unsigned)MIN(mice, 9));
	// 32 characters at most: "99/s 9.9M gap9999 K9M9 Q s999K" (gap in ms; "midi" for
	// K and M when a computer has the port, "midi!" once a message was dropped)
	snprintf(note, sizeof note, "%u/s %u.%uM gap%u %.5s %c s%uK", MIN(per_s, 99u), (unsigned)(heap_used() >> 20) % 10,
	         (unsigned)(heap_used() % 1048576 / 104858) % 10, (unsigned)MIN<uint32_t>(gap_max / 1000, 9999u),
	         io, xip_quad ? 'Q' : 'S', (unsigned)MIN<uint32_t>((cvd_stack_used() + 1023) / 1024, 999u));
	cvf_note(note);
	t_last = now;
	shown_last = enc.stats.shown;
	gap_max = 0;
}

void OSystem_Cart::service() {
	// not inside itself: the mixer's channels (Channel::mix) and the timers
	// ask getMillis(), which serves; inside, the time is all they need. (A
	// nested service() mixed again before _mixed moved on: a recursion that
	// ran the engine's 128K stack out in 25 ms, 0.1.3 and 0.1.4.)
	if (in_service) return;
	static bool first = true;
	if (first) {
		first = false;
		cvd_stage(9);
	}
	in_service = true;
	uint32_t now = time_us_32();
	update_note(now);
	if (usb_midi) usbmidi_task();
	else usbin_task();
	if (!_menuOpen) input(now);         // the save screen reads the controller itself
	// the mixer, at its rate (Player_V2 renders in it, and calls the tap)
	if (!_mixStart) _mixStart = time_us_64();
	uint64_t due = (time_us_64() - _mixStart) * MIX_RATE / 1000000;
	while (_mixed < due) {
		uint n = (uint)MIN<uint64_t>(due - _mixed, 256);
		_mixer->mixCallback((byte *)_mixBuf, n * 4);
		_mixed += n;
	}
	// the engine's timers (iMUSE and the like): every millisecond at most
	if (now - _timerLast >= 1000) {
		_timerLast = now;
		static_cast<DefaultTimerManager *>(_timerManager)->handler();
	}
	if (new_frame()) {
		// a new picture when the engine drew one, or when the pointer moves the window
		const Scumm::ScummEngine *se = static_cast<const Scumm::ScummEngine *>(g_engine);
		if (_menuOpen) {
			if (msg_waiting) offer_msg();   // the save screen is the picture
		} else if (!_ready && (_mouseVisible || _viewPoke || (se && se->movieCartCutscene()) || split() != _view.split)) {
			scummcv_view_t v = _view;
			choose_view(&v);
			_ready = memcmp(&v, &_view, sizeof v) != 0;
			_viewPoke = false;
		}
		if (_ready && !_menuOpen) offer();
		send_frame();
	}
	in_service = false;
}

// the pointer in the window the TV shows. Near its edge the next picture's
// window pans (scummcv_view: 32 pixels a picture at most); a pointer let past
// the edge meanwhile is drawn off the window, then jumps back when the
// picture comes.
void OSystem_Cart::keep_in_window() {
	const scummcv_view_t &v = pic_views[nescv_next_shown(&enc) & 15];
	const int left = (_my >> 8) >= v.split ? v.bx : v.x;
	_mx = CLIP<int32>(_mx, left << 8, ((left + 255) << 8) | 255);
}

// the controller, a USB mouse and keyboard: read in service(), every
// INPUT_US, their events queued with the pointer where it was then. The
// engine asks pollEvent() only once a loop, and a loop can take longer
// than a tap of the fire button.
void OSystem_Cart::input(uint32_t now) {
	if (_last_us && now - _last_us < INPUT_US) return;
	uint8_t joy, fire, key;
	cvf_input(&joy, &fire, &key);
	uint32_t dt = _last_us ? now - _last_us : 0;
	_last_us = now;
	if (dt > 50000) dt = 50000;
	// the joystick: 60 pixels a second, rising to 240 over half a second held
	int dx = !(joy & 2) - !(joy & 8), dy = !(joy & 4) - !(joy & 1);
	if (dx || dy) {
		_held_us += dt;
		uint32_t speed = 60 + MIN<uint32_t>(_held_us, 500000) * 180 / 500000;   // pixels a second
		int32 step = (int32)(speed * 256 * dt / 1000000);
		_mx = CLIP<int32>(_mx + dx * step, 0, (SCUMMCV_W - 1) << 8);
		_my = CLIP<int32>(_my + dy * step, 0, (SCUMMCV_H - 1) << 8);
		keep_in_window();
		_moved = true;
		_movedUs = now;
		_viewPinned = false;
	} else {
		_held_us = 0;
	}
	// the buttons, on each change
	uint8 f = fire & 3;
	if ((f ^ _fire) & 1) push(f & 1 ? Common::EVENT_LBUTTONUP : Common::EVENT_LBUTTONDOWN);
	if ((f ^ _fire) & 2) push(f & 2 ? Common::EVENT_RBUTTONUP : Common::EVENT_RBUTTONDOWN);
	_fire = f;
	// a USB mouse: its movement, then its buttons on each change
	int mdx, mdy;
	uint8_t mb;
	if (usbin_mouse(&mdx, &mdy, &mb)) {
		if (mdx || mdy) {
			_mx = CLIP<int32>(_mx + mdx * MOUSE_SCALE, 0, (SCUMMCV_W - 1) << 8);
			_my = CLIP<int32>(_my + mdy * MOUSE_SCALE, 0, (SCUMMCV_H - 1) << 8);
			keep_in_window();
			_moved = true;
			_movedUs = now;
			_viewPinned = false;
		}
		if ((mb ^ _usbButtons) & 1) push(mb & 1 ? Common::EVENT_LBUTTONDOWN : Common::EVENT_LBUTTONUP);
		if ((mb ^ _usbButtons) & 2) push(mb & 2 ? Common::EVENT_RBUTTONDOWN : Common::EVENT_RBUTTONUP);
		_usbButtons = mb;
	}
	// a USB keyboard
	uint8_t usage, mods;
	bool down;
	while (usbin_key(&usage, &mods, &down)) {
		Common::KeyCode kc;
		uint16 ascii;
		const bool shift = mods & 0x22, ctrl = mods & 0x11, alt = mods & 0x44;
		if (!hid_key(usage, shift, &kc, &ascii)) continue;
		byte flags = (ctrl ? Common::KBD_CTRL : 0) | (alt ? Common::KBD_ALT : 0) | (shift ? Common::KBD_SHIFT : 0);
		push(down ? Common::EVENT_KEYDOWN : Common::EVENT_KEYUP, kc, ascii, flags);
	}
	// the keypad: * = Esc and 0 = '.' when pressed; 1 one window or two; 7 / 9 held: save / load
	if (key != _keyCode) {
		_keyCode = key;
		_keySince = now;
		_keyActed = false;
		if (key == 0x9) { push(Common::EVENT_KEYDOWN, Common::KEYCODE_ESCAPE, 27); push(Common::EVENT_KEYUP, Common::KEYCODE_ESCAPE, 27); }
		if (key == 0xA) { push(Common::EVENT_KEYDOWN, Common::KEYCODE_PERIOD, '.'); push(Common::EVENT_KEYUP, Common::KEYCODE_PERIOD, '.'); }
		if (key == 0x2 || key == 0xE) {     // 4, 6
			nudge(key == 0x2 ? -1 : 1);
			_nudgeAt = now + NUDGE_HOLD_US;
		}
		if (key == 0xD) {                   // 1: one window, or the verb area's own again
			_oneWindow = !_oneWindow;
			_viewPoke = true;
		}
		if (key == 0x3) {                   // 5: the middle, and following again at once
			const int mid = (SCUMMCV_W - 256) / 2;
			_manualX = -1;
			_viewPinned = false;
			_centreNow = true;
			// the pointer into the middle window, clear of the edges that scroll it
			if ((_my >> 8) < _view.split) _mx = CLIP<int32>(_mx, (mid + 32) << 8, ((mid + 223) << 8) | 255);
			_moved = true;
			_viewPoke = true;
		}
	} else if ((key == 0x2 || key == 0xE) && (int32_t)(now - _nudgeAt) >= 0) {
		nudge(key == 0x2 ? -1 : 1);
		_nudgeAt = now + NUDGE_REP_US;
	} else if (!_keyActed && key == 0x6 && now - _keySince > MENU_HOLD_US) {
		_keyActed = true;
		_menuWanted = true;
	} else if (!_keyActed && (key == 0x5 || key == 0xB) && now - _keySince > HOLD_US) {
		_keyActed = true;
		_saveLoad = key == 0x5 ? 1 : 2;
	}
}

bool OSystem_Cart::pollEvent(Common::Event &event) {
	service();
	if (_menuWanted && g_engine) {
		_menuWanted = false;
		save_menu();
	}
	if (_saveLoad && g_engine) {
		Scumm::ScummEngine *s = static_cast<Scumm::ScummEngine *>(g_engine);
		if (_saveLoad == 1) s->requestSave(1, "MovieCart");
		else s->requestLoad(1);
		_saveLoad = 0;
	}
	if (_moved) {
		_moved = false;
		push(Common::EVENT_MOUSEMOVE);
	}
	if (_qHead == _qTail) return false;
	event = _queue[_qHead];
	_qHead = (_qHead + 1) % QUEUE;
	return true;
}

// ---- MIDI to a computer on the USB port ---------------------------------------------

// ScummVM's MPU-401 (16 channels; its timer, every 10 ms, from the timer
// manager service() runs), the bytes to usbmidi_dev
class CartMidiDriver : public MidiDriver_MPU401 {
public:
	~CartMidiDriver() override { close(); }
	int open() override { _open = true; return 0; }
	bool isOpen() const override { return _open; }
	void close() override {
		if (_open) MidiDriver_MPU401::close();
		_open = false;
	}
	void send(uint32 b) override {
		const uint8_t m[3] = {(uint8_t)b, (uint8_t)(b >> 8), (uint8_t)(b >> 16)};
		const uint8_t st = m[0] & 0xF0;
		if (m[0] < 0x80 || m[0] >= 0xF0) return;     // channel messages only (SysEx comes by sysEx())
		usbmidi_write(m, st == 0xC0 || st == 0xD0 ? 2 : 3);
	}
	void sysEx(const byte *msg, uint16 len) override {
		uint8_t b[300];
		if (len > sizeof b - 2) return;
		b[0] = 0xF0;
		memcpy(b + 1, msg, len);
		b[len + 1] = 0xF7;
		usbmidi_write(b, len + 2u);
	}

private:
	bool _open = false;
};

// "cvmidi_mt32" as the music driver: MT_MT32, so the engine sends the MT-32
// its own instruments and the game's MT-32 music as it is
class CartMusicPlugin : public MusicPluginObject {
public:
	const char *getName() const override { return "MovieCart USB MIDI"; }
	const char *getId() const override { return "cvmidi"; }
	MusicDevices getDevices() const override {
		MusicDevices d;
		d.push_back(MusicDevice(this, "mt32", MT_MT32));
		return d;
	}
	Common::Error createInstance(MidiDriver **md, MidiDriver::DeviceHandle) const override {
		*md = new CartMidiDriver();
		return Common::kNoError;
	}
};

class CartPlugin : public Plugin {
public:
	CartPlugin(PluginObject *o, PluginType t) { _pluginObject = o; _type = t; }
	~CartPlugin() { delete _pluginObject; }
	bool loadPlugin() override { return true; }
	void unloadPlugin() override {}
};

class CartPluginProvider : public PluginProvider {
public:
	PluginList getPlugins() override {
		PluginList pl;
		pl.push_back(new CartPlugin(new CartMusicPlugin(), PLUGIN_TYPE_MUSIC));
		return pl;
	}
};

// the port: a MIDI device first; a computer configures it at once (it's
// the host and powers the port), a hub never does, and then it's a host.
// A computer that answers but doesn't take it is asking its user (macOS,
// the first time: "Allow accessory to connect?"), so it's waited for.
static bool usb_choose(void) {
	usbmidi_start();
	const uint32_t t0 = time_us_32();
	bool asked = false, armed = false;   // armed: the fire buttons seen up since (the one that opened the game let go)
	for (;;) {
		usbmidi_task();
		if (usbmidi_mounted()) break;
		const uint32_t dt = time_us_32() - t0;
		const bool host = usbmidi_host_seen();
		if (dt >= (host ? MIDI_ALLOW_US : MIDI_WAIT_US)) break;
		if (host && !asked && dt >= MIDI_ASK_US) {
			message("A COMPUTER IS ON THE USB PORT", "ALLOW MOVIECART MIDI ON IT", "FIRE: GO ON WITHOUT MIDI");
			asked = true;
		}
		if (asked) {
			uint8_t joy, fire, key;
			cvf_input(&joy, &fire, &key);
			if ((fire & 3) == 3) armed = true;       // both up (0: pressed)
			else if (armed) break;
		}
		if (new_frame()) {
			if (msg_waiting) offer_msg();
			send_frame();
		}
	}
	const bool midi = usbmidi_mounted();
	if (asked) message("STARTING", midi ? "MUSIC: MIDI TO THE COMPUTER" : "LOADING THE GAME", nullptr);
	if (midi) return true;
	usbmidi_stop();
	usbin_init();                   // a host, for a keyboard and a mouse
	return false;
}

// ---- the game -----------------------------------------------------------------------

static Common::String game_dir;

static void __attribute__((noreturn)) run_engine(void) {
	cvd_stage(4);
	g_system = g_cart = new OSystem_Cart();
	g_cart->setSaveDir(game_dir);
	g_cart->initBackend();
	pointer_hook = pointer_frame;
	usb_midi = usb_choose();        // the USB port: MIDI to a computer, or a host for a keyboard and mouse
	cvd_stage(5);

	scummconf_setup("cart", game_dir.c_str(), usb_midi ? "cvmidi_mt32" : "pcjr");   // as the Mac test's (../scummconf.h)
	if (usb_midi) ConfMan.set("mt32_device", "cvmidi_mt32");
	ConfMan.setInt("autosave_period", AUTOSAVE_S);   // slot 0, the save screen's AUTO
	psgmix_install();               // the PCjr player, and the CMS one for songs the PCjr has no version of

	PluginManager::instance().addPluginProvider(new CartPluginProvider());
	PluginManager::instance().loadAllPlugins();
	const EnginePlugin *plugin = 0;
	const EnginePlugin::List &plugins = EngineMan.getPlugins();
	for (EnginePlugin::List::const_iterator p = plugins.begin(); p != plugins.end(); ++p)
		if (!strcmp((**p)->getEngineID(), "scumm"))
			plugin = *p;
	if (!plugin) fail("NO SCUMM ENGINE", nullptr, "SCUMM: no engine");
	Common::FSNode dir(game_dir);
	Common::FSList files;
	if (!dir.isDirectory() || !dir.getChildren(files, Common::FSNode::kListAll))
		fail("ITS FOLDER CAN'T BE READ", nullptr, "SCUMM: the folder can't be read");
	GameList games = (*plugin)->detectGames(files);
	if (games.empty()) fail("NO SCUMM GAME IN ITS FOLDER", nullptr, "SCUMM: no game found");
	cvd_stage(6);
	ConfMan.set("gameid", games[0].getGameID());
	SearchMan.addDirectory(dir.getPath(), dir, 0, 4);
	snprintf(note, sizeof note, "SCUMM: %.24s", games[0].getGameID().c_str());
	cvf_note(note);

	Engine *engine = 0;
	Common::Error err = (*plugin)->createInstance(g_system, &engine);
	if (err.getCode() != Common::kNoError || !engine) fail("THE ENGINE DIDN'T START", nullptr, "SCUMM: createInstance failed");
	cvd_stage(7);
	msg_shown = false;
	err = engine->run();
	snprintf(note, sizeof note, "SCUMM: ended (%d)", (int)err.getCode());
	fail("THE GAME ENDED", nullptr, note);
}

// the engine on a stack of its own (the SDK's core 0 stack is 4K, in scratch Y)
static uint32_t engine_stack[STACK_BYTES / 4] __attribute__((aligned(8)));

static void __attribute__((noreturn)) run_on_engine_stack(void) {
	cvd_stack(engine_stack, STACK_BYTES);   // painted: the note says how deep it goes
	__asm volatile("mov sp, %0\n\tmsr msplim, %1\n\tblx %2\n"
	               : : "r"(engine_stack + STACK_BYTES / 4), "r"((uint8_t *)engine_stack + STACK_GUARD), "r"(run_engine)
	               : "memory");
	__builtin_unreachable();
}

int main(void) {
	static const cvf_config_t cfg = {gbkernel_bin, GBKERNEL_BIN_LEN, handoff_bin, HANDOFF_BIN_LEN};
	xip_quad = cvxip_fast();        // quad flash reads: before core 1 and the PSRAM (cvxip.h)
	cvf_start(&cfg);
	cvd_start();                    // where it gets to, in the menu's trace (cvdiag.h)
	cvd_stage(xip_quad ? 101 : 100);
	nescv_init(&enc);
	nescv_reg(&enc, 1, 0xE2);       // the pointer's sprites: 16 x 16 (the kernel's R1 is $E0)
	for (scummcv_view_t &v : pic_views) v = {0, 0, SPLIT, 0};
	for (int i = 0; i < 256; i++) ident_lut[i] = (i & 15) ? (uint8_t)(i & 15) : (uint8_t)C_BLACK;

	const cvl_t *b = cvf_launch();
	if (b && b->path[0]) {
		const char *slash = strrchr(b->path, '/');
		game_dir = slash && slash != b->path ? Common::String(b->path, slash) : Common::String("/");
		const char *dn = strrchr(game_dir.c_str(), '/');
		snprintf(game_name, sizeof game_name, "%.32s", dn && dn[1] ? dn + 1 : game_dir.c_str());
		for (char *c = game_name; *c; c++)
			if (*c >= 'a' && *c <= 'z') *c = (char)(*c - 'a' + 'A');
	}
	message(b && b->path[0] ? "STARTING" : nullptr, b && b->path[0] ? nullptr : "OPEN A GAME'S 000.LFL FROM THE MENU", nullptr);
	serve_for(OPEN_US);
	if (!b || !b->path[0]) fail("OPEN A GAME'S 000.LFL FROM THE MENU", nullptr, nullptr);

	if (!cvpsram_init()) fail("THE PSRAM DIDN'T ANSWER", nullptr, "SCUMM: the PSRAM didn't answer");
	cvd_stage(2);
	heap_init();
	if (cabal_fs_init() != CABAL_FS_OK)
		fail(cvsd_card_in() ? "THE CARD DIDN'T MOUNT" : "NO CARD IN THE SLOT", nullptr, "SCUMM: the card didn't mount");
	cvd_stage(3);
	watchdog_enable(WATCHDOG_MS, true);
	message("STARTING", "LOADING THE GAME", nullptr);
	serve_for(OPEN_US);
	run_on_engine_stack();
}
