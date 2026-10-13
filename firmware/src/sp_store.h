#pragma once
// What a SmartPort unit is backed by (M7 spec sec. 2): the M6 RAM disk or the floppy.
// Calls return a SmartPort status code (liron.asm err_*), without the $80 bit.
#include <stdint.h>

#define SP_BLOCK_LEN 512

enum {
    SP_E_OK = 0x00, SP_E_BADCMD = 0x01, SP_E_BADPCNT = 0x04, SP_E_BADCTL = 0x21,
    SP_E_IOERROR = 0x27, SP_E_NOWRITE = 0x2B, SP_E_BADBLOCK = 0x2D, SP_E_OFFLINE = 0x2F,
    SP_E_LAST = 0x40,                    // INIT: nothing behind us
    SP_E_SOFT_IO = 0x67,                 // read OK after a recal or wrong-cylinder seek (ROM $E7)
};

// Media state for the STATUS general byte (ROM Se5f0: no disk wins over write-protect).
typedef enum { SP_MEDIA_OK, SP_MEDIA_NODISK, SP_MEDIA_WRPROT } sp_media_t;

typedef struct {
    uint8_t (*read)(void *ctx, uint32_t block, uint8_t *out);         // SP_BLOCK_LEN bytes
    uint8_t (*write)(void *ctx, uint32_t block, const uint8_t *in);
    uint8_t (*format)(void *ctx);
    uint8_t (*eject)(void *ctx);
    sp_media_t (*media)(void *ctx);
    void (*unistat)(void *ctx, uint8_t out[8]);                      // STATUS $05
    void *ctx;
} sp_store_t;
