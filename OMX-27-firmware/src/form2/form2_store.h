#pragma once
// FORM v2 — pattern store (Phase 1). Owns the single project (globals + all patterns).
// Phase 1 keeps every pattern resident in RAM (measured to fit V3/T4; T31 uses a reduced
// count via form2_config.h). Persistence is versioned; see the note on saveToStorage.

#include "form2_config.h"

class Storage; // fwd (device-only; defined in hardware/storage.h)

namespace form2
{
    static const uint8_t kProjectSaveVersion = 1;

    class PatternStore
    {
    public:
        Globals globals;
        Pattern patterns[FORM_NUM_PATTERNS];

        PatternStore() { init(); }
        void init(); // reset globals + all patterns to defaults

        uint8_t activeIndex() const { return globals.curPattern < FORM_NUM_PATTERNS ? globals.curPattern : 0; }
        Pattern &active() { return patterns[activeIndex()]; }
        Pattern &at(uint8_t i) { return patterns[i < FORM_NUM_PATTERNS ? i : 0]; }
        uint8_t count() const { return FORM_NUM_PATTERNS; }

        void copyPattern(uint8_t from, uint8_t to);
        void clearPattern(uint8_t i);

        // Persistence: [version byte][globals][all patterns] raw blit. Returns next address.
        // NOTE: the full 16-pattern image (~145 KB) exceeds FRAM (32 KB) / EEPROM; if it
        // won't fit `storage->capacity()`, save/load are skipped (return unchanged) until
        // the V3 flash-filesystem backend lands (Phase 12). Small projects still persist.
        static int storageSize();
        int saveToStorage(int address, Storage *storage);
        int loadFromStorage(int address, Storage *storage);
    };
} // namespace form2
