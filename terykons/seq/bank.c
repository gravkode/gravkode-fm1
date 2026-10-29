#include "bank.h"
#include <stddef.h>
#include <string.h>
#include "plat.h"

_Static_assert(BANK_LO >= PLAT_DATA_LO && BANK_HI <= 0xD8000u, "the bank in the data area, below the store objects");
_Static_assert(BANK_DATA >= sizeof(bank_head_t) && BANK_DATA + sizeof(seq_pattern_t) <= BANK_SLOT,
               "a slot holds a header and a pattern");
_Static_assert(BANK_PATTERNS + 1u < BANK_SLOTS && BANK_SLOTS <= 127u, "slots to spare; a slot's number in an int8_t");

static uint8_t page[BANK_PAGE] __attribute__((aligned(4)));   /* a page on its way to the flash */

uint32_t bank_crc32(const void *p, uint32_t n, uint32_t crc)   /* zlib's CRC-32, 4 bits a step */
{
    static const uint32_t T[16] = {
        0x00000000u, 0x1DB71064u, 0x3B6E20C8u, 0x26D930ACu, 0x76DC4190u, 0x6B6B51F4u, 0x4DB26158u, 0x5005713Cu,
        0xEDB88320u, 0xF00F9344u, 0xD6D6A3E8u, 0xCB61B38Cu, 0x9B64C2B0u, 0x86D3D2D4u, 0xA00AE278u, 0xBDBDF21Cu};
    const uint8_t *b = p;
    uint32_t c = ~crc;
    while (n--) {
        c ^= *b++;
        c = (c >> 4) ^ T[c & 15u];
        c = (c >> 4) ^ T[c & 15u];
    }
    return ~c;
}

static uint32_t slot_off(uint32_t s) { return BANK_LO + s * BANK_SLOT; }

static const bank_head_t *head(uint32_t s) { return (const bank_head_t *)plat_data(slot_off(s)); }

static int head_ok(const bank_head_t *h)
{
    return h->magic == BANK_MAGIC && h->pattern <= BANK_CLIP &&
           (h->size == sizeof(seq_pattern_t) || h->size == SEQ_PATTERN_V018) &&   /* (0.35: or as saved before its
                                                                                  * locks: seq_load finds none) */
           h->hcrc == bank_crc32(h, offsetof(bank_head_t, hcrc), 0);
}

static int erased(uint32_t s)            /* every byte 0xFF */
{
    const uint32_t *w = (const uint32_t *)plat_data(slot_off(s));
    for (uint32_t i = 0; i < BANK_SLOT / 4u; i++)
        if (w[i] != 0xFFFFFFFFu)
            return 0;
    return 1;
}

void bank_init(bank_t *b)
{
    int32_t newest = -1;
    memset(b, 0, sizeof *b);
    b->w_slot = -1;
    for (uint32_t n = 0; n <= BANK_CLIP; n++)
        b->live[n] = -1;
    b->ok = plat_data(BANK_LO) != 0 && plat_data(BANK_HI - 1u) != 0;
    if (!b->ok)
        return;
    for (uint32_t s = 0; s < BANK_SLOTS; s++) {
        const bank_head_t *h = head(s);
        const uint8_t *data = (const uint8_t *)h + BANK_DATA;
        if (!head_ok(h) || bank_crc32(data, h->size, 0) != h->crc || !seq_valid((const seq_pattern_t *)data) ||
            (h->size == sizeof(seq_pattern_t) && !seq_locks_valid((const seq_pattern_t *)data))) {
            b->state[s] = erased(s) ? BANK_FREE : BANK_STALE;   /* (a save cut short: to erase) */
            continue;
        }
        const int8_t was = b->live[h->pattern];
        if (was >= 0 && head((uint32_t)was)->seq > h->seq) {
            b->state[s] = BANK_STALE;        /* an older copy */
            continue;
        }
        if (was >= 0)
            b->state[was] = BANK_STALE;
        b->live[h->pattern] = (int8_t)s;
        b->state[s] = BANK_LIVE;
        if (newest < 0 || h->seq > b->seq) {
            b->seq = h->seq;
            newest = (int32_t)s;
        }
    }
    b->next = (uint32_t)(newest + 1) % BANK_SLOTS;
}

const seq_pattern_t *bank_pattern(const bank_t *b, uint32_t n)
{
    if (!b->ok || n > BANK_CLIP || b->live[n] < 0)
        return 0;
    return (const seq_pattern_t *)(plat_data(slot_off((uint32_t)b->live[n])) + BANK_DATA);
}

uint32_t bank_count(const bank_t *b, uint32_t state)
{
    uint32_t k = 0;
    for (uint32_t s = 0; s < BANK_SLOTS; s++)
        k += b->state[s] == state || (state == BANK_STALE && b->state[s] == BANK_HALF);
    return k;
}

int bank_begin(bank_t *b, uint32_t n)
{
    if (!b->ok || bank_busy(b) || n > BANK_CLIP)
        return -1;
    for (uint32_t i = 0; i < BANK_SLOTS; i++) {
        const uint32_t s = (b->next + i) % BANK_SLOTS;
        if (b->state[s] != BANK_FREE)
            continue;
        b->state[s] = BANK_STALE;            /* (until its header is written: no pattern in it) */
        b->w_slot = (int8_t)s;
        b->w_pattern = (uint8_t)n;
        b->w_at = 0;
        b->w_crc = 0;
        b->next = (s + 1u) % BANK_SLOTS;
        return 0;
    }
    return -1;
}

static int failed(bank_t *b)
{
    b->w_slot = -1;                          /* (its slot stays stale: erased later) */
    return -1;
}

int bank_step(bank_t *b, const seq_pattern_t *p, uint32_t pages)
{
    const uint32_t size = sizeof *p;
    if (!bank_busy(b))
        return -1;
    const uint32_t s = (uint32_t)b->w_slot, off = slot_off(s);
    for (; pages && b->w_at < size; pages--) {
        uint32_t n = size - b->w_at;
        if (n > BANK_PAGE)
            n = BANK_PAGE;
        memcpy(page, (const uint8_t *)p + b->w_at, n);
        b->w_crc = bank_crc32(page, n, b->w_crc);
        if (plat_data_write(off + BANK_DATA + b->w_at, page, n))
            return failed(b);
        b->w_at = (uint16_t)(b->w_at + n);
    }
    if (b->w_at < size)
        return 0;
    if (bank_crc32(plat_data(off + BANK_DATA), size, 0) != b->w_crc)   /* read back: what the flash holds */
        return failed(b);
    bank_head_t h;
    h.magic = BANK_MAGIC;
    h.pattern = b->w_pattern;
    h.size = (uint16_t)size;
    h.seq = b->seq + 1u;
    h.crc = b->w_crc;
    h.hcrc = bank_crc32(&h, offsetof(bank_head_t, hcrc), 0);
    memcpy(page, &h, sizeof h);
    if (plat_data_write(off, page, sizeof h) || !head_ok(head(s)))   /* the commit */
        return failed(b);
    b->seq = h.seq;
    if (b->live[b->w_pattern] >= 0)
        b->state[b->live[b->w_pattern]] = BANK_STALE;   /* its older copy */
    b->live[b->w_pattern] = (int8_t)s;
    b->state[s] = BANK_LIVE;
    b->w_slot = -1;
    return 1;
}

int bank_tidy(bank_t *b)
{
    if (!b->ok)
        return 0;
    for (uint32_t s = 0; s < BANK_SLOTS; s++) {
        if ((int32_t)s == b->w_slot || (b->state[s] != BANK_STALE && b->state[s] != BANK_HALF))
            continue;
        const int half = b->state[s] == BANK_HALF;
        if (plat_data_erase(slot_off(s) + (half ? 0x1000u : 0u)))
            return 0;
        b->state[s] = half ? (erased(s) ? BANK_FREE : BANK_STALE) : BANK_HALF;
        return 1;
    }
    return 0;
}
