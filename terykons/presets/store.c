#include "store.h"
#include "../seq/bank.h"
#include "plat.h"
#include <stddef.h>
#include <string.h>

#define RECORD 256u
#define PER_SECTOR 15u                   /* page F00..FFF is ALWAYS erased: no loader AT-record collisions */
#define SECTORS 8u
#define LOG_MAGIC 0x50525332u
typedef struct { uint32_t magic; uint16_t slot, size; uint32_t seq, crc, hcrc; } log_head_t;
_Static_assert(sizeof(log_head_t) + sizeof(preset_t) <= RECORD, "expanded preset fits one record");
_Static_assert(PRESET_LOG_RECORDS == SECTORS * PER_SECTOR && PRESET_LOG_LO >= PLAT_XDATA1_LO &&
               PRESET_LOG_HI <= PLAT_XDATA1_HI, "preset log in a free flash window");
#ifdef PLAT_HOST
static int fail_writes;
void preset_store_test_fail(int fail) { fail_writes = fail; }
#endif
static uint32_t off(uint32_t r) { return PRESET_LOG_LO + r / PER_SECTOR * 4096u + r % PER_SECTOR * RECORD; }
static const log_head_t *head(uint32_t r) { return (const log_head_t *)plat_data(off(r)); }
static const preset_t *sound(uint32_t r) { return (const preset_t *)(plat_data(off(r)) + sizeof(log_head_t)); }
static int committed(uint32_t r)
{
    const log_head_t *h = head(r);
    return h && h->magic == LOG_MAGIC && h->slot < PRESET_USERS && h->size == sizeof(preset_t) &&
           h->hcrc == bank_crc32(h, offsetof(log_head_t, hcrc), 0) &&
           h->crc == bank_crc32(sound(r), sizeof(preset_t), 0) && preset_valid(sound(r));
}
static int erased(uint32_t r)
{
    const uint32_t *p = (const uint32_t *)plat_data(off(r));
    if (!p) return 0;
    for (uint32_t i = 0; i < RECORD / 4u; i++) if (p[i] != 0xFFFFFFFFu) return 0;
    return 1;
}
static int guard_erased(uint32_t sector)
{
    const uint32_t *p = (const uint32_t *)plat_data(PRESET_LOG_LO + sector * 4096u + 0xF00u);
    if (!p) return 0;
    for (uint32_t n = 0; n < 64; n++) if (p[n] != 0xFFFFFFFFu) return 0;
    return 1;
}
void preset_store_init(preset_bank_t *b)
{
    presets_init(b);
    b->ok = plat_data(PRESET_LOG_LO) && plat_data(PRESET_LOG_HI - 1u);
    if (!b->ok) return;
    for (uint32_t r = 0; r < PRESET_LOG_RECORDS; r++) {
        /* Old firmware may have left a loader record in the guard page. Do not append there until an entire
         * sector erase removes it; existing live presets, if any, are evacuated by compaction first. */
        b->state[r] = guard_erased(r / PER_SECTOR) && erased(r) ? PRESET_FREE : PRESET_STALE;
        if (!committed(r)) continue;
        const log_head_t *h = head(r);
        int16_t old = b->live[h->slot];
        if (old >= 0 && (int32_t)(h->seq - head((uint32_t)old)->seq) <= 0) continue;
        if (old >= 0) b->state[(uint32_t)old] = PRESET_STALE;
        b->live[h->slot] = (int16_t)r;
        b->state[r] = PRESET_LIVE;
        if ((int32_t)(h->seq - b->seq) > 0) { b->seq = h->seq; b->next = (r + 1u) % PRESET_LOG_RECORDS; }
    }
}
const preset_t *preset_user(const preset_bank_t *b, uint32_t slot)
{
    if (slot >= PRESET_USERS) return 0;
    for (uint32_t n = 0; n < b->n_pending; n++)
        if (b->pending_slot[n] == slot) return &b->pending[n];
    return b->live[slot] >= 0 ? sound((uint32_t)b->live[slot]) : 0;
}
static uint32_t free_count(const preset_bank_t *b)
{
    uint32_t n = 0;
    for (uint32_t r = 0; r < PRESET_LOG_RECORDS; r++) n += b->state[r] == PRESET_FREE;
    return n;
}
static int append(preset_bank_t *b, uint32_t slot, const preset_t *p, int excluded_sector)
{
    uint32_t r, n;
    for (n = 0; n < PRESET_LOG_RECORDS; n++) {
        r = (b->next + n) % PRESET_LOG_RECORDS;
        if (b->state[r] == PRESET_FREE && (int)(r / PER_SECTOR) != excluded_sector) break;
    }
    if (n == PRESET_LOG_RECORDS) return -1;
    b->next = (r + 1u) % PRESET_LOG_RECORDS;
    log_head_t h = {LOG_MAGIC, (uint16_t)slot, sizeof *p, ++b->seq, bank_crc32(p, sizeof *p, 0), 0};
    h.hcrc = bank_crc32(&h, offsetof(log_head_t, hcrc), 0);
    b->state[r] = PRESET_STALE;             /* a failed/cut-short record is never reused without erasing */
    if (plat_data_write(off(r) + sizeof h, p, sizeof *p) || plat_data_write(off(r), &h, sizeof h) || !committed(r))
        return -1;
    if (b->live[slot] >= 0) b->state[(uint32_t)b->live[slot]] = PRESET_STALE;
    b->live[slot] = (int16_t)r;
    b->state[r] = PRESET_LIVE;
    return 0;
}
static int compact(preset_bank_t *b)
{
    const uint32_t available = free_count(b);
    int victim = -1;
    uint32_t cost = PER_SECTOR + 1u;
    for (uint32_t n = 0; n < SECTORS; n++) {
        const uint32_t s = (b->next / PER_SECTOR + n) % SECTORS;
        uint32_t live = 0, stale = 0, free = 0;
        for (uint32_t j = 0; j < PER_SECTOR; j++) {
            live += b->state[s * PER_SECTOR + j] == PRESET_LIVE;
            stale += b->state[s * PER_SECTOR + j] == PRESET_STALE;
            free += b->state[s * PER_SECTOR + j] == PRESET_FREE;
        }
        if (stale && live <= available - free && live < cost) { victim = (int)s; cost = live; }
    }
    if (victim < 0) return -1;
    for (uint32_t j = 0; j < PER_SECTOR; j++) {
        const uint32_t r = (uint32_t)victim * PER_SECTOR + j;
        if (b->state[r] != PRESET_LIVE) continue;
        preset_t copy = *sound(r);          /* flash write's source MUST be in RAM */
        if (append(b, head(r)->slot, &copy, victim)) return -1;
    }
    if (plat_data_erase(PRESET_LOG_LO + (uint32_t)victim * 4096u)) return -1;
    for (uint32_t j = 0; j < PER_SECTOR; j++) b->state[(uint32_t)victim * PER_SECTOR + j] = PRESET_FREE;
    return 0;
}
int preset_store_flush(preset_bank_t *b)
{
    if (!b->n_pending) return 0;
    if (!b->ok) return -1;
#ifdef PLAT_HOST
    if (fail_writes) return -1;
#endif
    /* Preserve enough erased records to evacuate a whole sector, including after a cut during compaction. */
    for (uint32_t n = 0; free_count(b) <= PER_SECTOR; n++)
        if (n >= SECTORS || compact(b)) return -1;
    if (append(b, b->pending_slot[0], &b->pending[0], -1)) return -1;
    b->n_pending--;
    for (uint32_t n = 0; n < b->n_pending; n++) {
        b->pending_slot[n] = b->pending_slot[n + 1];
        b->pending[n] = b->pending[n + 1];
    }
    return 1;
}
int preset_store_import_021(preset_bank_t *b, const void *data, uint32_t size)
{
    typedef struct { uint8_t engine, osc[3], tune, shape[3]; char name[11]; uint8_t drum; } old_sound_t;
    typedef struct { uint32_t magic; uint16_t version, reserved; uint32_t used[2]; old_sound_t slot[64]; } old_bank_t;
    if (size != sizeof(old_bank_t)) return 0;
    const old_bank_t *old = (const old_bank_t *)data;
    if (old->magic != 0x47525031u || old->version != 1 || old->reserved) return 0;
    /* Validate every occupied legacy slot before writing anything. */
    for (uint32_t s = 0; s < 64; s++) if (old->used[s / 32] >> (s % 32) & 1u) {
        preset_t p;
        memcpy(&p, &old->slot[s], sizeof(old_sound_t));
        grav_controls_init(&p.controls);
        if (!preset_valid(&p)) return 0;
    }
    for (uint32_t s = 0; s < 64; s++) if ((old->used[s / 32] >> (s % 32) & 1u) && !preset_used(b, s)) {
        preset_t p;
        memcpy(&p, &old->slot[s], sizeof(old_sound_t));
        grav_controls_init(&p.controls);
        if (!preset_put(b, s, &p) || preset_store_flush(b) < 0) return -1;
    }
    return 1;
}
