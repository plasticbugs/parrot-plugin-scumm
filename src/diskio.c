// diskio.c -- FatFs's disk (vendor/frank-quest/src/fatfs) on the cart's
// microSD card: the SDK's cvsd, a sector at a time.
#include "fatfs/ff.h"
#include "fatfs/diskio.h"

// cvsd.h's (its fsro.h and FatFs's ff.h name the same things)
#include <stdbool.h>
#include <stdint.h>
bool cvsd_init(void);
bool cvsd_read(uint32_t lba, uint8_t *buf);
bool cvsd_write(uint32_t lba, const uint8_t *buf);

static DSTATUS status = STA_NOINIT;

DSTATUS disk_initialize(BYTE pdrv) {
    if (pdrv) return STA_NOINIT;
    status = cvsd_init() ? 0 : STA_NOINIT;
    return status;
}

DSTATUS disk_status(BYTE pdrv) { return pdrv ? STA_NOINIT : status; }

// cart.cpp's: the console served between sectors, while a file loads
void diskio_wait(void);

DRESULT disk_read(BYTE pdrv, BYTE *buff, LBA_t sector, UINT count) {
    if (pdrv || (status & STA_NOINIT)) return RES_NOTRDY;
    for (UINT i = 0; i < count; i++) {
        diskio_wait();
        if (!cvsd_read((uint32_t)sector + i, buff + 512 * i)) return RES_ERROR;
    }
    return RES_OK;
}

DRESULT disk_write(BYTE pdrv, const BYTE *buff, LBA_t sector, UINT count) {
    if (pdrv || (status & STA_NOINIT)) return RES_NOTRDY;
    for (UINT i = 0; i < count; i++)
        if (!cvsd_write((uint32_t)sector + i, buff + 512 * i)) return RES_ERROR;
    return RES_OK;
}

DRESULT disk_ioctl(BYTE pdrv, BYTE cmd, void *buff) {
    if (pdrv || (status & STA_NOINIT)) return RES_NOTRDY;
    switch (cmd) {
    case CTRL_SYNC:        return RES_OK;    // cvsd_write waits out each sector
    case GET_SECTOR_SIZE:  *(WORD *)buff = 512; return RES_OK;
    case GET_BLOCK_SIZE:   *(DWORD *)buff = 1; return RES_OK;
    default:               return RES_PARERR;
    }
}

// the cart has no clock: files are dated 2026-01-01
DWORD get_fattime(void) { return (DWORD)(2026 - 1980) << 25 | 1u << 21 | 1u << 16; }
