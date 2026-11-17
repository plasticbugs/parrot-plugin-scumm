// heap.c -- the plugin's heap in the PSRAM: newlib's malloc grows through
// _sbrk, and this one hands it the 8 MB PSRAM, mapped through the XIP cache
// (cvpsram.h). The engine's heap (its screens, the game's resources) is a
// few MB; SRAM keeps the stacks, the console's lists and the picture.
// heap_init() before the first malloc (the SDK's runtime doesn't allocate).
#include <errno.h>
#include <stdint.h>

#include "cvpsram.h"

static uint8_t *top, *lim;

void heap_init(void) {
    top = CVPSRAM_CACHED;
    lim = CVPSRAM_CACHED + CVPSRAM_SIZE;
}

void *_sbrk(int incr) {
    if (!top || (incr > 0 ? (uint32_t)(lim - top) < (uint32_t)incr : top + incr < CVPSRAM_CACHED)) {
        errno = ENOMEM;
        return (void *)-1;
    }
    uint8_t *prev = top;
    top += incr;
    return prev;
}

uint32_t heap_used(void) { return top ? (uint32_t)(top - CVPSRAM_CACHED) : 0; }

// frank_quest_fs.c's whole-file reads
void *psram_malloc(size_t n);
void psram_free(void *p);
#include <stdlib.h>
void *psram_malloc(size_t n) { return malloc(n); }
void psram_free(void *p) { free(p); }
