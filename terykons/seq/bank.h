/* Terykons' patterns in the flash (0.18): 16 of them in the data area (plat.h plat_data_*), 0x97000..0xD7000, in 32
 * slots of 8 KiB (two sectors) used in turn. A pattern is saved into an erased slot a page (256 bytes) at a time,
 * each with the interrupts off for well under a millisecond (the sound goes on), its header last: until then the
 * slot holds no pattern, so a save cut short loses nothing. Its older copy is then stale, to be erased (bank_tidy):
 * an erase stops the sound for tens of ms, so the app does it only while nothing sounds, and at power-on. With 15
 * slots more than patterns and the copy, 15 saves can be made before an erase is needed. (0.40) The copy (HOME +
 * OCT-, pasted with HOME + OCT+) is a pattern of its own, BANK_CLIP: older firmware finds no pattern in its slot and
 * erases it. Main loop only. */
#pragma once
#include <stdint.h>
#include "seq.h"

#define BANK_PATTERNS 16u
#define BANK_CLIP BANK_PATTERNS          /* (0.40) the copy: one pattern more, after the 16 */
#define BANK_SLOTS 32u
#define BANK_SLOT 0x2000u                /* two 4 KiB sectors */
#define BANK_LO 0x97000u                 /* (plat.h PLAT_DATA_LO) .. 0xD7000; app.h's store objects from 0xD8000 */
#define BANK_HI (BANK_LO + BANK_SLOTS * BANK_SLOT)
#define BANK_DATA 0x100u                 /* a pattern's place in its slot: after the header's page */
#define BANK_PAGE 0x100u                 /* written a page at a time */
#define BANK_MAGIC 0x50415431u           /* "PAT1" */

enum { BANK_FREE, BANK_LIVE, BANK_STALE, BANK_HALF };   /* a slot: erased; a pattern's newest copy; to erase; its
                                                         * first sector erased */

typedef struct {                         /* at a slot's start, written last */
    uint32_t magic;                      /* BANK_MAGIC */
    uint16_t pattern;                    /* 0..BANK_PATTERNS - 1 */
    uint16_t size;                       /* sizeof(seq_pattern_t); or SEQ_PATTERN_V018, saved before its locks */
    uint32_t seq;                        /* saves so far: the newest copy of a pattern wins */
    uint32_t crc;                        /* the pattern's (CRC-32) */
    uint32_t hcrc;                       /* the header's, up to here */
} bank_head_t;

typedef struct {
    uint8_t ok;                          /* the flash can be read (plat_data) */
    uint8_t state[BANK_SLOTS];           /* BANK_* */
    int8_t live[BANK_PATTERNS + 1u];     /* each pattern's slot (and the copy's, BANK_CLIP); -1: none (an empty one) */
    uint32_t seq;                        /* the newest copy's */
    uint32_t next;                       /* where a save looks for a free slot first: in turn, to spread the wear */
    int8_t w_slot;                       /* a save under way: its slot (-1: none), its pattern, bytes written, CRC */
    uint8_t w_pattern;
    uint16_t w_at;
    uint32_t w_crc;
} bank_t;

void bank_init(bank_t *b);               /* what the flash holds (nothing erased) */
const seq_pattern_t *bank_pattern(const bank_t *b, uint32_t n);   /* pattern n (or BANK_CLIP), in the flash; 0: an
                                                                  * empty one */
uint32_t bank_count(const bank_t *b, uint32_t state);              /* slots in a state (BANK_HALF counts as STALE) */
int bank_begin(bank_t *b, uint32_t n);   /* a save of pattern n (or BANK_CLIP) into a free slot: 0, or -1 (none
                                          * free) */
/* the save under way: up to pages more of p (a copy made a page at a time: p may change meanwhile); 1 saved, 0 more
 * to do, -1 failed (the slot is left to erase) */
int bank_step(bank_t *b, const seq_pattern_t *p, uint32_t pages);
static inline int bank_busy(const bank_t *b) { return b->w_slot >= 0; }
int bank_tidy(bank_t *b);                /* a stale sector erased: 1, or 0 (none to) */
uint32_t bank_crc32(const void *p, uint32_t n, uint32_t crc);   /* (crc: 0 to begin) */
