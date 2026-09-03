#pragma once
#include "../ClearUI/ClearUI_Input.h"
// #include "../../hardware/omx_keypad.h"
// #include "../../utils/param_manager.h"
#include "../utils/music_scales.h"

enum ShortCutMode
{
	FORMSHORTCUT_NONE, // No shortcut keys
	FORMSHORTCUT_AUX,  // Aux shortcut held
	FORMSHORTCUT_F1,   // Top key 1 held
	FORMSHORTCUT_F2,   // Top key 2 held
	FORMSHORTCUT_F3,   // Top key 1 & 2 held
};

extern const uint8_t kSeqRates[];
extern const uint8_t kNumSeqRates;

// Singleton class that form machines and base form mode can use to stay in sync
class OmxFormGlobalSettings
{
public:
bool encoderSelect = false;
bool isPlaying = false;

// Set to true for F1 Copy and F2 Cut shortcuts once something is added to buffer
bool shortcutPaste = false;

bool auxBlock = false;

bool useNoteNumbers = false;

// Live recording (§7): armed = keyboard notes quantize into the selected track while playing.
bool recArm = false;
bool recReplace = false; // false = overdub (add to step), true = replace (clear step first)

// Whether a selected AUX macro (M8/NRN/DEL) may take the pots in FORM. Default false so the
// knobs stay on the track's CC bank; enable to let the macro drive them. (FORM-only — applied
// to FORM's AuxMacroManager instance; other modes keep the macro's own behavior.)
bool macroConsumesPots = false;

// Note-entry behavior for the step editors: false = Pressed (a fresh press replaces the
// step's notes), true = Toggle (each press adds/removes that note — drum programming).
bool noteEntryToggle = false;

// True while any track is soloed — non-soloed tracks are inaudible. Recomputed by the
// shell every loop; machines consult it in their audibility check.
bool anySolo = false;

MusicScales *musicScale;

uint8_t shortcutMode;

};

extern OmxFormGlobalSettings omxFormGlobal;