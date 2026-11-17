// scummcv: a SCUMM picture (320 x 200, 256 colours from a VGA palette) as
// the TMS9918's colours, for nescv (../nes/nescv.h) to send to the console.
// Portable C (no pico-sdk): the plugin's and the Mac test's.
//
//   palette changes:  scummcv_palette(&pal, rgb, dither)
//   each picture:     scummcv_view(&view, mouse_x, mouse_y)
//                     scummcv_convert(&pal, pixels, 320, &view, out)
//                     out (256 x 192, colours 1-15) through nescv
//   the pointer:      scummcv_cursor(&cur, ...) when the engine sets it
//                     (its patterns to $3800), each TV frame
//                     scummcv_pointer_sat(&cur, &pal, &view, mouse_x, mouse_y, shown, sat)
//                     (to $1B00): two sprites, moving at the TV's rate
//
// The console shows 256 x 192 of the 320 x 200 at 1:1: SCUMM's pictures are
// drawn in 8-pixel strips and scroll by 8, as the TMS9918's cells are, so a
// cell-aligned window keeps most cells the same from picture to picture.
// The window follows the pointer, 8 pixels at a time, when the pointer
// comes near its edge. The lines from split down (the sentence, the verbs,
// the inventory, a dialogue's choices) have a window of their own, at the
// left (where their text starts) unless the pointer is down there.
#pragma once
#include <stdbool.h>
#include <stdint.h>

#define SCUMMCV_W 320
#define SCUMMCV_H 200

// each palette entry as two TMS9918 colours (1-15) and how many of 16
// pixels take the second (0: the first alone)
typedef struct {
    uint8_t a[256], b[256], mix[256];
} scummcv_pal_t;

typedef struct {
    int x, y;              // the window's top left in the picture: multiples of 8
    int split;             // the first line of the verb area (a multiple of 8; 144 in v4 games)
    int bx;                // the verb area's window's left
} scummcv_view_t;

// rgb: 256 x R, G, B (0-255). dither: 0 nearest colour, 1 a 4 x 4 ordered
// dither between two colours where that is nearer, 2 the same in grey
// (black, grey and white)
void scummcv_palette(scummcv_pal_t *p, const uint8_t *rgb, int dither);
// how much a mix of colours far apart is avoided (3), a lightness
// difference counting lweight (0.1) of a hue or chroma one; for tuning
extern float scummcv_penalty, scummcv_lweight;
// the fewest 16ths of either colour in a mix of two far apart in lightness
// (4: fewer make a grid of dots), and how far apart that is (OKLab 0.15)
extern int scummcv_sparse;
extern float scummcv_sparse_dl;
// the grade before matching: lightness to the power lift (0.6), chroma times sat (1.7)
extern float scummcv_lift, scummcv_sat;
// the windows, moved if the pointer is within 32 pixels of a left or
// right edge (the room's above split, the verb area's below) or in the
// last 16 lines
void scummcv_view(scummcv_view_t *v, int mouse_x, int mouse_y);
// the room's window moved (by 8s) as little as it takes to hold an actor's
// message, columns [left, right) (or centred on it when it is wider than 256)
void scummcv_view_keep(scummcv_view_t *v, int left, int right);
// out: 256 x 192 bytes, colours 1-15
void scummcv_convert(const scummcv_pal_t *p, const uint8_t *pixels, int pitch, const scummcv_view_t *v, uint8_t *out);
// the TMS9918's colours as RGB, for tests (0 = transparent, shown black)
extern const uint8_t scummcv_tms_rgb[16][3];

// ---- the pointer: two 16 x 16 sprites over the picture, the shape and,
// under it, an outline (the shape grown by a pixel), so it shows on any
// colour. R1 = $E2 (16 x 16 sprites); patterns 0 and 4 at $3800.
typedef struct {
    uint8_t pat[64];       // the two sprites' patterns, as the VDP takes them
    int ox, oy;            // the sprites' top left, from the hot spot
    uint8_t index;         // the shape's commonest palette entry: its colour
    bool empty;
} scummcv_cursor_t;

// buf: w x h palette entries (pitch apart), key transparent; the window
// of 16 x 16 kept is the shape's (with a pixel round it for the outline
// where it's smaller). True when the patterns changed.
bool scummcv_cursor(scummcv_cursor_t *c, const uint8_t *buf, int w, int h, int pitch, int hotx, int hoty, uint8_t key);

#define SCUMMCV_SAT 9
// the first SCUMMCV_SAT bytes of the sprite attributes ($1B00) for the
// pointer at (mx, my) in the picture, with the window v on screen; not
// shown: no sprites
void scummcv_pointer_sat(const scummcv_cursor_t *c, const scummcv_pal_t *p, const scummcv_view_t *v, int mx, int my,
                 bool shown, uint8_t sat[SCUMMCV_SAT]);
