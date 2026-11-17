// savemenu.c: see savemenu.h.
#include "savemenu.h"

#include <stdio.h>
#include <string.h>

#define REPEAT_FIRST_US 350000      // the joystick held: again after this
#define REPEAT_US       150000      // and every this

#define ROWS 5                      // slots in a column: 0-4 left, 5-9 right
#define THUMB_X 48                  // a multiple of 8 (two colours in each 8 x 1)
#define TITLE_Y 2
#define THUMB_Y 13
#define LIST_Y  119
#define LIST_DY 9
#define HELP_Y  166
#define BACK_Y  177                 // ends at 185: the last lines are under some TVs' edges

// keypad codes as cvf_input() reads them: digit -> code
static const uint8_t DIGIT_KEY[10] = {0xA, 0xD, 0x7, 0xC, 0x2, 0x3, 0xE, 0x5, 0x1, 0xB};

void savemenu_init(savemenu_t *m, int sel, uint8_t joy, uint8_t fire, uint8_t key) {
    memset(m->used, 0, sizeof m->used);
    memset(m->playtime, 0, sizeof m->playtime);
    m->sel = sel >= 0 && sel < SAVEMENU_SLOTS ? sel : 1;
    m->confirm = SAVEMENU_NONE;
    m->status = NULL;
    m->joy = joy;
    m->fire = fire;
    m->key = key;
    m->repeat_at = 0;
}

static int pick(savemenu_t *m, int sel) {
    if (sel < 0 || sel == m->sel) return SAVEMENU_NONE;
    m->sel = sel;
    m->confirm = SAVEMENU_NONE;
    m->status = NULL;
    return SAVEMENU_PICKED;
}

// the joystick's direction (bits 0-3 low = N E S W pressed) as a slot
static int moved(const savemenu_t *m, uint8_t joy) {
    int col = m->sel / ROWS, row = m->sel % ROWS;
    if (!(joy & 1)) row = (row + ROWS - 1) % ROWS;
    else if (!(joy & 4)) row = (row + 1) % ROWS;
    else if (!(joy & 2) || !(joy & 8)) col ^= 1;
    else return -1;
    return col * ROWS + row;
}

int savemenu_input(savemenu_t *m, uint8_t joy, uint8_t fire, uint8_t key, uint32_t now_us) {
    int r = SAVEMENU_NONE;
    // the joystick: a step on the press, then again while held
    const uint8_t dir = joy & 15;
    if (dir != 15) {
        if (dir != (m->joy & 15)) {
            m->repeat_at = now_us + REPEAT_FIRST_US;
            r = pick(m, moved(m, joy));
        } else if ((int32_t)(now_us - m->repeat_at) >= 0) {
            m->repeat_at = now_us + REPEAT_US;
            r = pick(m, moved(m, joy));
        }
    }
    m->joy = joy;
    // the keypad, on a press: a digit picks its slot; # or * goes back
    if (key != m->key) {
        m->key = key;
        for (int d = 0; d < 10; d++)
            if (key == DIGIT_KEY[d]) r = pick(m, d) ? SAVEMENU_PICKED : r;
        if (key == 0x6 || key == 0x9) return SAVEMENU_CLOSE;
    }
    // the fire buttons, on a press (0: pressed)
    const uint8_t pressed = (uint8_t)(m->fire & ~fire & 3);
    m->fire = fire;
    if (pressed & 1) {                          // left: save
        if (m->sel == 0) {
            m->status = "AUTO IS FOR LOADING: PICK 1-9";
            m->confirm = SAVEMENU_NONE;
            return SAVEMENU_REDRAW;
        }
        if (!m->used[m->sel] || m->confirm == SAVEMENU_SAVE) return SAVEMENU_SAVE;
        m->confirm = SAVEMENU_SAVE;
        m->status = "SAVE OVER IT? LEFT FIRE AGAIN";
        return SAVEMENU_REDRAW;
    }
    if (pressed & 2) {                          // right: load
        if (!m->used[m->sel]) {
            m->status = "NOTHING SAVED THERE";
            m->confirm = SAVEMENU_NONE;
            return SAVEMENU_REDRAW;
        }
        if (m->confirm == SAVEMENU_LOAD) return SAVEMENU_LOAD;
        m->confirm = SAVEMENU_LOAD;
        m->status = "LOAD IT? RIGHT FIRE AGAIN";
        return SAVEMENU_REDRAW;
    }
    return r;
}

// ---- the screen ------------------------------------------------------------------

void savemenu_palette332(scummcv_pal_t *p332) {
    uint8_t rgb[256 * 3];
    for (int i = 0; i < 256; i++) {
        rgb[i * 3 + 0] = (uint8_t)((i >> 5) * 255 / 7);
        rgb[i * 3 + 1] = (uint8_t)(((i >> 2) & 7) * 255 / 7);
        rgb[i * 3 + 2] = (uint8_t)((i & 3) * 255 / 3);
    }
    scummcv_palette(p332, rgb, 1);
}

static void centre(canvas_t *c, int y, const char *s, uint8_t colour) {
    int w = (int)strlen(s) * 8;
    canvas_text(c, w < VDP_W ? (VDP_W - w) / 2 : 0, y, s, colour, 1);
}

static const uint8_t BAYER[4][4] = {{0, 8, 2, 10}, {12, 4, 14, 6}, {3, 11, 1, 9}, {15, 7, 13, 5}};

// a row of the thumbnail into the canvas, 8 pixels (a VDP segment, two
// colours at most) at a time. Each pixel's colour is what p332 makes of it
// (its two TMS9918 colours mixed, so graded as the game's pictures are);
// a segment takes the pair of those colours whose blends come nearest to
// all 8, and each pixel the Bayer dither between the pair at its blend.
static void thumb_row(canvas_t *c, int y, const uint16_t *row, const scummcv_pal_t *p332) {
    const uint8_t *th = BAYER[y & 3];
    for (int x0 = 0; x0 < SAVEMENU_THUMB_W; x0 += 8) {
        int t[8][3];                    // each pixel's colour, RGB x 16
        uint16_t cand = 0;              // the TMS9918 colours its pixels use
        for (int k = 0; k < 8; k++) {
            const uint16_t v = row[x0 + k];
            const uint8_t i = (uint8_t)((v >> 13) << 5 | ((v >> 8) & 7) << 2 | ((v >> 3) & 3));
            const uint8_t a = p332->a[i], b = p332->b[i], mx = p332->mix[i];
            for (int j = 0; j < 3; j++)
                t[k][j] = scummcv_tms_rgb[a][j] * (16 - mx) + scummcv_tms_rgb[b][j] * mx;
            cand |= (uint16_t)(1u << a);
            if (mx) cand |= (uint16_t)(1u << b);
        }
        // the pair (c1 <= c2) with the least squared error, each pixel at
        // its nearest blend of the two (in 16ths)
        int best1 = 1, best2 = 1;
        int64_t best_err = INT64_MAX;
        uint8_t best_f[8] = {0};
        for (int c1 = 1; c1 < 16; c1++) {
            if (!(cand >> c1 & 1)) continue;
            for (int c2 = c1; c2 < 16; c2++) {
                if (!(cand >> c2 & 1)) continue;
                int d[3], dd = 0;
                for (int j = 0; j < 3; j++) {
                    d[j] = (scummcv_tms_rgb[c2][j] - scummcv_tms_rgb[c1][j]) * 16;
                    dd += d[j] * d[j] / 16;
                }
                int64_t err = 0;
                uint8_t f[8];
                for (int k = 0; k < 8 && err < best_err; k++) {
                    int e[3], dot = 0;
                    for (int j = 0; j < 3; j++) {
                        e[j] = t[k][j] - scummcv_tms_rgb[c1][j] * 16;
                        dot += e[j] * d[j] / 16;
                    }
                    int q = dd ? (dot * 16 + dd / 2) / dd : 0;       // 16ths of the way to c2
                    q = q < 0 ? 0 : q > 16 ? 16 : q;
                    f[k] = (uint8_t)q;
                    for (int j = 0; j < 3; j++) {
                        const int r = e[j] - d[j] * q / 16;
                        err += (int64_t)r * r;
                    }
                }
                if (err < best_err) {
                    best_err = err;
                    best1 = c1;
                    best2 = c2;
                    memcpy(best_f, f, sizeof f);
                }
            }
        }
        uint8_t *px = &c->px[THUMB_Y + y][THUMB_X + x0];
        for (int k = 0; k < 8; k++) px[k] = (uint8_t)(best_f[k] > th[(x0 + k) & 3] ? best2 : best1);
    }
}

void savemenu_draw(canvas_t *c, const savemenu_t *m, const uint16_t *thumb, const scummcv_pal_t *p332) {
    canvas_fill(c, 0, 0, VDP_W, VDP_H, C_BLACK);
    centre(c, TITLE_Y, "SAVED GAMES", C_LYELLOW);

    // the chosen slot's picture, in a frame
    canvas_fill(c, THUMB_X - 1, THUMB_Y - 1, SAVEMENU_THUMB_W + 2, SAVEMENU_THUMB_H + 2, C_GRAY);
    canvas_fill(c, THUMB_X, THUMB_Y, SAVEMENU_THUMB_W, SAVEMENU_THUMB_H, C_BLACK);
    if (thumb && m->used[m->sel]) {
        for (int y = 0; y < SAVEMENU_THUMB_H; y++) thumb_row(c, y, thumb + y * SAVEMENU_THUMB_W, p332);
    } else {
        const char *s = !m->used[m->sel] ? (m->sel ? "EMPTY" : "NO AUTOSAVE YET") : "NO PICTURE";
        int w = (int)strlen(s) * 8;
        canvas_text(c, THUMB_X + (SAVEMENU_THUMB_W - w) / 2, THUMB_Y + SAVEMENU_THUMB_H / 2 - 4, s, C_GRAY, 1);
    }
    canvas_all_dirty(c);

    // the slots: a bar behind the chosen one (8-pixel aligned, so the
    // text's two colours are the bar's and its own)
    for (int i = 0; i < SAVEMENU_SLOTS; i++) {
        const int x = i < ROWS ? 8 : 128, y = LIST_Y + (i % ROWS) * LIST_DY;
        const bool on = i == m->sel;
        if (on) canvas_fill(c, x, y - 1, 120, LIST_DY, C_DBLUE);
        char t[20], when[12];
        if (m->used[i]) snprintf(when, sizeof when, "%u:%02u", (unsigned)(m->playtime[i] / 3600), (unsigned)(m->playtime[i] / 60 % 60));
        else snprintf(when, sizeof when, "-");
        if (i == 0) snprintf(t, sizeof t, "AUTO %7s", when);
        else snprintf(t, sizeof t, "%d    %7s", i, when);
        canvas_text(c, x + 8, y, t, on ? C_WHITE : m->used[i] ? C_GRAY : C_DBLUE, 1);
    }

    centre(c, HELP_Y, m->status ? m->status : "LEFT FIRE SAVES   RIGHT LOADS", m->status ? C_LYELLOW : C_GRAY);
    centre(c, BACK_Y, "# BACK TO THE GAME", C_DBLUE);
}
