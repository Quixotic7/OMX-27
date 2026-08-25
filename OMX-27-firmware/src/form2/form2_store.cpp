#include "form2_store.h"
#include "../hardware/storage.h"

namespace form2
{
    void PatternStore::init()
    {
        globals = Globals();
        globals.bpmX10 = 1200; // 120.0 BPM
        globals.clockSource = 0;
        globals.scaleRoot = 0;
        globals.scalePattern = -1; // chromatic
        globals.swing = 0;
        globals.groove = 0;
        globals.curPattern = 0;
        globals.flags = 0;

        for (uint8_t i = 0; i < FORM_NUM_PATTERNS; i++)
            patterns[i] = Pattern();
    }

    void PatternStore::copyPattern(uint8_t from, uint8_t to)
    {
        if (from < FORM_NUM_PATTERNS && to < FORM_NUM_PATTERNS && from != to)
            patterns[to] = patterns[from];
    }

    void PatternStore::clearPattern(uint8_t i)
    {
        if (i < FORM_NUM_PATTERNS)
            patterns[i] = Pattern();
    }

    int PatternStore::storageSize()
    {
        return 1 + (int)sizeof(Globals) + (int)sizeof(Pattern) * FORM_NUM_PATTERNS;
    }

    int PatternStore::saveToStorage(int address, Storage *storage)
    {
        // Skip if the image won't fit this platform's storage (see header note).
        if (address + storageSize() > storage->capacity())
            return address;

        storage->write(address++, kProjectSaveVersion);
        storage->writeArray(address, (uint8_t *)&globals, sizeof(Globals));
        address += sizeof(Globals);
        storage->writeArray(address, (uint8_t *)patterns, sizeof(Pattern) * FORM_NUM_PATTERNS);
        address += sizeof(Pattern) * FORM_NUM_PATTERNS;
        return address;
    }

    int PatternStore::loadFromStorage(int address, Storage *storage)
    {
        if (address + storageSize() > storage->capacity())
            return address;

        uint8_t ver = storage->read(address++);
        if (ver == kProjectSaveVersion)
        {
            storage->readArray(address, (uint8_t *)&globals, sizeof(Globals));
            address += sizeof(Globals);
            storage->readArray(address, (uint8_t *)patterns, sizeof(Pattern) * FORM_NUM_PATTERNS);
            address += sizeof(Pattern) * FORM_NUM_PATTERNS;
        }
        else
        {
            init(); // version mismatch → clean defaults
            address += sizeof(Globals) + sizeof(Pattern) * FORM_NUM_PATTERNS;
        }
        return address;
    }

    // ---- Phase-1 compile gate: pin the sizes we measured on host (GNU bitfield ABI).
    // If arm-gcc packs differently these fire with the real numbers — that's the gate.
    static_assert(sizeof(Step) == 18, "form2::Step size drifted (expected 18)");
    static_assert(sizeof(Track) == 1162, "form2::Track size drifted (expected 1162)");
    static_assert(sizeof(Pattern) == 9296, "form2::Pattern size drifted (expected 9296)");
} // namespace form2
