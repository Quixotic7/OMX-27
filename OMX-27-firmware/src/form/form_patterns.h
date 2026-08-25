#pragma once
// FORM v2 — pattern data layer (built on the working OMNI structs, per the Phase-2
// data-model decision). A pattern is a whole-sequencer snapshot: the per-track OmniSeq
// data for all tracks. The live/active pattern lives in the 8 machines; the bank holds
// the rest. Counts are the per-platform caps from form2_config.h.

#include "machines/omni_structs.h"  // FormOmni::OmniSeq
#include "../form2/form2_config.h"  // FORM_NUM_PATTERNS, FORM_NUM_TRACKS

// One pattern = every track's sequencer data (steps + track/seq settings).
struct FormPattern
{
    FormOmni::OmniSeq tracks[FORM_NUM_TRACKS];
};
