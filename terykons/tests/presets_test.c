#include <assert.h>
#include <limits.h>
#include <stdio.h>
#include <stddef.h>
#include <string.h>
#include "presets/presets.h"
#include "presets/store.h"
#include "seq/bank.h"
#include "dsp/grav_plaits.h"

static uint8_t flash[PRESET_LOG_HI - PRESET_LOG_LO], saved_flash[sizeof flash];
static int budget = -1;
static unsigned erases[8], writes;
const uint8_t *plat_data(uint32_t off) {
    return off >= PRESET_LOG_LO && off < PRESET_LOG_HI ? flash + off - PRESET_LOG_LO : 0;
}
int plat_data_write(uint32_t off, const void *data, uint32_t n) {
    assert(off >= PRESET_LOG_LO && off + n <= PRESET_LOG_HI);
    assert((off & 4095u) + n <= 0xF00u); /* last page is reserved for loader compatibility */
    unsigned take = budget < 0 || (unsigned)budget >= n ? n : (unsigned)budget;
    for (unsigned i = 0; i < take; i++) flash[off - PRESET_LOG_LO + i] &= ((const uint8_t *)data)[i];
    if (budget >= 0) budget -= take;
    writes++;
    return take == n ? 0 : -1;
}
int plat_data_erase(uint32_t off) {
    assert((off & 4095u) == 0 && off >= PRESET_LOG_LO && off + 4096 <= PRESET_LOG_HI);
    unsigned take = budget < 0 || budget >= 4096 ? 4096 : (unsigned)budget;
    memset(flash + off - PRESET_LOG_LO, 255, take);
    if (budget >= 0) budget -= take;
    erases[(off - PRESET_LOG_LO) / 4096]++;
    return take == 4096 ? 0 : -1;
}
static void save(preset_bank_t *b, unsigned slot, const preset_t *p) {
    assert(preset_put(b, slot, p));
    assert(preset_store_flush(b) == 1);
}
static void check_slots(preset_bank_t *b, const preset_t expected[64]) {
    assert(presets_valid(b));
    for (unsigned s = 0; s < 64; s++) {
        const preset_t *p = preset_user(b, s);
        assert(p && !memcmp(p, &expected[s], sizeof *p));
    }
}
int main(void) {
    preset_bank_t b;
    preset_t p, q, expected[64];
    memset(flash, 255, sizeof flash);
    preset_store_init(&b);
    assert(b.ok && presets_valid(&b));
    unsigned factories = 0;
    for (unsigned drum = 0; drum < 2; drum++)
        for (unsigned e = 0; e < GRAV_ENGINE_ALL; e++)
            for (int id = 0; id < (int)PRESET_FACTORY; id++)
                if (preset_get(&b, e, drum, id, &p)) {
                    assert(preset_valid(&p));
                    assert(preset_put(&b, 63, &p));
                    assert(preset_get(&b, e, drum, 67, &q));
                    assert(!memcmp(&p, &q, sizeof p));
                    assert(preset_match(&b, &p) == 67);
                    ++factories;
                }
    assert(factories == 120);
    flash[0xF06] = 'A'; flash[0xF07] = 'T';
    preset_store_init(&b);
    save(&b, 63, &p);
    assert(b.live[63] >= 15);             /* a dirty loader guard page makes its sector unavailable */
    memset(flash, 255, sizeof flash);
    presets_init(&b);
    for (unsigned s = 0; s < 8; s++) assert(preset_put(&b, s, &p));
    assert(!preset_put(&b, 8, &p)); /* a full pending queue explicitly refuses a ninth distinct slot */
    assert(preset_put(&b, 7, &p));
    preset_store_init(&b);
    for (unsigned s = 0; s < 64; s++) {
        assert(preset_get(&b, s % 24, 0, s % 4, &p));
        p.osc[0] = s;
        p.controls.level = 100 - s;
        snprintf(p.name, sizeof p.name, "USER %02u", s + 1);
        save(&b, s, &p);
        expected[s] = p;
    }
    preset_store_init(&b);
    check_slots(&b, expected);
    for (unsigned s = 0; s < 64; s++) {
        assert(preset_get(&b, s % 24, 0, s + 4, &p));
        assert(preset_match(&b, &p) == (int)s + 4);
        assert(!preset_get(&b, (s + 1) % 24, 0, s + 4, &q));
    }
    assert(!preset_put(&b, 64, &p));
    assert(!preset_get(&b, 0, 0, INT_MAX, &p));
    assert(!preset_get(&b, GRAV_ENGINE_ALL, 0, 0, &p));
    assert(!preset_get(&b, GRAV_ENGINE_SYN_KICK, 0, 0, &p));
    assert(!preset_get(&b, GRAV_ENGINE_FM, 1, 0, &p));
    assert(!preset_used(&b, 64));
    /* Every cut within payload/header leaves the previous committed sound available after boot. */
    memcpy(saved_flash, flash, sizeof flash);
    p = expected[0]; p.osc[0] = 100; save(&b, 0, &p);
    const unsigned damaged = (unsigned)b.live[0];
    flash[damaged / 15 * 4096 + damaged % 15 * 256 + 32] ^= 1;
    preset_store_init(&b); check_slots(&b, expected); /* bad newest CRC falls back to the previous commit */
    for (int cut = 0; cut < 204; cut++) {
        memcpy(flash, saved_flash, sizeof flash);
        preset_store_init(&b);
        p = expected[0]; p.osc[0] = 100;
        assert(preset_put(&b, 0, &p));
        budget = cut;
        assert(preset_store_flush(&b) < 0);
        budget = -1;
        preset_store_init(&b);
        check_slots(&b, expected);
    }
    memcpy(flash, saved_flash, sizeof flash);
    preset_store_init(&b);
    /* Compaction, wear and reboot under sustained replacement of all 64 slots. */
    for (unsigned n = 0; n < 2000; n++) {
        const unsigned s = n % 64;
        p = expected[s]; p.osc[0] = n % 101;
        save(&b, s, &p); expected[s] = p;
        if (n % 67 == 0) { preset_store_init(&b); check_slots(&b, expected); }
    }
    preset_store_init(&b); check_slots(&b, expected);
    for (unsigned s = 0; s < 8; s++) {
        assert(erases[s]);
        for (unsigned j = 0xF00; j < 4096; j++) assert(flash[s * 4096 + j] == 255);
    }
    /* Interrupted compaction: cuts span copied records, commit headers and the sector erase. */
    for (;;) {
        unsigned free = 0;
        for (unsigned r = 0; r < PRESET_LOG_RECORDS; r++) free += b.state[r] == PRESET_FREE;
        if (free <= 15) break;
        save(&b, 0, &expected[0]);
    }
    memcpy(saved_flash, flash, sizeof flash);
    for (int cut = 0; cut < 7000; cut += 37) {
        memcpy(flash, saved_flash, sizeof flash);
        preset_store_init(&b);
        p = expected[0]; p.osc[0] = 99;
        assert(preset_put(&b, 0, &p));
        budget = cut; const int rc = preset_store_flush(&b); budget = -1;
        preset_store_init(&b);
        for (unsigned s = 0; s < 64; s++) {
            const preset_t *loaded = preset_user(&b, s);
            assert(loaded);
            assert(!memcmp(loaded, rc == 1 && s == 0 ? &p : &expected[s], sizeof p));
        }
    }
    /* Legacy bank import survives interruption, retains all 64 slots and never replaces newer sounds. */
    typedef struct { uint8_t engine, osc[3], tune, shape[3]; char name[11]; uint8_t drum; } old_p;
    struct { uint32_t magic; uint16_t version, reserved; uint32_t used[2]; old_p slot[64]; } old = {0};
    old.magic = 0x47525031u; old.version = 1; old.used[0] = old.used[1] = UINT_MAX;
    for (unsigned s = 0; s < 64; s++) memcpy(&old.slot[s], &expected[s], sizeof(old_p));
    memset(flash, 255, sizeof flash); preset_store_init(&b);
    budget = 500; assert(preset_store_import_021(&b, &old, sizeof old) < 0); budget = -1;
    preset_store_init(&b);
    assert(preset_store_import_021(&b, &old, sizeof old) == 1);
    for (unsigned s = 0; s < 64; s++) {
        const preset_t *loaded = preset_user(&b, s);
        assert(loaded && !memcmp(loaded, &old.slot[s], sizeof(old_p)));
        assert(loaded->controls.level == 100 && loaded->controls.value[0] == GRAV_AUTO);
    }
    p = *preset_user(&b, 0); p.osc[0] = 100; save(&b, 0, &p);
    assert(preset_store_import_021(&b, &old, sizeof old) == 1);
    assert(preset_user(&b, 0)->osc[0] == 100);
    q = p; q.osc[0] = 101; assert(!preset_valid(&q));
    q = p; q.controls.level = 101; assert(!preset_valid(&q));
    q = p; q.controls.flags = GRAV_CUSTOM_FM; assert(!preset_valid(&q));
    q = p; memset(q.name, 'A', 11); assert(!preset_valid(&q));
    q = p; q.name[0] = 1; assert(!preset_valid(&q));
    assert(!preset_get(&b, 0, 0, INT_MIN, &q));
    /* (0.37) a sound saved before (its 184 bytes, the record's rest erased: no LFO, no envelope) is read and valid,
     * its LFO and envelope none; one saved now brings them back; out of range, refused */
    memset(flash, 255, sizeof flash);
    {
        typedef struct { uint32_t magic; uint16_t slot, size; uint32_t seq, crc, hcrc; } head_t;
        preset_t s;
        uint8_t rec[256];
        assert(preset_get(&b, 0, 0, 1, &s) && s.mod_set == PRESET_MOD);
        head_t h = {0x50525332u, 5, PRESET_SIZE_036, 1, bank_crc32(&s, PRESET_SIZE_036, 0), 0};
        h.hcrc = bank_crc32(&h, offsetof(head_t, hcrc), 0);
        memset(rec, 255, sizeof rec);
        memcpy(rec, &h, sizeof h);
        memcpy(rec + sizeof h, &s, PRESET_SIZE_036);
        assert(plat_data_write(PRESET_LOG_LO, rec, sizeof h + PRESET_SIZE_036) == 0);
        preset_store_init(&b);
        const preset_t *before = preset_user(&b, 5);
        assert(before && before->mod_set != PRESET_MOD && !memcmp(before->osc, s.osc, 3) && presets_valid(&b));
        p = s;
        p.env[0] = 77;
        p.lfo[1] = 33;
        save(&b, 6, &p);
        preset_store_init(&b);
        assert(preset_user(&b, 6)->mod_set == PRESET_MOD && preset_user(&b, 6)->env[0] == 77 &&
               preset_user(&b, 6)->lfo[1] == 33 && preset_user(&b, 5)->mod_set != PRESET_MOD);
        q = p; q.env[2] = 101; assert(!preset_valid(&q));
        q = p; q.drum = 1; assert(!preset_valid(&q));
    }
    puts("PASS: 120 factory sounds, 64 expanded slots, queue bounds, record cuts, compaction, wear, legacy import, LFO and envelope");
    return 0;
}
