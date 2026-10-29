/* The patterns' bank (seq/bank.c) on a flash of its own, as the FM-1's behaves: erased to 0xFF a 4 KiB sector at a
 * time, written only from 1 to 0. Saves, the newest copy winning, a save cut short (power lost), a damaged copy, the
 * slots used in turn, a full bank. Built and run by tests/run_seq_test.py:
 *   cc -std=c99 -I include -I terykons terykons/tests/bank_test.c terykons/seq/bank.c terykons/seq/seq.c */
#include <stdio.h>
#include <string.h>
#include "seq/bank.h"
#include "plat.h"

static int fails;
#define CHECK(c, ...) do { if (!(c)) { printf("FAIL %s:%d: ", __FILE__, __LINE__); printf(__VA_ARGS__); \
    printf("\n"); fails++; } } while (0)

/* the flash: the data area, and each slot's writes (the wear) */
static uint8_t flash[PLAT_DATA_HI - PLAT_DATA_LO] __attribute__((aligned(4)));
static uint32_t erases[(PLAT_DATA_HI - PLAT_DATA_LO) / 0x1000u], writes;
static int write_fail;

const uint8_t *plat_data(uint32_t off)
{
    return off >= PLAT_DATA_LO && off < PLAT_DATA_HI ? &flash[off - PLAT_DATA_LO] : 0;
}
int plat_data_erase(uint32_t off)
{
    if ((off & 0xFFFu) || off < PLAT_DATA_LO || off + 0x1000u > PLAT_DATA_HI)
        return -1;
    memset(&flash[off - PLAT_DATA_LO], 0xFF, 0x1000u);
    erases[(off - PLAT_DATA_LO) / 0x1000u]++;
    return 0;
}
int plat_data_write(uint32_t off, const void *src, uint32_t n)
{
    if (write_fail || off < PLAT_DATA_LO || off + n > PLAT_DATA_HI || (off & 0xFFu) + n > 0x100u)
        return -1;                       /* (the bank writes a page at a time, never across one) */
    for (uint32_t i = 0; i < n; i++)
        flash[off - PLAT_DATA_LO + i] &= ((const uint8_t *)src)[i];
    writes++;
    return 0;
}

static void save(bank_t *b, uint32_t n, const seq_pattern_t *p)   /* a whole save, as the app's frames make it */
{
    CHECK(bank_begin(b, n) == 0, "a free slot for pattern %u", n);
    int r;
    while ((r = bank_step(b, p, 3u)) == 0)
        ;
    CHECK(r == 1, "pattern %u saved", n);
}

static void tidy(bank_t *b)
{
    while (bank_tidy(b))
        ;
}

int main(void)
{
    static bank_t b;
    static seq_pattern_t p, q;
    memset(flash, 0xFF, sizeof flash);

    bank_init(&b);
    CHECK(b.ok && bank_count(&b, BANK_FREE) == BANK_SLOTS && bank_count(&b, BANK_LIVE) == 0, "an erased bank");
    CHECK(bank_pattern(&b, 0) == 0, "pattern 1 empty");

    /* a save: the pattern as it was, read in place; again: the newer copy, the older one stale */
    seq_defaults(&p);
    p.swing = 61;
    p.track[5].length = 12;
    p.track[5].step[3].on = 1;
    save(&b, 3, &p);
    CHECK(bank_pattern(&b, 3) && !memcmp(bank_pattern(&b, 3), &p, sizeof p), "pattern 4 as saved");
    CHECK(bank_count(&b, BANK_LIVE) == 1 && bank_count(&b, BANK_FREE) == BANK_SLOTS - 1u, "one slot taken");
    p.swing = 62;
    save(&b, 3, &p);
    CHECK(bank_pattern(&b, 3)->swing == 62 && bank_count(&b, BANK_STALE) == 1, "the newer copy; the older stale");
    bank_init(&b);                       /* power-on: the same */
    CHECK(bank_pattern(&b, 3) && bank_pattern(&b, 3)->swing == 62 && bank_count(&b, BANK_LIVE) == 1 &&
          bank_count(&b, BANK_STALE) == 1, "after power-on: the newest copy wins");
    tidy(&b);
    CHECK(bank_count(&b, BANK_STALE) == 0 && bank_count(&b, BANK_FREE) == BANK_SLOTS - 1u, "the stale one erased");

    /* a save cut short (the power lost before its header): the pattern as it was before, the slot to erase */
    q = p;
    q.swing = 70;
    CHECK(bank_begin(&b, 3) == 0 && bank_step(&b, &q, 5u) == 0, "a save begun");
    bank_init(&b);
    CHECK(bank_pattern(&b, 3) && bank_pattern(&b, 3)->swing == 62, "cut short: the copy before");
    CHECK(bank_count(&b, BANK_STALE) == 1, "its slot to erase: %u", bank_count(&b, BANK_STALE));
    CHECK(bank_tidy(&b) && bank_count(&b, BANK_STALE) == 1 && bank_tidy(&b) && bank_count(&b, BANK_STALE) == 0,
          "erased a sector at a time");

    /* a write that fails: the save fails, nothing changes */
    write_fail = 1;
    CHECK(bank_begin(&b, 3) == 0 && bank_step(&b, &q, 99u) == -1 && !bank_busy(&b), "a failed write: the save fails");
    write_fail = 0;
    CHECK(bank_pattern(&b, 3)->swing == 62, "the copy before");
    tidy(&b);

    /* a damaged copy (a bit of its steps lost): not taken; the pattern empty */
    {
        const uint32_t at = (uint32_t)((const uint8_t *)bank_pattern(&b, 3) - flash) + 1000u;
        flash[at] ^= 0x10u;
        bank_init(&b);
        CHECK(bank_pattern(&b, 3) == 0 && bank_count(&b, BANK_STALE) == 1, "a damaged copy: refused");
        tidy(&b);
    }

    /* the slots in turn: 100 saves of two patterns, tidied as the app does when quiet, each slot used about alike */
    memset(erases, 0, sizeof erases);
    for (uint32_t i = 0; i < 100u; i++) {
        p.swing = (uint8_t)(50u + i % 25u);
        save(&b, i & 1u, &p);
        if (i % 7u == 6u)
            tidy(&b);
    }
    tidy(&b);
    {
        uint32_t lo = ~0u, hi = 0;
        for (uint32_t s = 0; s < BANK_SLOTS * 2u; s++) {
            const uint32_t e = erases[(BANK_LO - PLAT_DATA_LO) / 0x1000u + s];
            lo = e < lo ? e : lo;
            hi = e > hi ? e : hi;
        }
        CHECK(hi - lo <= 1u, "the wear spread: erases %u..%u a sector", lo, hi);
        CHECK(bank_pattern(&b, 1)->swing == 50u + 99u % 25u, "the last save of pattern 2 kept");
    }

    /* a full bank: no slot free until one is erased; nothing outside the bank written */
    {
        uint32_t n = 0;
        while (bank_begin(&b, 7) == 0) {
            while (bank_step(&b, &p, 99u) == 0)
                ;
            n++;
        }
        CHECK(n == BANK_SLOTS - 2u && bank_count(&b, BANK_STALE) == n - 1u && bank_count(&b, BANK_FREE) == 0,
              "full after %u saves (beside patterns 1 and 2: 30, the last live)", n);
        CHECK(bank_tidy(&b) && bank_tidy(&b) && bank_begin(&b, 7) == 0, "one erased: a save again");
        for (uint32_t i = 0; i < BANK_LO - PLAT_DATA_LO; i++)
            if (flash[i] != 0xFFu) {
                CHECK(0, "written below the bank at %u", i);
                break;
            }
        for (uint32_t i = BANK_HI - PLAT_DATA_LO; i < sizeof flash; i++)
            if (flash[i] != 0xFFu) {
                CHECK(0, "written above the bank at %u", i);
                break;
            }
    }

    /* (0.35) a pattern with its locks, saved and read back whole; one as 0.18..0.34 saved it (no locks: its size the
     * old one) taken, its steps as they were, no locks when loaded */
    memset(flash, 0xFF, sizeof flash);
    bank_init(&b);
    seq_defaults(&p);
    p.track[2].step[5].on = 1;
    CHECK(seq_lock_set(&p, 2, 5, SEQ_LK_OSC1, 77) == 0 && seq_lock_set(&p, 6, 0, SEQ_LK_SEND, 10) == 0, "locks set");
    save(&b, 4, &p);
    bank_init(&b);
    {
        static seq_t s;
        seq_init(&s);
        seq_load(&s, bank_pattern(&b, 4));
        CHECK(s.pat.nlocks == 2 && seq_lock_get(&s.pat, 2, 5, SEQ_LK_OSC1) == 77 &&
              seq_lock_get(&s.pat, 6, 0, SEQ_LK_SEND) == 10, "the locks read back");
    }
    {
        const uint32_t off = BANK_LO + 7u * BANK_SLOT, size = SEQ_PATTERN_V018;
        seq_defaults(&q);
        q.track[1].step[9].on = 1;
        q.track[1].step[9].note[0] = 71;
        for (uint32_t at = 0; at < size; at += BANK_PAGE)
            CHECK(plat_data_write(off + BANK_DATA + at, (const uint8_t *)&q + at,
                                  size - at < BANK_PAGE ? size - at : BANK_PAGE) == 0, "old pattern written");
        bank_head_t h;
        memset(&h, 0, sizeof h);
        h.magic = BANK_MAGIC;
        h.pattern = 9;
        h.size = (uint16_t)size;
        h.seq = 1000;
        h.crc = bank_crc32(&q, size, 0);
        h.hcrc = bank_crc32(&h, __builtin_offsetof(bank_head_t, hcrc), 0);
        CHECK(plat_data_write(off, &h, sizeof h) == 0, "old header written");
        bank_init(&b);
        const seq_pattern_t *old = bank_pattern(&b, 9);
        CHECK(old != 0 && old->track[1].step[9].on && old->track[1].step[9].note[0] == 71, "0.34's pattern taken");
        static seq_t s;
        seq_init(&s);
        seq_load(&s, old);
        CHECK(s.pat.nlocks == 0 && s.pat.track[1].step[9].note[0] == 71, "0.34's pattern loaded: no locks");
        CHECK(bank_pattern(&b, 4) != 0, "the new one still there");
    }

    if (fails) {
        printf("%d failures\n", fails);
        return 1;
    }
    printf("PASS: the patterns' bank (saves, newest copy, cut short, failed write, damage, wear, full, locks, 0.34's patterns)\n");
    return 0;
}
