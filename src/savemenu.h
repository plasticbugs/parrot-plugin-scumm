// savemenu: the SCUMM plugin's save and load screen, drawn on the console
// by the cart (the game is paused under it). Ten slots: 0 is the engine's
// autosave (load only), 1-9 the player's. The chosen slot's thumbnail (the
// 160 x 100 picture ScummVM keeps in each save) above, the slots and their
// play times below.
//
//   the controller: the joystick picks a slot (keypad 0-9 too), left fire
//   saves there, right fire loads it (over a save, or loading: the same
//   button again to be sure), # or * goes back to the game
//
// Portable C (no pico-sdk): the plugin's (plugin/cart.cpp) and the Mac test's.
#pragma once
#include <stdbool.h>
#include <stdint.h>

#include "vdp.h"
#include "scummcv.h"

#ifdef __cplusplus
extern "C" {
#endif

#define SAVEMENU_SLOTS 10
#define SAVEMENU_THUMB_W 160
#define SAVEMENU_THUMB_H 100

enum { SAVEMENU_NONE, SAVEMENU_REDRAW, SAVEMENU_PICKED, SAVEMENU_SAVE, SAVEMENU_LOAD, SAVEMENU_CLOSE };

typedef struct {
    bool used[SAVEMENU_SLOTS];
    uint32_t playtime[SAVEMENU_SLOTS];  // seconds
    int sel;
    int confirm;                        // SAVEMENU_SAVE / _LOAD: pressed once over a save
    const char *status;                 // a line in place of the help, until the next move
    // the controller as last read: a press is a change to pressed
    uint8_t joy, fire, key;
    uint32_t repeat_at;
} savemenu_t;

// joy, fire, key as cvf_input() reads them now (what's held as the screen
// opens doesn't count as pressed)
void savemenu_init(savemenu_t *m, int sel, uint8_t joy, uint8_t fire, uint8_t key);
// the controller, each TV frame or so: what to do (PICKED: another slot
// is chosen, its thumbnail wanted; REDRAW: just draw again)
int savemenu_input(savemenu_t *m, uint8_t joy, uint8_t fire, uint8_t key, uint32_t now_us);
// the whole screen into c. thumb: the chosen slot's thumbnail, RGB565,
// SAVEMENU_THUMB_W x SAVEMENU_THUMB_H (NULL: none); p332: the 256 colours
// of RGB 3-3-2 as savemenu_palette332() makes them
void savemenu_draw(canvas_t *c, const savemenu_t *m, const uint16_t *thumb, const scummcv_pal_t *p332);
void savemenu_palette332(scummcv_pal_t *p332);

#ifdef __cplusplus
}
#endif
