#include <assert.h>
#include <limits.h>
#include <stdio.h>
#include <string.h>
#include "presets/presets.h"
#include "dsp/grav_plaits.h"

int main(void)
{
    preset_bank_t b, saved;
    preset_t p, q;
    memset(&b, 0xff, sizeof b);             /* old BLE / erased sectors must never appear as presets */
    assert(!presets_valid(&b));
    presets_init(&b);
    assert(presets_valid(&b));
    unsigned factories = 0;
    for (unsigned drum = 0; drum < 2; drum++)
        for (unsigned e = 0; e < GRAV_ENGINE_ALL; e++)
            for (int id = 0; id < (int)PRESET_FACTORY; id++)
                if (preset_get(&b, e, drum, id, &p)) {
                    assert(preset_put(&b, 63, &p));
                    assert(presets_valid(&b));
                    assert(preset_get(&b, e, drum, 67, &q));
                    assert(!memcmp(&p, &q, sizeof p));
                    assert(preset_match(&b, &p) == 67);
                    ++factories;
                }
    assert(factories == 120);              /* 24 synth engines + 6 dedicated drums, four each */
    presets_init(&b);
    for (unsigned i = 0; i < PRESET_USERS; i++) {
        assert(preset_get(&b, i % 24, 0, i % 4, &p));
        p.osc[0] = i;
        snprintf(p.name, sizeof p.name, "USER %02u", i + 1);
        assert(preset_put(&b, i, &p));
    }
    saved = b;
    assert(presets_valid(&saved));
    for (unsigned i = 0; i < PRESET_USERS; i++) {
        assert(preset_get(&saved, i % 24, 0, i + 4, &p));
        assert(p.osc[0] == i);
        assert(preset_match(&saved, &p) == (int)i + 4);
        assert(!preset_get(&saved, (i + 1) % 24, 0, i + 4, &q));
    }
    assert(!preset_put(&b, 64, &p));
    assert(!preset_get(&b, 0, 0, INT_MAX, &p));
    assert(!preset_get(&b, GRAV_ENGINE_ALL, 0, 0, &p));
    assert(!preset_get(&b, GRAV_ENGINE_SYN_KICK, 0, 0, &p));
    assert(!preset_get(&b, GRAV_ENGINE_FM, 1, 0, &p));
    assert(!preset_used(&b, 64));
    /* Traversal skips other engines and kinds, wraps both ways, handles large encoder deltas and CUSTOM. */
    presets_init(&b);
    assert(preset_get(&b, GRAV_ENGINE_BASS_DRUM, 1, 1, &p));
    assert(preset_put(&b, 31, &p));
    assert(preset_next(&b, p.engine, 1, 3, 1) == 35);
    assert(preset_next(&b, p.engine, 1, 35, 1) == 0);
    assert(preset_next(&b, p.engine, 1, 0, -1) == 35);
    assert(preset_next(&b, p.engine, 0, 3, 1) == 0); /* same engine on synth track: separate controls */
    assert(preset_next(&b, p.engine, 1, -1, 1) == 0);
    assert(preset_next(&b, p.engine, 1, -1, -1) == 35);
    assert(preset_next(&b, p.engine, 1, 0, INT_MAX) == 2);
    assert(preset_next(&b, p.engine, 1, 0, INT_MIN) == 2);
    saved = b;
    b.version++; assert(!presets_valid(&b)); b = saved;
    b.magic++; assert(!presets_valid(&b)); b = saved;
    b.reserved++; assert(!presets_valid(&b)); b = saved;
    b.slot[31].osc[0] = 101; assert(!presets_valid(&b)); b = saved;
    b.slot[31].shape[2] = 101; assert(!presets_valid(&b)); b = saved;
    b.slot[31].tune = 101; assert(!presets_valid(&b)); b = saved;
    b.slot[31].drum = 2; assert(!presets_valid(&b)); b = saved;
    b.slot[31].engine = 0; assert(!presets_valid(&b)); b = saved;
    memset(b.slot[31].name, 'A', 11); assert(!presets_valid(&b)); b = saved;
    b.slot[31].name[0] = 0; assert(!presets_valid(&b)); b = saved;
    b.slot[31].name[0] = 1; assert(!presets_valid(&b));
    puts("PASS: 120 factory sounds, all 64 slots, engine/kind filtering, traversal, record validation");
    return 0;
}
