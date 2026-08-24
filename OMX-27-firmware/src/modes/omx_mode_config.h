#pragma once

#include "omx_mode_interface.h"
#include "../utils/param_manager.h"
#include "submodes/submode_interface.h"
#include "../utils/music_scales.h"

// Global settings / CONFIG mode. Appears at the end of the mode rotation.
// Gathers the device's global settings into one place (additive - the settings
// still live in their per-mode pages too for now).
//
// Input model (no notes played in this mode):
//   Keys 1 / 2      : select prev / next param on the page
//   Keys 19-22      : hold to quick-edit param 0-3 directly (as if selected + AUX);
//                     for "action" params a press triggers the action
//   Hold AUX + enc  : edit the selected param
//   Encoder click   : toggle select / edit of the selected param
class OmxModeConfig : public OmxModeInterface
{
public:
	OmxModeConfig();
	~OmxModeConfig() {}

	void InitSetup() override;
	void onModeActivated() override;
	void onModeDeactivated() override;

	void onPotChanged(int potIndex, int prevValue, int newValue, int analogDelta) override;
	void loopUpdate(Micros elapsedTime) override;

	void updateLEDs() override;
	void onEncoderChanged(Encoder::Update enc) override;
	void onEncoderButtonDown() override;
	void onEncoderButtonDownLong() override {}
	bool shouldBlockEncEdit() override;

	void onKeyUpdate(OMXKeypadEvent e) override;
	void onKeyHeldUpdate(OMXKeypadEvent e) override;

	void onDisplayUpdate() override;

	void SetScale(MusicScales *scale) { musicScale_ = scale; }

private:
	ParamManager params;
	MusicScales *musicScale_ = nullptr;
	String tempString;

	bool initSetup = false;
	bool encoderSelect = true; // true = encoder selects param, false = edits value

	// Quick-edit: keys 19-22 map to params 0-3. -1 = none held.
	int8_t heldParam_ = -1;

	// True while the selected param should be edited by the encoder.
	bool inEditMode();

	// "Action" params trigger something (Save / Clear Storage / Pot config) instead
	// of editing a value.
	bool isActionParam(int8_t page, int8_t param);
	void doAction(int8_t page, int8_t param);

	// Some params are intentional gaps (empty slots) to space out the layout.
	bool isGapParam(int8_t page, int8_t param);
	// Change selected param, skipping over gap params.
	void navParam(int8_t dir);

	void onEncoderChangedEditParam(Encoder::Update enc);

	// Submodes (pot config, clear storage) launched from within CONFIG.
	SubmodeInterface *activeSubmode = nullptr;
	void enableSubmode(SubmodeInterface *subMode);
	void disableSubmode();
	bool isSubmodeEnabled();
};
