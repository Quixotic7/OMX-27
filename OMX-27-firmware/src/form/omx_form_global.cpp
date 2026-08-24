#include "omx_form_global.h"

// Rate for a bar, 1 is 1 bar
const uint8_t kSeqRates[] = {1, 2, 3, 4, 5, 6, 8, 10, 12, 16, 20, 24, 32, 40, 48, 64};
const uint8_t kNumSeqRates = 16;

// ticksPerStep
// 1 = 384, 3 = 192, 4 = 128, 4 = 96, 5 = 76.8, 6 = 64, 8 = 48, 10 = 38.4, 12 = 32, 16 = 24, 20 = 19.2, 24 = 16, 32 = 12, 40 = 9.6, 48 = 8, 64 = 6

OmxFormGlobalSettings omxFormGlobal;