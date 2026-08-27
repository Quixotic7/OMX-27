#include "omx_mode_form.h"
#include "../config.h"
#include "../globals.h" // sysSettings/potSettings/midiMacroConfig moved here in the q7/RP2040 restructure
#include "../consts/colors.h"
#include "../utils/omx_util.h"
#include "../utils/cvNote_util.h"
#include "../hardware/omx_disp.h"
#include "../hardware/omx_leds.h"
#include "../midi/midi.h"
#include "../utils/music_scales.h"
#include "../midi/noteoffs.h"
#include "machines/form_machine_omni.h"
#include "omx_form_global.h"

enum FormModePage
{
	FORMPAGE_INSPECT,		// Sent Pot CC, Last Note, Last Vel, Last Chan, Not editable, just FYI
	FORMPAGE_DRUMKEY,		// Note, Chan, Vel, MidiFX
	FORMPAGE_DRUMKEY2,		// Hue, RND Hue, Copy, Paste
	FORMPAGE_SCALES,		// Hue,
	FORMPAGE_POTSANDMACROS, // PotBank, Thru, Macro, Macro Channel
	FORMPAGE_CFG,
	FORMPAGE_NUMPAGES
};

const char *kMachineNames[FORMMACH_COUNT] = {"NONE", "OMNI"};

const int kMachineColors[FORMMACH_COUNT] = {LEDOFF, ORANGE};

OmxModeForm::OmxModeForm()
{
	params.addPages(FORMPAGE_NUMPAGES);

	auxMacroManager_.setContext(this);
	auxMacroManager_.setMacroNoteOn(&OmxModeForm::doNoteOnForwarder);
	auxMacroManager_.setMacroNoteOff(&OmxModeForm::doNoteOffForwarder);
	auxMacroManager_.setSelectMidiFXFPTR(&OmxModeForm::selectMidiFXForwarder);

	presetManager.setContextPtr(this);
	presetManager.setDoSaveFunc(&OmxModeForm::doSaveKitForwarder);
	presetManager.setDoLoadFunc(&OmxModeForm::doLoadKitForwarder);

	stopped_ = true;

	// Setup default machines
	for (uint8_t i = 0; i < kNumMachines; i++)
	{
		machines_[i] = new FormOmni::FormMachineOmni();
		machines_[i]->setContext(this);
		machines_[i]->setNoteOnFptr(&OmxModeForm::seqNoteOnForwarder);
		machines_[i]->setNoteOffFptr(&OmxModeForm::seqNoteOffForwarder);
		trackHue_[i] = i * (256 / kNumMachines); // spread 8 hues around the wheel
	}

	machines_[0]->setTest();

	selectMachine(0);

	ledUpdateTime_ = 0;

	// machines_[0]->setContext(this);
	// machines_[0]->setNoteOnFptr(&OmxModeForm::seqNoteOnForwarder);
	// machines_[0]->setNoteOffFptr(&OmxModeForm::seqNoteOffForwarder);



	// char foo[sizeof(OmxModeForm)]
}

OmxModeForm::~OmxModeForm()
{
	omxLeds.setBlinkAutoRefresh(true);
	cleanup();
}

void OmxModeForm::cleanup()
{
	for (uint8_t i = 0; i < kNumMachines; i++)
	{
		if(machines_[i] != nullptr)
		{
			delete machines_[i];
			machines_[i] = nullptr;
		}
	}

	if(copyBuffer_ != nullptr)
	{
		delete copyBuffer_;
		copyBuffer_ = nullptr;
	}

	if(undoBuffer_ != nullptr)
	{
		delete undoBuffer_;
		undoBuffer_ = nullptr;
	}
}

void OmxModeForm::changeFormMode(uint8_t newFormMode)
{
	if (newFormMode < FORMMODE_COUNT)
	{
		omxFormGlobal.formMode = newFormMode;
	}
}

bool OmxModeForm::isMachineValid(uint8_t machineIndex)
{
	return machineIndex < kNumMachines;
}

const char *OmxModeForm::getMachineName(uint8_t machineIndex)
{
	if (isMachineValid(machineIndex) == false)
		return "";

	uint8_t mType = static_cast<uint8_t>(machines_[machineIndex]->getType());

	return kMachineNames[mType];
}

int OmxModeForm::getMachineColor(uint8_t machineIndex)
{
	if (isMachineValid(machineIndex) == false)
		return LEDOFF;

	uint8_t mType = static_cast<uint8_t>(machines_[machineIndex]->getType());

	return kMachineColors[mType];
}

void OmxModeForm::selectMachine(uint8_t machineIndex)
{
	if (isMachineValid(machineIndex) == false)
		return;

	selectedMachine_ = machineIndex;
	machines_[machineIndex]->onSelected();
}

FormMachineInterface *OmxModeForm::getSelectedMachine()
{
	return machines_[selectedMachine_];
}

void OmxModeForm::changeMachineAtIndex(uint8_t machineIndex, uint8_t machineType)
{
	if (isMachineValid(machineIndex) == false)
		return;

	// v2 single-engine: every track is the OMNI engine. machineType is retained in the
	// signature for save/load compatibility but no longer selects a type.
	(void)machineType;
	setMachineTo(machineIndex, new FormOmni::FormMachineOmni());
}

void OmxModeForm::cutMachineAt(uint8_t machineIndex)
{
	if (isMachineValid(machineIndex) == false)
		return;

	// Cut = copy the track, then reset the slot to an empty OMNI track (no NULL type).
	copyMachineAt(machineIndex);
	setMachineTo(machineIndex, new FormOmni::FormMachineOmni());
}

void OmxModeForm::copyMachineAt(uint8_t machineIndex)
{
	if (isMachineValid(machineIndex) == false)
		return;

	if (copyBuffer_ != nullptr)
	{
		delete copyBuffer_;
		copyBuffer_ = nullptr;
	}

	copyBuffer_ = machines_[machineIndex]->getClone();
}

void OmxModeForm::pasteMachineTo(uint8_t machineIndex)
{
	if (isMachineValid(machineIndex) == false)
		return;

	if (copyBuffer_ != nullptr)
	{
		setMachineTo(machineIndex, copyBuffer_->getClone());
	}
}

void OmxModeForm::setMachineTo(uint8_t machineIndex, FormMachineInterface *ptr)
{
	if (isMachineValid(machineIndex) == false)
		return;

	if (undoBuffer_ != nullptr)
	{
		delete undoBuffer_;
	}

	undoBuffer_ = machines_[machineIndex];

	machines_[machineIndex] = ptr;
	machines_[machineIndex]->setContext(this);
	machines_[machineIndex]->setMachineIndex(machineIndex);
	machines_[machineIndex]->setNoteOnFptr(&OmxModeForm::seqNoteOnForwarder);
	machines_[machineIndex]->setNoteOffFptr(&OmxModeForm::seqNoteOffForwarder);
}

// ---- Patterns (v2 data layer) ----
// Every track is an OMNI machine (single-engine), so we can snapshot / restore each
// track's OmniSeq to/from the pattern bank.
void OmxModeForm::snapshotActivePattern()
{
	for (uint8_t i = 0; i < kNumMachines; i++)
	{
		auto omni = static_cast<FormOmni::FormMachineOmni *>(machines_[i]);
		patterns_[activePattern_].tracks[i] = omni->getSeq();
	}
}

void OmxModeForm::loadPatternIntoMachines(uint8_t index)
{
	for (uint8_t i = 0; i < kNumMachines; i++)
	{
		auto omni = static_cast<FormOmni::FormMachineOmni *>(machines_[i]);
		omni->setSeq(patterns_[index].tracks[i]);
	}
}

void OmxModeForm::switchPattern(uint8_t index)
{
	if (index >= FORM_NUM_PATTERNS || index == activePattern_)
		return;

	snapshotActivePattern();      // keep edits to the pattern we're leaving
	activePattern_ = index;
	loadPatternIntoMachines(index);
}

void OmxModeForm::copyPatternTo(uint8_t from, uint8_t to)
{
	if (from >= FORM_NUM_PATTERNS || to >= FORM_NUM_PATTERNS || from == to)
		return;

	snapshotActivePattern(); // make sure the source (if it's active) is current
	patterns_[to] = patterns_[from];
	if (to == activePattern_)
		loadPatternIntoMachines(activePattern_); // reflect the paste in the live tracks
}

void OmxModeForm::clearPattern(uint8_t index)
{
	if (index >= FORM_NUM_PATTERNS)
		return;

	patterns_[index] = FormPattern(); // default (empty) tracks
	if (index == activePattern_)
		loadPatternIntoMachines(activePattern_);
}

// ---- v2 shell: view router ----
void OmxModeForm::setFormView(uint8_t view)
{
	if (view >= FORMVIEW_COUNT)
		return;
	formView_ = view;
	heldTrackKey_ = -1;

	// Editor views map to an OMNI UI mode, applied to every track so the view stays
	// consistent when you switch tracks. Patterns / MI are rendered by the container.
	uint8_t uiMode = 255;
	switch (view)
	{
	case FORMVIEW_MIX: uiMode = FormOmni::OMNIUIMODE_MIX; break;
	case FORMVIEW_STEP: uiMode = FormOmni::OMNIUIMODE_CONFIG; break;
	case FORMVIEW_TRANSPOSE: uiMode = FormOmni::OMNIUIMODE_TRANSPOSE; break;
	case FORMVIEW_NOTES: uiMode = FormOmni::OMNIUIMODE_NOTEEDIT; break;
	default: break;
	}
	if (uiMode != 255)
	{
		for (uint8_t i = 0; i < kNumMachines; i++)
			static_cast<FormOmni::FormMachineOmni *>(machines_[i])->setUiMode(uiMode);
	}

	static const char *kViewNames[FORMVIEW_COUNT] = {"MIX", "STEP", "TRANSPOSE", "NOTES", "PATTERNS", "MI"};
	omxDisp.displayMessage(kViewNames[view]);
	omxLeds.setDirty();
	omxDisp.setDirty();
}

// While AUX is held, keys 13-18 are the view selector: the selected (pending) view is
// lit WHITE, the rest dim. Whatever's lit is the view you'll drop into on release.
void OmxModeForm::updateAuxViewLEDs()
{
	for (uint8_t v = 0; v < FORMVIEW_COUNT; v++)
	{
		strip.setPixelColor(13 + v, v == pendingView_ ? WHITE : LOWWHITE);
	}
}

void OmxModeForm::updatePatternsLEDs()
{
	for (uint8_t i = 0; i < 16; i++)
	{
		uint32_t col = LEDOFF;
		if (i < FORM_NUM_PATTERNS)
			col = (i == activePattern_) ? WHITE : DKCYAN;
		strip.setPixelColor(11 + i, col);
	}
}

void OmxModeForm::onKeyUpdatePatterns(OMXKeypadEvent e)
{
	uint8_t k = e.key();
	if (!e.held() && e.down() && k >= 11 && k < 27)
	{
		uint8_t idx = k - 11;
		if (idx < FORM_NUM_PATTERNS)
			switchPattern(idx);
	}
}

void OmxModeForm::onDisplayPatterns()
{
	tempString = "P" + String(activePattern_ + 1) + "/" + String((int)FORM_NUM_PATTERNS);
	omxDisp.dispGenericModeLabelDoubleLine("PATTERNS", tempString.c_str(), 0, 0);
}

void OmxModeForm::onDisplayMI()
{
	omxDisp.dispGenericModeLabelDoubleLine("MI VIEW", "(todo)", 0, 0);
}

// ---- Step view (container-rendered v2 editor) ----

static const char *kStepModeNames[STEPMODE_COUNT] = {
	"NOTE", "VELOCITY", "LENGTH", "REPEAT", "CHANCE", "MATH", "FUNCTION", "MIDI FX"};

// Distinct colour per edit mode for the top-row selector.
static const uint32_t kStepModeColors[STEPMODE_COUNT] = {
	WHITE, YELLOW, CYAN, ORANGE, GREEN, MAGENTA, BLUE, RED};

static uint8_t mixPlayModeIndex(FormOmni::Track *t); // fwd decl (defined with the Mix helpers)

static const char *kPlayModeNames[5] = {"FWD", "REV", "FWD PONG", "REV PONG", "RANDOM"};

// Set a track's play mode from a 0-4 index (fwd/rev/fwd-pong/rev-pong/random).
static void setTrackPlayModeIdx(FormOmni::Track *t, uint8_t idx)
{
	switch (idx)
	{
	case 0: t->playDirection = FormOmni::TRACKDIRECTION_FORWARD; t->playMode = FormOmni::TRACKMODE_NONE; break;
	case 1: t->playDirection = FormOmni::TRACKDIRECTION_REVERSE; t->playMode = FormOmni::TRACKMODE_NONE; break;
	case 2: t->playDirection = FormOmni::TRACKDIRECTION_FORWARD; t->playMode = FormOmni::TRACKMODE_PONG; break;
	case 3: t->playDirection = FormOmni::TRACKDIRECTION_REVERSE; t->playMode = FormOmni::TRACKMODE_PONG; break;
	case 4: t->playMode = FormOmni::TRACKMODE_RAND; break;
	}
}

// Apply a palette value (or refresh the value message) to every held step.
void OmxModeForm::stepApplyToHeld(uint8_t paletteIndex)
{
	auto omni = static_cast<FormOmni::FormMachineOmni *>(getSelectedMachine());
	for (uint8_t s = 0; s < 16; s++)
		if (heldStepMask_ & (1 << s))
			omni->setStepPalette(s, stepEditMode_, paletteIndex);
	stepEdited_ = true;
	omxDisp.setDirty(); // the hold-step UI shows the new value — no popup message
	omxLeds.setDirty();
}

// Top row (3-10) selects the edit mode. Hold step(s) (11-26) → top row becomes the value
// palette for the current mode; single-click a step clears it (into the buffer for undo/paste).
// Multiple steps can be held to edit them together. In Function mode, pressing another step
// while holding a jump step sets that step as the jump target.
void OmxModeForm::onKeyUpdateStep(OMXKeypadEvent e)
{
	uint8_t thisKey = e.key();
	auto omni = static_cast<FormOmni::FormMachineOmni *>(getSelectedMachine());

	// While step(s) are held on the overview page, the top row is the value palette. On a
	// param page the menu (encoder) does the editing, so the palette is suppressed.
	if (heldStepMask_ != 0 && stepMenuPage_ == 0 && thisKey >= 1 && thisKey <= 10)
	{
		// Note mode: keys 1-10 = chord entry. Held keys build the chord; a fresh press (from
		// no note keys held) replaces. Notes audition while held.
		if (stepEditMode_ == STEPMODE_NOTE)
		{
			uint8_t degree = thisKey - 1;
			int8_t note = omxFormGlobal.musicScale->getNoteByDegree(degree, midiSettings.octave);
			if (e.down() && !e.held())
			{
				bool fresh = (heldNoteKeys_ == 0);
				for (uint8_t s = 0; s < 16; s++)
					if (heldStepMask_ & (1 << s))
					{
						if (fresh) omni->stepClearNotes(s);
						omni->stepAddNote(s, note);
					}
				heldNoteKeys_ |= (1 << degree);
				stepEdited_ = true;
				if (!omxFormGlobal.isPlaying)
					omni->previewNote(note, true); // audition only while stopped
				if (heldStepKey_ >= 0) omni->getStepNotes(heldStepKey_, lastNotes_); // remember chord
				omxDisp.setDirty();
				omxLeds.setDirty();
			}
			else if (!e.down())
			{
				heldNoteKeys_ &= ~(1 << degree);
				omni->previewNote(note, false);
				omxLeds.setDirty();
			}
			return;
		}

		// Other modes: MFX palette lives on keys 5-10 (Off + FX 1-5, like AUX); others on 1-N.
		if (e.down() && !e.held())
		{
			uint8_t base = (stepEditMode_ == STEPMODE_MFX) ? 5 : 1;
			if (thisKey >= base)
			{
				uint8_t p = thisKey - base;
				if (p < omni->stepPaletteCount(stepEditMode_))
					stepApplyToHeld(p);
			}
		}
		return;
	}

	// F1 + page keys (3-6): single-click selects the edit page; double-click solos that page;
	// hold one page + press another enables just that range (loop). Muted pages don't play.
	if (omxFormGlobal.shortcutMode == FORMSHORTCUT_F1 && (thisKey >= 3 && thisKey <= 6))
	{
		uint8_t p = thisKey - 3;
		if (e.down() && !e.held())
		{
			if (heldPageMask_ != 0 && !(heldPageMask_ & (1 << p)))
			{
				// loop-range: enable pages between the first held page and p.
				uint8_t a = 0;
				for (uint8_t i = 0; i < 4; i++) if (heldPageMask_ & (1 << i)) { a = i; break; }
				uint8_t lo = a < p ? a : p, hi = a < p ? p : a;
				uint8_t mask = 0;
				for (uint8_t i = lo; i <= hi; i++) mask |= (1 << i);
				omni->setEnabledPages(mask);
				omni->setActivePage(a);
				pageGestureDone_ = true;
			}
			heldPageMask_ |= (1 << p);
			omxDisp.setDirty();
			omxLeds.setDirty();
		}
		else if (!e.down() && (heldPageMask_ & (1 << p)))
		{
			if (!pageGestureDone_)
			{
				if (e.clicks() == 2)
				{
					omni->setEnabledPages(1 << p); // double-click = solo
					omni->setActivePage(p);
				}
				else if (e.quickClicked())
				{
					omni->setActivePage(p); // single-click = select edit page
				}
			}
			heldPageMask_ &= ~(1 << p);
			if (heldPageMask_ == 0)
				pageGestureDone_ = false;
			omxLeds.setDirty();
		}
		return;
	}
	// Release of an F2-held track key clears the hold (even if F2 was let go first).
	if (!e.down() && thisKey >= 3 && thisKey <= 10 && heldTrackKey_ == (int8_t)(thisKey - 3))
	{
		heldTrackKey_ = -1;
		omxDisp.setDirty();
		omxLeds.setDirty();
		return;
	}
	// F2 + top row (3-10) = select the track; holding one exposes its controls on the low row.
	if (omxFormGlobal.shortcutMode == FORMSHORTCUT_F2 && thisKey >= 3 && thisKey <= 10)
	{
		if (e.down() && !e.held())
		{
			uint8_t track = thisKey - 3;
			selectMachine(track);
			heldTrackKey_ = track; // the page's track box shows the selection — no popup
			omxLeds.setDirty();
			omxDisp.setDirty();
		}
		return;
	}
	// F2 + low row: with a track held = mute/solo/play mode/colour (Mix hold-track); otherwise
	// cut (a step with content) / paste (an empty step).
	if (omxFormGlobal.shortcutMode == FORMSHORTCUT_F2 && heldTrackKey_ >= 0 && thisKey >= 11 && thisKey < 27)
	{
		onKeyUpdateMixHold(e);
		return;
	}
	if (omxFormGlobal.shortcutMode == FORMSHORTCUT_F2 && heldTrackKey_ < 0 && e.down() && !e.held() && thisKey >= 11 && thisKey < 27)
	{
		uint8_t k = thisKey - 11;
		if (omni->stepIsOn(k))
		{
			omni->stepCut(k);
			omxDisp.displayMessage("CUT");
		}
		else
		{
			omni->stepPaste(k);
			omxDisp.displayMessage("PASTE");
		}
		return;
	}
	// F1 + step key = copy (a step with content) / paste (an empty step). F3 = structure layer.
	if (omxFormGlobal.shortcutMode == FORMSHORTCUT_F1 && e.down() && !e.held() && thisKey >= 11 && thisKey < 27)
	{
		uint8_t k = thisKey - 11;
		if (omni->stepIsOn(k))
		{
			omni->stepCopy(k);
			omxDisp.displayMessage("COPY");
		}
		else
		{
			omni->stepPaste(k);
			omxDisp.displayMessage("PASTE");
		}
		return;
	}
	if (omxFormGlobal.shortcutMode == FORMSHORTCUT_F3 && e.down() && !e.held() && thisKey >= 3 && thisKey <= 10)
	{
		omni->setRateShortcut(thisKey - 3); // F3 + top row = rate (like Mix)
		return;
	}
	if (omxFormGlobal.shortcutMode == FORMSHORTCUT_F3 && e.down() && !e.held() && thisKey >= 11 && thisKey < 27)
	{
		uint8_t page = omni->activePage();
		uint8_t pageLen = (thisKey - 11) + 1; // tapped step -> that page's length (1-16)
		omni->setPageLen(page, pageLen);
		omxDisp.displayMessage("P" + String(page + 1) + " LEN " + String(pageLen));
		omxLeds.setDirty();
		return;
	}

	if (omxFormGlobal.shortcutMode != FORMSHORTCUT_NONE)
		return; // other modifier combos: ignore

	// Mode selector (only when no step is held).
	if (heldStepMask_ == 0 && e.down() && !e.held() && thisKey >= 3 && thisKey <= 10)
	{
		stepEditMode_ = thisKey - 3;
		omxDisp.displayMessage(kStepModeNames[stepEditMode_]);
		omxLeds.setDirty();
		return;
	}

	// Step keys.
	if (thisKey >= 11 && thisKey < 27)
	{
		uint8_t key16 = thisKey - 11;
		if (e.down() && !e.held())
		{
			// Function mode: pressing another step while holding a jump step sets its target.
			if (stepEditMode_ == STEPMODE_FUNC && heldStepMask_ != 0 && !(heldStepMask_ & (1 << key16)) &&
				heldStepKey_ >= 0 && omni->stepIsJump(heldStepKey_))
			{
				for (uint8_t s = 0; s < 16; s++)
					if (heldStepMask_ & (1 << s))
						omni->setStepJumpTarget(s, key16);
				stepEdited_ = true;
				omxDisp.setDirty(); // hold UI shows the jump target
				omxLeds.setDirty();
				return;
			}
			bool firstHeld = (heldStepMask_ == 0);
			heldStepMask_ |= (1 << key16);
			heldStepKey_ = key16;
			if (firstHeld)
			{
				stepHoldStartMs_ = millis(); // hold-step UI waits a moment so quick-clicks don't flash it
				stepHoldUIShown_ = false;
			}
			// Preview the step's notes while stopped (Note mode previews via its note keys).
			if (!omxFormGlobal.isPlaying && stepEditMode_ != STEPMODE_NOTE)
				omni->auditionStep(key16, true);
			omxLeds.setDirty();
		}
		else if (!e.down() && (heldStepMask_ & (1 << key16)))
		{
			omni->auditionStep(key16, false); // stop any preview for this step
			// Quick tap (not a hold) toggles the step: a step with content clears; an empty
			// step is created (stamped with the last notes — middle C by default).
			if (heldStepMask_ == (uint16_t)(1 << key16) && !stepEdited_ && e.quickClicked())
			{
				if (omni->stepIsOn(key16))
					omni->stepCut(key16); // clear (no message — the grid shows it)
				else
					omni->stepSetNotes(key16, lastNotes_); // create
			}
			heldStepMask_ &= ~(1 << key16);
			if (heldStepKey_ == (int8_t)key16)
			{
				heldStepKey_ = -1; // refocus on another still-held step, if any
				for (int8_t s = 15; s >= 0; s--)
					if (heldStepMask_ & (1 << s)) { heldStepKey_ = s; break; }
			}
			if (heldStepMask_ == 0)
			{
				stepEdited_ = false;
				stepHoldUIShown_ = false;
				// Releasing the last step ends chord entry: note-off any auditioning notes.
				for (uint8_t d = 0; d < 10; d++)
					if (heldNoteKeys_ & (1 << d))
						omni->previewNote(omxFormGlobal.musicScale->getNoteByDegree(d, midiSettings.octave), false);
				heldNoteKeys_ = 0;
			}
			omxDisp.setDirty();
			omxLeds.setDirty();
		}
	}
}

void OmxModeForm::updateStepLEDs()
{
	auto omni = static_cast<FormOmni::FormMachineOmni *>(getSelectedMachine());
	bool blink = omxLeds.getBlinkState();

	// F1 / F2 keys lit in the track colour (brighter when that modifier is pressed). The
	// hold-a-step palette overrides keys 1-2, so this is skipped there.
	if (heldStepMask_ == 0)
	{
		uint32_t hueFull = strip.gamma32(strip.ColorHSV((uint16_t)trackHue_[selectedMachine_] << 8, 255, 255));
		uint32_t hueDim = (hueFull >> 3) & 0x1f1f1f;
		bool f1 = (omxFormGlobal.shortcutMode == FORMSHORTCUT_F1 || omxFormGlobal.shortcutMode == FORMSHORTCUT_F3);
		bool f2 = (omxFormGlobal.shortcutMode == FORMSHORTCUT_F2 || omxFormGlobal.shortcutMode == FORMSHORTCUT_F3);
		strip.setPixelColor(1, f1 ? hueFull : hueDim);
		strip.setPixelColor(2, f2 ? hueFull : hueDim);
	}

	// F1: top row 3-6 = pages; step row = content (copy targets).
	// Colours: selected = GREEN (RED if muted) · enabled = BLUE · muted = very dim ·
	// currently-playing page = flashing YELLOW.
	if (omxFormGlobal.shortcutMode == FORMSHORTCUT_F1 && heldStepMask_ == 0)
	{
		uint32_t hue = strip.gamma32(strip.ColorHSV((uint16_t)trackHue_[selectedMachine_] << 8, 255, 255));
		for (uint8_t k = 3; k <= 10; k++)
			strip.setPixelColor(k, LEDOFF);
		uint8_t en = omni->getEnabledPages();
		uint8_t sel = omni->activePage();
		uint8_t playingPage = omni->playingStepIndex() / 16;
		for (uint8_t p = 0; p < 4; p++)
		{
			bool enabled = en & (1 << p);
			uint32_t c;
			if (p == sel)
				c = enabled ? (uint32_t)GREEN : (uint32_t)RED;
			else if (enabled)
				c = (uint32_t)BLUE;
			else
				c = (uint32_t)VLOWWHITE;
			if (omxFormGlobal.isPlaying && p == playingPage && blink)
				c = (uint32_t)YELLOW; // flashing playhead page
			strip.setPixelColor(3 + p, c);
		}
		for (uint8_t i = 0; i < 16; i++)
			strip.setPixelColor(11 + i, omni->stepIsOn(i) ? hue : (uint32_t)LEDOFF);
		return;
	}
	// F2: top row 3-10 = the 8 tracks (track colour; selected white, muted red). Holding one
	// shows its controls on the low row (mute/solo/play mode/colour), else the low row is content.
	if (omxFormGlobal.shortcutMode == FORMSHORTCUT_F2 && heldStepMask_ == 0)
	{
		for (uint8_t t = 0; t < kNumMachines; t++)
		{
			uint32_t tc = strip.gamma32(strip.ColorHSV((uint16_t)trackHue_[t] << 8, 255, 255));
			uint32_t c = machines_[t]->getMute() ? (uint32_t)RED : tc;
			if (t == selectedMachine_)
				c = (uint32_t)WHITE;
			strip.setPixelColor(3 + t, c);
		}
		if (heldTrackKey_ >= 0)
		{
			updateMixHoldLEDs();
		}
		else
		{
			uint32_t hue = strip.gamma32(strip.ColorHSV((uint16_t)trackHue_[selectedMachine_] << 8, 255, 255));
			for (uint8_t i = 0; i < 16; i++)
				strip.setPixelColor(11 + i, omni->stepIsOn(i) ? hue : (uint32_t)LEDOFF);
		}
		return;
	}
	// F3 structure layer: the step row shows the track length — steps beyond it go dark,
	// the last step lit bright. Tap a step to set the length.
	if (omxFormGlobal.shortcutMode == FORMSHORTCUT_F3 && heldStepMask_ == 0)
	{
		for (uint8_t k = 3; k <= 10; k++)
			strip.setPixelColor(k, LEDOFF);
		// Top row 3-10 = rate options; the current rate is bright.
		int8_t rsel = omni->rateShortcutSel();
		for (uint8_t i = 0; i < 8; i++)
			strip.setPixelColor(3 + i, ((int8_t)i == rsel) ? (uint32_t)CYAN : (uint32_t)DKCYAN);
		uint8_t pageLen = omni->getPageLen(omni->activePage()); // this page's length (1-16)
		for (uint8_t i = 0; i < 16; i++)
		{
			uint32_t c = LEDOFF;
			if (i < pageLen)
				c = (i == pageLen - 1) ? (uint32_t)GREEN : (uint32_t)LOWWHITE;
			strip.setPixelColor(11 + i, c);
		}
		return;
	}

	// While step(s) are held on the overview page, the top row is the value palette.
	if (heldStepMask_ != 0 && stepMenuPage_ == 0)
	{
		for (uint8_t i = 11; i < 27; i++)
			strip.setPixelColor(i, (heldStepMask_ & (1 << (i - 11))) ? (blink ? WHITE : LOWWHITE) : LEDOFF);

		for (uint8_t k = 1; k <= 10; k++)
			strip.setPixelColor(k, LEDOFF);

		int8_t focus = heldStepKey_;
		if (stepEditMode_ == STEPMODE_NOTE)
		{
			// 10 note keys, each with a hue: chromatic = white notes periwinkle / black notes
			// amber; scale = root periwinkle / in-scale blue. Selected notes (in the chord or
			// held) show that same hue at full brightness; the rest are the dim version.
			bool chromatic = (scaleConfig.scalePattern < 0);
			for (uint8_t i = 0; i < 10; i++)
			{
				int8_t note = omxFormGlobal.musicScale->getNoteByDegree(i, midiSettings.octave);
				uint32_t full;
				if (chromatic)
				{
					uint8_t pc = ((note % 12) + 12) % 12;
					bool black = (pc == 1 || pc == 3 || pc == 6 || pc == 8 || pc == 10);
					full = black ? 0xff6a1a : 0xa8a8ff; // amber vs periwinkle
				}
				else
				{
					full = (i == 0) ? 0xa8a8ff : 0x3838ff; // root periwinkle vs in-scale blue
				}
				bool selected = (heldNoteKeys_ & (1 << i)) || (focus >= 0 && omni->stepHasNote(focus, note));
				strip.setPixelColor(1 + i, selected ? full : ((full >> 2) & 0x3f3f3f));
			}
		}
		else if (stepEditMode_ == STEPMODE_MATH)
		{
			uint8_t a = 0, b = 0, kind = focus >= 0 ? omni->stepMathInfo(focus, a, b) : 0;
			strip.setPixelColor(1, kind == 1 ? 0xff8000 : 0x4d2600); // Fill
			strip.setPixelColor(2, kind == 2 ? 0xff0080 : 0x4d0026); // !Fill
			for (uint8_t i = 0; i < 4; i++)
				strip.setPixelColor(3 + i, (kind == 3 && a == i + 1) ? 0x00ff00 : 0x264d00); // ratio A
			for (uint8_t i = 0; i < 4; i++)
				strip.setPixelColor(7 + i, (kind == 3 && b == i + 1) ? 0x00ffff : 0x004c4d); // ratio B
		}
		else if (stepEditMode_ == STEPMODE_MFX)
		{
			// Off on key 5, FX groups on 6-10 — same layout/colours as the AUX MIDI-FX view.
			int16_t sel = focus >= 0 ? omni->stepPaletteSelected(focus, STEPMODE_MFX) : -1; // 0=Off, 1-5=group
			strip.setPixelColor(5, sel == 0 ? colorConfig.selMidiFXGRPOffColor : colorConfig.midiFXGRPOffColor);
			for (uint8_t i = 0; i < NUM_MIDIFX_GROUPS; i++)
				strip.setPixelColor(6 + i, (sel == (int16_t)(i + 1)) ? colorConfig.selMidiFXGRPColor : colorConfig.midiFXGRPColor);
		}
		else if (stepEditMode_ != STEPMODE_NOTE)
		{
			uint8_t count = omni->stepPaletteCount(stepEditMode_);
			int16_t sel = focus >= 0 ? omni->stepPaletteSelected(focus, stepEditMode_) : -1;
			uint32_t col = kStepModeColors[stepEditMode_];
			uint32_t dim = (col >> 3) & 0x1f1f1f;
			bool isBar = (stepEditMode_ == STEPMODE_VEL || stepEditMode_ == STEPMODE_LENGTH || stepEditMode_ == STEPMODE_CHANCE);
			for (uint8_t i = 0; i < count; i++)
			{
				uint32_t c;
				if (isBar)
					c = ((int16_t)i == sel) ? col : ((int16_t)i < sel ? dim : (uint32_t)VLOWWHITE);
				else
					c = ((int16_t)i == sel) ? col : dim;
				strip.setPixelColor(1 + i, c);
			}
		}
		return;
	}

	// Top row 3-10 = the 8 edit modes; active bright, others dim.
	for (uint8_t m = 0; m < STEPMODE_COUNT; m++)
		strip.setPixelColor(3 + m, (m == stepEditMode_) ? kStepModeColors[m] : LOWWHITE);

	// Step row 11-26 = the current page's 16 steps.
	uint32_t hue = strip.gamma32(strip.ColorHSV((uint16_t)trackHue_[selectedMachine_] << 8, 255, 255));
	int16_t pageStart = (int16_t)omni->activePage() * 16;
	int16_t playhead = (int16_t)omni->playingStepIndex() - pageStart;

	for (uint8_t i = 0; i < 16; i++)
	{
		uint32_t col = LEDOFF;
		if (omni->stepIsOn(i))
			col = omni->getStepMute(i) ? DKRED : hue;
		if (omxFormGlobal.isPlaying && i == playhead)
			col = GREEN; // playhead: steady bright green over the step
		if (heldStepMask_ & (1 << i))
			col = blink ? WHITE : LOWWHITE; // held steps flash (e.g. while editing on a param page)
		strip.setPixelColor(11 + i, col);
	}
}

// Step-view encoder: navigate the menu cursor, or (while holding a step on a param page) edit
// the selected param on all held steps. Returns true if consumed.
bool OmxModeForm::onEncoderStep(Encoder::Update enc)
{
	if (formView_ != FORMVIEW_STEP)
		return false;
	int dir = enc.dir();
	if (dir == 0)
		return true;
	auto omni = static_cast<FormOmni::FormMachineOmni *>(getSelectedMachine());

	// Machine menu (page 3+): let the machine navigate/edit, except turning left off its first
	// page returns to the custom TRIG page.
	if (stepMenuPage_ == 3)
	{
		if (dir < 0 && omni->seqMenuAtStart())
		{
			stepMenuPage_ = 2;
			stepMenuSel_ = 3;
			omxDisp.setDirty();
			omxLeds.setDirty();
			return true;
		}
		return false; // forward to the machine
	}

	// Overview, track-select armed (no step held): the encoder changes the track.
	if (stepMenuPage_ == 0 && stepTrackSelEdit_ && heldStepMask_ == 0)
	{
		int8_t t = constrain((int)selectedMachine_ + dir, 0, kNumMachines - 1);
		if (t != (int8_t)selectedMachine_)
			selectMachine(t);
		omxDisp.setDirty();
		omxLeds.setDirty();
		return true;
	}

	// Holding a step on a custom param page: encoder edits the selected param (auto edit-mode).
	if (heldStepMask_ != 0 && stepMenuPage_ != 0)
	{
		uint8_t pid = (stepMenuPage_ - 1) * 4 + stepMenuSel_;
		int delta = enc.accel(1);
		if (delta == 0)
			delta = dir;
		for (uint8_t s = 0; s < 16; s++)
			if (heldStepMask_ & (1 << s))
				omni->editStepParam(s, pid, delta);
		stepEdited_ = true;
		if (heldStepKey_ >= 0 && omni->stepParamWide(pid))
			omxDisp.displayMessage(omni->stepParamValueString2(heldStepKey_, pid));
		omxDisp.setDirty();
		omxLeds.setDirty();
		return true;
	}

	// Custom param page, default-edit armed (no step held): encoder edits the track default.
	if (stepMenuPage_ != 0 && stepMenuPage_ != 3 && stepDefaultEdit_ && heldStepMask_ == 0)
	{
		uint8_t pid = (stepMenuPage_ - 1) * 4 + stepMenuSel_;
		int delta = enc.accel(1);
		if (delta == 0)
			delta = dir;
		omni->editParamDefault(pid, delta);
		omxDisp.setDirty();
		omxLeds.setDirty();
		return true;
	}

	// Navigate the custom cursor [overview=0, params 1..8], then hand off to the machine menu.
	int idx = (stepMenuPage_ == 0) ? 0 : (1 + (stepMenuPage_ - 1) * 4 + stepMenuSel_);
	idx += dir;
	if (idx > 8)
	{
		stepMenuPage_ = 3; // enter the machine menu
		omni->seqMenuEnter();
		omxDisp.displayMessage("Step Params");
		omxDisp.setDirty();
		omxLeds.setDirty();
		return true;
	}
	if (idx < 0)
		idx = 0;
	if (idx == 0)
	{
		stepMenuPage_ = 0;
		stepMenuSel_ = 0;
	}
	else
	{
		stepMenuPage_ = (idx - 1) / 4 + 1;
		stepMenuSel_ = (idx - 1) % 4;
	}
	omxDisp.setDirty();
	omxLeds.setDirty();
	return true;
}

// Step-view encoder press: while holding a step on a param page, clear that param's P-Lock;
// otherwise consumed (the Step view owns its own navigation). Returns true if consumed.
bool OmxModeForm::onEncoderButtonStep()
{
	if (formView_ != FORMVIEW_STEP)
		return false;
	if (stepMenuPage_ == 3)
		return false; // machine menu: let it toggle its own select/edit
	// Overview (no step held): arm/disarm changing the track with the encoder.
	if (stepMenuPage_ == 0 && heldStepMask_ == 0)
	{
		stepTrackSelEdit_ = !stepTrackSelEdit_;
		omxDisp.setDirty();
		return true;
	}
	if (heldStepMask_ != 0 && stepMenuPage_ != 0)
	{
		uint8_t pid = (stepMenuPage_ - 1) * 4 + stepMenuSel_;
		auto omni = static_cast<FormOmni::FormMachineOmni *>(getSelectedMachine());
		for (uint8_t s = 0; s < 16; s++)
			if (heldStepMask_ & (1 << s))
				omni->clearStepParamLock(s, pid);
		stepEdited_ = true;
		// No message — the header un-inverting communicates the cleared lock.
		omxDisp.setDirty();
		omxLeds.setDirty();
		return true;
	}
	// Custom param page, no step held: arm/disarm editing the track default.
	if (stepMenuPage_ != 0 && heldStepMask_ == 0)
	{
		stepDefaultEdit_ = !stepDefaultEdit_;
		omxDisp.setDirty();
		return true;
	}
	return true;
}

// Render a Step param page: the held step's values (with per-param lock indicators) or the
// built-in defaults when no step is held.
void OmxModeForm::onDisplayStepMenu()
{
	auto omni = static_cast<FormOmni::FormMachineOmni *>(getSelectedMachine());
	bool holding = (heldStepMask_ != 0 && heldStepKey_ >= 0);
	uint8_t base = (stepMenuPage_ - 1) * 4;

	const char *labels[4];
	String vals[4];
	const char *values[4];
	bool locked[4];
	for (uint8_t i = 0; i < 4; i++)
	{
		uint8_t pid = base + i;
		labels[i] = omni->stepParamLabel(pid);
		if (holding)
		{
			vals[i] = omni->stepParamBox(heldStepKey_, pid);
			locked[i] = omni->stepParamLocked(heldStepKey_, pid);
		}
		else
		{
			vals[i] = omni->paramDefaultBox(pid); // track defaults
			locked[i] = false;
		}
		values[i] = vals[i].c_str();
	}
	// Value box inverts while editing: holding a step, or the default-edit is armed.
	omxDisp.dispStepParams(labels, values, locked, stepMenuSel_, holding || stepDefaultEdit_);
}

void OmxModeForm::onDisplayStep()
{
	auto omni = static_cast<FormOmni::FormMachineOmni *>(getSelectedMachine());

	// F3 structure layer: rate on top, the active page's length bar on the bottom.
	if (omxFormGlobal.shortcutMode == FORMSHORTCUT_F3)
	{
		uint8_t activeCount = omni->getPageLen(omni->activePage());
		char rbuf[12];
		snprintf(rbuf, sizeof(rbuf), "1:%u", (unsigned)kSeqRates[omni->getSeq().rate]);
		omxDisp.dispTrackLength(rbuf, activeCount);
		return;
	}
	// Holding F1/F2 always shows the track page (with the copy/track overlay), from any menu
	// page — so the whole seq view is consistent while a modifier is held.
	if (omxFormGlobal.shortcutMode == FORMSHORTCUT_F1 || omxFormGlobal.shortcutMode == FORMSHORTCUT_F2)
	{
		onDisplaySeqTrackPage();
		return;
	}

	// Machine menu (page 3): the machine renders it natively (Notes/CC/Transpose/track params).
	if (stepMenuPage_ == 3)
	{
		omni->onDisplayUpdate();
		return;
	}

	// Custom param page (menu): show the held step's params (locks indicated) or the defaults.
	if (stepMenuPage_ != 0)
	{
		onDisplayStepMenu();
		return;
	}

	// Overview page, holding step(s): the value-palette popup — but only after a short delay (or
	// once edited), so a quick-click doesn't flash it.
	if (heldStepMask_ != 0 && (stepHoldUIShown_ || stepEdited_))
	{
		// Step states (0 empty · 1 notes · 2 ghost · 3 muted ghost · 4 muted notes) + the active
		// page's length: same data the track page uses, so the step row renders identically.
		uint8_t stepState[16];
		for (uint8_t i = 0; i < 16; i++)
		{
			bool m = omni->getStepMute(i);
			stepState[i] = omni->stepHasNotes(i) ? (m ? 4 : 1) : (omni->stepIsOn(i) ? (m ? 3 : 2) : 0);
		}
		uint8_t pageLen = omni->getPageLen(omni->activePage());

		// Note mode: compact piano keyboard for the held step's chord, with step markers below.
		if (stepEditMode_ == STEPMODE_NOTE && heldStepKey_ >= 0)
		{
			int8_t nts[6];
			omni->getStepNotes(heldStepKey_, nts);
			int8_t noteKeys[6];
			for (uint8_t i = 0; i < 6; i++)
				noteKeys[i] = (nts[i] >= 0 && nts[i] <= 127) ? omxUtil.noteNumberToKeyNumber(nts[i]) : -1;
			omxDisp.dispStepNoteKeyboard(noteKeys, stepState, pageLen, heldStepKey_);
			return;
		}

		// Other modes: value readout + step-position overview.
		String v = (heldStepKey_ >= 0) ? omni->stepValueString(heldStepKey_, stepEditMode_) : String("");
		omxDisp.dispStepOverview(v.c_str(), stepState, pageLen, heldStepKey_);
		return;
	}

	// Overview page, idle: the full track/page overview.
	onDisplaySeqTrackPage();
}

// Render the page-1 track overview, with an F1/F2 hold overlay when a modifier is held.
void OmxModeForm::onDisplaySeqTrackPage()
{
	auto omni = static_cast<FormOmni::FormMachineOmni *>(getSelectedMachine());
	int16_t pageStart = (int16_t)omni->activePage() * 16;
	int8_t playhead = omxFormGlobal.isPlaying ? (int8_t)((int16_t)omni->playingStepIndex() - pageStart) : -1;

	// Step states: 0 empty · 1 notes · 2 ghost · 3 muted ghost · 4 muted notes.
	uint8_t stepState[16];
	for (uint8_t i = 0; i < 16; i++)
	{
		bool m = omni->getStepMute(i);
		stepState[i] = omni->stepHasNotes(i) ? (m ? 4 : 1) : (omni->stepIsOn(i) ? (m ? 3 : 2) : 0);
	}

	// Track mute states, with solo override (any soloed -> non-soloed render muted).
	bool anySolo = false;
	for (uint8_t t = 0; t < kNumMachines; t++)
		if (machines_[t]->getSolo()) { anySolo = true; break; }
	bool trackMuted[8];
	for (uint8_t t = 0; t < 8; t++)
		trackMuted[t] = anySolo ? !machines_[t]->getSolo() : machines_[t]->getMute();

	bool mixMute = (formView_ == FORMVIEW_MIX && omxFormGlobal.shortcutMode == FORMSHORTCUT_F1);
	char title[16];
	if (mixMute)
		snprintf(title, sizeof(title), "MUTE");
	else
		snprintf(title, sizeof(title), "TRK %u", (unsigned)(selectedMachine_ + 1));
	char rateStr[10];
	snprintf(rateStr, sizeof(rateStr), "1:%u", (unsigned)kSeqRates[omni->getSeq().rate]);

	uint8_t modOverlay = 0;
	const char *overlayLabel = nullptr;
	if (mixMute)
	{
		// Mix F1: box/invert the "MUTE" name but keep the step row (a null label = no bottom box,
		// so the step glyphs stay visible to show mute state).
		modOverlay = 2;
		overlayLabel = nullptr;
	}
	else if (omxFormGlobal.shortcutMode == FORMSHORTCUT_F1)
	{
		modOverlay = 1;
		overlayLabel = "COPY";
	}
	else if (omxFormGlobal.shortcutMode == FORMSHORTCUT_F2)
	{
		modOverlay = 2;
		overlayLabel = (heldTrackKey_ >= 0) ? "MUTE / PLAY MODE" : "CUT / PASTE";
	}
	else if (formView_ == FORMVIEW_MIX && heldTrackKey_ >= 0)
	{
		// Mix hold-track: same box + label as Seq's F2 + Track.
		modOverlay = 2;
		overlayLabel = "MUTE / PLAY MODE";
	}

	const char *viewLabel = (formView_ == FORMVIEW_MIX) ? "MIX" : nullptr;
	uint8_t transport = omxFormGlobal.isPlaying ? 1 : 0; // record state not wired yet
	omxDisp.dispSeqTrackPage(title, trackMuted, selectedMachine_, rateStr,
							 mixPlayModeIndex(omni->trackPtr()), (uint16_t)clockConfig.clockbpm,
							 omni->getEnabledPages(), omni->activePage(), stepState, playhead,
							 modOverlay, overlayLabel, omni->getPageLen(omni->activePage()), transport, viewLabel);
}

// Mix view — track keys (3-10): F1+tap = mute, F2+tap = solo, double-click = open Step,
// single tap = select. (Low-row keys still go to the machine's step editor.)
void OmxModeForm::onKeyUpdateMix(OMXKeypadEvent e)
{
	uint8_t thisKey = e.key();
	if (thisKey < 3 || thisKey >= 11)
		return;
	uint8_t track = thisKey - 3;

	// Release: clear the held-track marker (used by K5 hue).
	if (!e.down())
	{
		if (heldTrackKey_ == track)
			heldTrackKey_ = -1;
		return;
	}

	if (omxFormGlobal.shortcutMode == FORMSHORTCUT_F1) // Mute
	{
		if (!e.held())
		{
			selectMachine(track);
			auto m = getSelectedMachine();
			m->setMute(!m->getMute());
			omxDisp.setDirty(); // the MUTE page's track squares show the state; no popup
		}
		return;
	}
	if (omxFormGlobal.shortcutMode == FORMSHORTCUT_F2) // Solo
	{
		if (!e.held())
		{
			selectMachine(track);
			auto m = getSelectedMachine();
			m->setSolo(!m->getSolo());
			omxDisp.displayMessage(m->getSolo() ? "SOLO" : "UNSOLO");
		}
		return;
	}

	// No modifier, key down: select + mark held (so K5 can set its hue, low row = controls).
	// No popup — the track page's boxed name + "MUTE / PLAY MODE" label shows the held state.
	if (!e.held())
	{
		selectMachine(track);
		heldTrackKey_ = track;
		omxDisp.setDirty();
	}
}

// Play-mode index 0-4 (fwd / rev / fwd-pong / rev-pong / random) from a track's fields.
static uint8_t mixPlayModeIndex(FormOmni::Track *t)
{
	if (t->playMode == FormOmni::TRACKMODE_RAND)
		return 4;
	if (t->playMode == FormOmni::TRACKMODE_PONG)
		return (t->playDirection == FormOmni::TRACKDIRECTION_REVERSE) ? 3 : 2;
	return (t->playDirection == FormOmni::TRACKDIRECTION_REVERSE) ? 1 : 0;
}

// Low-row per-track controls shown while a track is held in Mix:
// 11 Mute · 12 Solo · 14-18 play mode (fwd/rev/fwd-pong/rev-pong/random) · 25 copy · 26 paste.
void OmxModeForm::onKeyUpdateMixHold(OMXKeypadEvent e)
{
	if (e.held() || !e.down())
		return;
	uint8_t k = e.key();
	auto omni = static_cast<FormOmni::FormMachineOmni *>(machines_[heldTrackKey_]);
	auto trk = omni->trackPtr();

	if (k == 11)
	{
		omni->setMute(!omni->getMute());
		omxDisp.displayMessage(omni->getMute() ? "MUTE" : "UNMUTE");
	}
	else if (k == 12)
	{
		omni->setSolo(!omni->getSolo());
		omxDisp.displayMessage(omni->getSolo() ? "SOLO" : "UNSOLO");
	}
	else if (k >= 13 && k <= 17) // play modes, adjacent to mute/solo (no gap)
	{
		setTrackPlayModeIdx(trk, k - 13);
		omxDisp.displayMessage(kPlayModeNames[k - 13]);
	}
	else if (k >= 19 && k <= 26) // last 8 keys = track colour presets (8 evenly-spaced hues)
	{
		trackHue_[heldTrackKey_] = (uint8_t)((k - 19) * 32);
		omxDisp.displayMessage("Trk" + String(heldTrackKey_ + 1) + " Color");
	}
	omxLeds.setDirty();
}

// Low-row taps audition (preview) the selected track's programmed steps: note-on on key
// down, note-off on release. key16 = 0-15.
void OmxModeForm::onKeyUpdateMixStep(OMXKeypadEvent e)
{
	if (e.held())
		return;
	uint8_t key16 = e.key() - 11;
	auto omni = static_cast<FormOmni::FormMachineOmni *>(getSelectedMachine());
	omni->auditionStep(key16, e.down());
	omxLeds.setDirty();
}

// F1 + low-row taps toggle the selected track's step mutes. Under F2 the low row is a no-op
// (F2 = momentary FILL, a held global state — not a per-step control), and importantly no
// longer falls through to the machine's destructive cut-step.
void OmxModeForm::onKeyUpdateMixStepMute(OMXKeypadEvent e)
{
	if (e.held() || !e.down())
		return;
	if (omxFormGlobal.shortcutMode != FORMSHORTCUT_F1)
		return; // F2 = fill (handled by holding F2); low-row does nothing
	uint8_t key16 = e.key() - 11;
	auto omni = static_cast<FormOmni::FormMachineOmni *>(getSelectedMachine());
	omni->toggleStepMute(key16);
	omxDisp.setDirty(); // the MUTE page's step glyphs show the state; no popup
	omxLeds.setDirty();
}

void OmxModeForm::updateMixHoldLEDs()
{
	auto omni = static_cast<FormOmni::FormMachineOmni *>(machines_[heldTrackKey_]);
	auto trk = omni->trackPtr();

	for (uint8_t i = 11; i < 27; i++)
		strip.setPixelColor(i, LEDOFF);

	// Mute / Solo (warm) — contrasting with the play modes (cyan) beside them.
	strip.setPixelColor(11, omni->getMute() ? RED : DKRED);
	strip.setPixelColor(12, omni->getSolo() ? YELLOW : DKYELLOW);

	// Play modes 13-17: one cool colour, selected bright (the icons distinguish which is which).
	uint8_t pm = mixPlayModeIndex(trk);
	for (uint8_t m = 0; m < 5; m++)
		strip.setPixelColor(13 + m, (m == pm) ? (uint32_t)CYAN : (uint32_t)DKCYAN);

	// 19-26: 8 track-colour presets, each in its own hue (current one brightest).
	for (uint8_t i = 0; i < 8; i++)
	{
		bool cur = (trackHue_[heldTrackKey_] == i * 32);
		uint32_t c = strip.gamma32(strip.ColorHSV((uint16_t)(i * 32) << 8, 255, cur ? 255 : 60));
		strip.setPixelColor(19 + i, c);
	}
}

void OmxModeForm::updateShortcutMode()
{
	if (omxFormGlobal.auxBlock && midiSettings.keyState[0] == false)
	{
		omxFormGlobal.auxBlock = false;
		omxDisp.setDirty();
		omxLeds.setDirty();
	}

	// Step view: while a step is held the top row 1-10 is the value palette, so keys 1/2
	// must not become the F1/F2 shortcut. Freeze the shortcut mode at NONE.
	if (formView_ == FORMVIEW_STEP && heldStepMask_ != 0)
	{
		omxFormGlobal.shortcutMode = FORMSHORTCUT_NONE;
		return;
	}

	uint8_t prevMode = omxFormGlobal.shortcutMode;

	if (omxFormGlobal.shortcutMode != FORMSHORTCUT_AUX && midiSettings.keyState[1] && midiSettings.keyState[2])
	{
		omxFormGlobal.shortcutMode = FORMSHORTCUT_F3;
	}
	else if (omxFormGlobal.shortcutMode != FORMSHORTCUT_AUX && midiSettings.keyState[1])
	{
		omxFormGlobal.shortcutMode = FORMSHORTCUT_F1;
	}
	else if (omxFormGlobal.shortcutMode != FORMSHORTCUT_AUX && midiSettings.keyState[2])
	{
		omxFormGlobal.shortcutMode = FORMSHORTCUT_F2;
	}
	else if (midiSettings.keyState[0])
	{
		omxFormGlobal.shortcutMode = FORMSHORTCUT_AUX;
	}
	else
	{
		omxFormGlobal.shortcutMode = FORMSHORTCUT_NONE;
	}

	if (prevMode != omxFormGlobal.shortcutMode)
	{
		omxFormGlobal.shortcutPaste = false;

		// Mix: holding F2 activates FILL on all tracks (steps with a Fill condition play).
		bool fillOn = (formView_ == FORMVIEW_MIX && omxFormGlobal.shortcutMode == FORMSHORTCUT_F2);
		for (uint8_t i = 0; i < kNumMachines; i++)
			static_cast<FormOmni::FormMachineOmni *>(machines_[i])->setFill(fillOn);

		omxDisp.setDirty();
		omxLeds.setDirty();
	}
}

void OmxModeForm::InitSetup()
{
	initSetup = true;
}

void OmxModeForm::onModeActivated()
{
	// auto init when activated
	if (!initSetup)
	{
		InitSetup();
	}

	// sequencer.playing = false;
	stopSequencers();

	omxLeds.setDirty();
	omxDisp.setDirty();

	for (uint8_t i = 0; i < NUM_MIDIFX_GROUPS; i++)
	{
		subModeMidiFx[i].setEnabled(true);
		subModeMidiFx[i].onModeChanged();
		subModeMidiFx[i].setNoteOutputFunc(&OmxModeForm::onNotePostFXForwarder, this);
	}

	pendingNoteOffs.setNoteOffFunction(&OmxModeForm::onPendingNoteOffForwarder, this);

	params.setSelPageAndParam(0, 0);
	omxFormGlobal.encoderSelect = true;

	// Serial.println("AuxMacroActivated");
	auxMacroManager_.onModeActivated();
	// Serial.println("onModeActivated complete");


	// activeDrumKit.CopyFrom(drumKits[selDrumKit]);

	// selectMidiFx(mfxIndex_, false);
}

void OmxModeForm::onModeDeactivated()
{
	stopSequencers();

	for (uint8_t i = 0; i < NUM_MIDIFX_GROUPS; i++)
	{
		subModeMidiFx[i].setEnabled(false);
		subModeMidiFx[i].onModeChanged();
	}

	auxMacroManager_.onModeDectivated();
}

void OmxModeForm::stopSequencers()
{
	omxUtil.stopClocks();
	pendingNoteOffs.allOff();
}

void OmxModeForm::selectMidiFx(uint8_t mfxIndex, bool dispMsg)
{
	getSelectedMachine()->selectMidiFx(mfxIndex, dispMsg);

	for (uint8_t i = 0; i < NUM_MIDIFX_GROUPS; i++)
	{
		subModeMidiFx[i].setSelected(i == mfxIndex);
	}

	// uint8_t prevMidiFX = activeDrumKit.drumKeys[selDrumKey].midifx;

	// if(mfxIndex != prevMidiFX && prevMidiFX < NUM_MIDIFX_GROUPS)
	// {
	//     drumKeyUp(selDrumKey + 1);
	// }

	// activeDrumKit.drumKeys[selDrumKey].midifx = mfxIndex;

	// if(mfxQuickEdit_)
	// {
	// 	// Change the MidiFX Group being edited
	// 	if(mfxIndex < NUM_MIDIFX_GROUPS && mfxIndex != quickEditMfxIndex_)
	// 	{
	// 		enableSubmode(&subModeMidiFx[mfxIndex]);
	// 		subModeMidiFx[mfxIndex].enablePassthrough();
	// 		quickEditMfxIndex_ = mfxIndex;
	// 		dispMsg = false;
	// 	}
	// 	else if(mfxIndex >= NUM_MIDIFX_GROUPS)
	// 	{
	// 		disableSubmode();
	// 	}
	// }

	

	// if (dispMsg)
	// {
	// 	if (mfxIndex < NUM_MIDIFX_GROUPS)
	// 	{
	// 		omxDisp.displayMessageTimed("MidiFX " + String(mfxIndex + 1), 5);
	// 	}
	// 	else
	// 	{
	// 		omxDisp.displayMessageTimed("MidiFX Off", 5);
	// 	}
	// }
}

void OmxModeForm::onPotChanged(int potIndex, int prevValue, int newValue, int analogDelta)
{
	if (auxMacroManager_.onPotChanged(potIndex, prevValue, newValue, analogDelta))
		return;

	// Mix: hold a track + turn K5 to set that track's colour (hue).
	if (formView_ == FORMVIEW_MIX && heldTrackKey_ >= 0 && potIndex == 4)
	{
		trackHue_[heldTrackKey_] = (uint8_t)constrain(map(newValue, potMinVal, potMaxVal, 0, 255), 0, 255);
		omxDisp.displayMessage("Trk" + String(heldTrackKey_ + 1) + " Hue " + String(trackHue_[heldTrackKey_]));
		omxLeds.setDirty();
		return;
	}

	auto selMachine = getSelectedMachine();

	if(selMachine->doesConsumePots())
	{
		selMachine->onPotChanged(potIndex, prevValue, newValue, analogDelta);
		return;
	}

	omxUtil.sendPots(potIndex, sysSettings.midiChannel);
	omxDisp.setDirty();
}

void OmxModeForm::onClockTick()
{
	for(auto machine : machines_)
	{
		machine->onClockTick();
	}

	for (uint8_t i = 0; i < NUM_MIDIFX_GROUPS; i++)
	{
		// Lets them do things in background
		subModeMidiFx[i].onClockTick();
	}
}

void OmxModeForm::loopUpdate(Micros elapsedTime)
{
	// Serial.println("LoopUpdate");

	// Engage the hold-step UI once the hold passes a short delay (prevents quick-click flashes).
	if (heldStepMask_ != 0 && !stepHoldUIShown_ && (millis() - stepHoldStartMs_) >= 150)
	{
		stepHoldUIShown_ = true;
		omxDisp.setDirty();
	}

	for(auto machine : machines_)
	{
		machine->loopUpdate();
	}

	for (uint8_t i = 0; i < NUM_MIDIFX_GROUPS; i++)
	{
		// Lets them do things in background
		subModeMidiFx[i].loopUpdate();
	}

	// Can be modified by scale MidiFX
	omxFormGlobal.musicScale->calculateScaleIfModified(scaleConfig.scaleRoot, scaleConfig.scalePattern);

	if (omxFormGlobal.isPlaying)
	{
		// Refresh the instant the selected track's step advances, so the playhead (LED + the
		// under-step display marker) tracks each step exactly instead of jumping in coarse chunks.
		auto selOmni = static_cast<FormOmni::FormMachineOmni *>(getSelectedMachine());
		int16_t curStep = (int16_t)selOmni->playingStepIndex();
		if (curStep != lastPlayheadStep_)
		{
			lastPlayheadStep_ = curStep;
			omxLeds.setDirty();
			omxDisp.setDirty();
		}

		// Periodic refresh so blink animations (soloed-track / held-step flash) keep ticking
		// between step advances.
		uint32_t stepmicros = seqConfig.currentFrameMicros;
		if (stepmicros >= ledUpdateTime_)
		{
			ledUpdateTime_ = stepmicros + clockConfig.ppqInterval * 3;
			omxLeds.setDirty();
		}
	}
	else
	{
		lastPlayheadStep_ = -1;
	}

	// Serial.println("LoopUpdate complete");
}

bool OmxModeForm::getEncoderSelect()
{
	// return encoderSelect && !midiSettings.midiAUX && !isDrumKeyHeld();

	return omxFormGlobal.encoderSelect && !midiSettings.midiAUX;
}

void OmxModeForm::onEncoderChanged(Encoder::Update enc)
{
	if (auxMacroManager_.onEncoderChanged(enc))
		return;

	if (onEncoderStep(enc))
		return;

	auto selMachine = getSelectedMachine();
	selMachine->onEncoderChanged(enc);

	// if (getEncoderSelect())
	// {
	// 	// onEncoderChangedSelectParam(enc);
	// 	params.changeParam(enc.dir());
	// 	omxDisp.setDirty();
	// 	return;
	// }

	// auto amt = enc.accel(5); // where 5 is the acceleration factor if you want it, 0 if you don't)

	// int8_t selPage = params.getSelPage();
	// int8_t selParam = params.getSelParam() + 1; // Add one for readability

	// if (selPage == FORMPAGE_POTSANDMACROS)
	// {
	// 	if (selParam == 1)
	// 	{
	// 		potSettings.potbank = constrain(potSettings.potbank + amt, 0, NUM_CC_BANKS - 1);
	// 	}
	// 	if (selParam == 2)
	// 	{
	// 		midiSettings.midiSoftThru = constrain(midiSettings.midiSoftThru + amt, 0, 1);
	// 	}
	// 	if (selParam == 3)
	// 	{
	// 		midiMacroConfig.midiMacro = constrain(midiMacroConfig.midiMacro + amt, 0, nummacromodes);
	// 	}
	// 	if (selParam == 4)
	// 	{
	// 		midiMacroConfig.midiMacroChan = constrain(midiMacroConfig.midiMacroChan + amt, 1, 16);
	// 	}
	// }
	// else if (selPage == FORMPAGE_SCALES)
	// {
	// 	if (selParam == 1)
	// 	{
	// 		int prevRoot = scaleConfig.scaleRoot;
	// 		scaleConfig.scaleRoot = constrain(scaleConfig.scaleRoot + amt, 0, 12 - 1);
	// 		if (prevRoot != scaleConfig.scaleRoot)
	// 		{
	// 			omxFormGlobal.musicScale->calculateScale(scaleConfig.scaleRoot, scaleConfig.scalePattern);
	// 		}
	// 	}
	// 	if (selParam == 2)
	// 	{
	// 		int prevPat = scaleConfig.scalePattern;
	// 		scaleConfig.scalePattern = constrain(scaleConfig.scalePattern + amt, -1, omxFormGlobal.musicScale->getNumScales() - 1);
	// 		if (prevPat != scaleConfig.scalePattern)
	// 		{
	// 			omxDisp.displayMessage(omxFormGlobal.musicScale->getScaleName(scaleConfig.scalePattern));
	// 			omxFormGlobal.musicScale->calculateScale(scaleConfig.scaleRoot, scaleConfig.scalePattern);
	// 		}
	// 	}
	// 	if (selParam == 3)
	// 	{
	// 		scaleConfig.lockScale = constrain(scaleConfig.lockScale + amt, 0, 1);
	// 	}
	// 	if (selParam == 4)
	// 	{
	// 		scaleConfig.group16 = constrain(scaleConfig.group16 + amt, 0, 1);
	// 	}
	// }
	// else if (selPage == FORMPAGE_CFG)
	// {
	// 	if (selParam == 3)
	// 	{
	// 		clockConfig.globalQuantizeStepIndex = constrain(clockConfig.globalQuantizeStepIndex + amt, 0, kNumArpRates - 1);
	// 	}
	// 	else if (selParam == 4)
	// 	{
	// 		cvNoteUtil.triggerMode = constrain(cvNoteUtil.triggerMode + amt, 0, 1);
	// 	}
	// }

	// omxDisp.setDirty();
}

void OmxModeForm::onEncoderButtonDown()
{
	if (auxMacroManager_.onEncoderButtonDown())
		return;

	if (onEncoderButtonStep())
		return;

	auto selMachine = getSelectedMachine();
	selMachine->onEncoderButtonDown();

	// if (params.isPageAndParam(FORMPAGE_CFG, 0))
	// {
	// 	auxMacroManager_.enableSubmode(&omxUtil.subModePotConfig);
	// 	omxDisp.isDirty();
	// 	return;
	// }

	omxFormGlobal.encoderSelect = !omxFormGlobal.encoderSelect;
	omxDisp.isDirty();
}

void OmxModeForm::onEncoderButtonUp()
{
}

void OmxModeForm::onEncoderButtonDownLong()
{
}

bool OmxModeForm::shouldBlockEncEdit()
{
	if (auxMacroManager_.shouldBlockEncEdit())
		return true;

	return false;
}

void OmxModeForm::saveKit(uint8_t saveIndex)
{
	// drumKits[saveIndex].CopyFrom(activeDrumKit);
	// selDrumKit = saveIndex;
}

void OmxModeForm::loadKit(uint8_t loadIndex)
{
	// activeDrumKit.CopyFrom(drumKits[loadIndex]);
	// selDrumKit = loadIndex;
}

void OmxModeForm::onKeyUpdate(OMXKeypadEvent e)
{
	omxDisp.setDirty();
	omxLeds.setDirty();

	auto selMachine = machines_[selectedMachine_];

	updateShortcutMode();

	if (auxMacroManager_.onKeyUpdate(e))
		return; // Key consumed by macro

	// Enables quick key aux exit
	if(e.quickClicked() && selMachine->onKeyQuickClicked(e))
		return;

	if (omxFormGlobal.auxBlock)
		return;

	if (onKeyUpdateSelMidiFX(e))
		return;

	// if (omxFormGlobal.shortcutMode == FORMSHORTCUT_NONE || omxFormGlobal.shortcutMode == FORMSHORTCUT_AUX)
	// {
	// 	if (auxMacroManager_.onKeyUpdate(e))
	// 		return; // Key consumed by macro

	// 	if (onKeyUpdateSelMidiFX(e))
	// 		return;
	// }


	int thisKey = e.key();
	// AUX KEY

	// Don't go into aux mode if shortcuts F1 or F2 are being used
	if (omxFormGlobal.shortcutMode == FORMSHORTCUT_NONE || omxFormGlobal.shortcutMode == FORMSHORTCUT_AUX)
	{
		if (thisKey == 0)
		{
			// Step view: AUX while holding step(s) resets their value (no view browsing).
			if (formView_ == FORMVIEW_STEP && heldStepMask_ != 0)
			{
				if (e.down() && !e.held())
				{
					auto omni = static_cast<FormOmni::FormMachineOmni *>(getSelectedMachine());
					bool note = (stepEditMode_ == STEPMODE_NOTE);
					for (uint8_t s = 0; s < 16; s++)
						if (heldStepMask_ & (1 << s))
						{
							if (note)
								omni->stepNotesToGhost(s); // clear notes, keep as ghost
							else
								omni->resetStepValue(s, stepEditMode_);
						}
					stepEdited_ = true;
					omxDisp.setDirty(); // hold UI shows the reset value — no popup
					omxLeds.setDirty();
				}
				return;
			}
			bool wasAux = midiSettings.midiAUX;
			midiSettings.midiAUX = e.down();
			if (e.down() && !wasAux)
			{
				pendingView_ = formView_; // start browsing from the current view
			}
			// v2 shell: on AUX release, commit the view you browsed to while holding AUX.
			else if (wasAux && !e.down() && pendingView_ != formView_)
			{
				setFormView(pendingView_);
			}
			return;
		}
	}

	bool keyConsumed = false;

	// Aux shortcuts for all modes
	if(omxFormGlobal.shortcutMode == FORMSHORTCUT_AUX)
	{
		if (!e.held() && e.down())
		{
			if (thisKey == 11 || thisKey == 12) // Change Octave
			{
				int amt = thisKey == 11 ? -1 : 1;
				midiSettings.octave = constrain(midiSettings.octave + amt, -5, 4);
				omxDisp.displayMessage("Octave: " + String(midiSettings.octave));
				keyConsumed = true;
			}
			else if (auxMacroManager_.isMFXQuickEditEnabled() == false && (thisKey == 1 || thisKey == 2)) // Change Param selection
			{
				if (thisKey == 1)
				{
					togglePlayback();
					// params.decrementParam();
					omxDisp.displayMessage(omxFormGlobal.isPlaying ? "PLAY" : "PAUSE");
				}
				else if (thisKey == 2)
				{
					resetPlayback();
					omxDisp.displayMessage("RESET");
				}
				keyConsumed = true;
			}
			else if (thisKey >= 13 && thisKey <= 18) // v2 shell: preview view (commit on AUX release)
			{
				static const char *kViewNames[FORMVIEW_COUNT] = {"MIX", "STEP", "TRANSPOSE", "NOTES", "PATTERNS", "MI"};
				pendingView_ = thisKey - 13;
				omxDisp.displayMessage(kViewNames[pendingView_]);
				omxLeds.setDirty();
				keyConsumed = true;
			}
		}
		else if(e.held())
		{
			if (auxMacroManager_.isMFXQuickEditEnabled() == false && thisKey == 1 ) // Change Param selection
			{
				// Shortcut to do a reset after pausing
				if(omxFormGlobal.isPlaying == false)
				{
					resetPlayback();
					omxDisp.displayMessage("STOP");
					keyConsumed = true;
				}
			}
		}
	}


	// v2 shell: container-rendered views take their own keys (not the machine).
	if (formView_ == FORMVIEW_STEP)
	{
		if (keyConsumed)
			return;
		// Machine menu (page 3): F1/F2/F3 still copy/paste/length; a plain step tap selects
		// which step the notes/CC pages edit.
		if (stepMenuPage_ == 3)
		{
			if (omxFormGlobal.shortcutMode != FORMSHORTCUT_NONE)
			{
				onKeyUpdateStep(e);
				return;
			}
			if (e.down() && !e.held() && thisKey >= 11 && thisKey < 27)
			{
				static_cast<FormOmni::FormMachineOmni *>(getSelectedMachine())->setSelStepByKey(thisKey - 11);
				omxLeds.setDirty();
				omxDisp.setDirty();
			}
			return;
		}
		onKeyUpdateStep(e);
		return;
	}
	if (formView_ == FORMVIEW_PATTERNS)
	{
		if (!keyConsumed)
			onKeyUpdatePatterns(e);
		return;
	}
	if (formView_ == FORMVIEW_MI)
	{
		return; // stub: swallow keys
	}
	// Mix view routing.
	if (formView_ == FORMVIEW_MIX && !keyConsumed)
	{
		// Track keys 3-10 (except under F3, which the machine handles as rate).
		if (thisKey >= 3 && thisKey < 11 && omxFormGlobal.shortcutMode != FORMSHORTCUT_F3)
		{
			onKeyUpdateMix(e);
			return;
		}
		// Low-row per-track controls while a track is held.
		if (heldTrackKey_ >= 0 && thisKey >= 11 && thisKey < 27)
		{
			onKeyUpdateMixHold(e);
			return;
		}
		// Low-row taps (no track held, no modifier) audition the selected track's steps.
		if (heldTrackKey_ < 0 && thisKey >= 11 && thisKey < 27 && omxFormGlobal.shortcutMode == FORMSHORTCUT_NONE)
		{
			onKeyUpdateMixStep(e);
			return;
		}
		// F1/F2 + low row = step mute/solo on the selected track (not the machine's copy/cut).
		if (thisKey >= 11 && thisKey < 27 &&
			(omxFormGlobal.shortcutMode == FORMSHORTCUT_F1 || omxFormGlobal.shortcutMode == FORMSHORTCUT_F2))
		{
			onKeyUpdateMixStepMute(e);
			return;
		}
		// Otherwise (F3 + track) falls through to the machine.
	}

	if(selMachine->doesConsumeKeys())
	{
		// if(omxFormGlobal.shortcutMode != FORMSHORTCUT_AUX)
		// {
		// 	selMachine->onKeyUpdate(e);
		// }

		if(!keyConsumed)
		{
			keyConsumed = selMachine->onKeyUpdate(e);
		}

		return;
	}

	if(keyConsumed)
	return;

	switch (omxFormGlobal.formMode)
	{
	case FORMMODE_BASE:
	{
		switch (omxFormGlobal.shortcutMode)
		{
		case FORMSHORTCUT_AUX:
		break;
		case FORMSHORTCUT_F1: // Copy
		{
			if (!e.held() && e.down())
			{
				if(thisKey == 0)
				{
					omxDisp.displayMessage("AUX F1");
				}
				else if (thisKey >= 3 && thisKey < 11)
				{
					if (omxFormGlobal.shortcutPaste == false)
					{
						copyMachineAt(thisKey - 3);
						omxFormGlobal.shortcutPaste = true;
						keyConsumed = true;
					}
					else
					{
						pasteMachineTo(thisKey - 3);
						keyConsumed = true;
					}
				}
			}
		}
		break;
		case FORMSHORTCUT_F2: // Cut
		{
			if (!e.held() && e.down())
			{
				if (thisKey == 0)
				{
					omxDisp.displayMessage("AUX F2");
				}
				else if (thisKey >= 3 && thisKey < 11)
				{
					if (omxFormGlobal.shortcutPaste == false)
					{
						cutMachineAt(thisKey - 3);
						omxFormGlobal.shortcutPaste = true;
						keyConsumed = true;
					}
					else
					{
						pasteMachineTo(thisKey - 3);
						keyConsumed = true;
					}
				}
			}
		}
		break;
		case FORMSHORTCUT_F3: // Sequencer shortcut
		{
			if (thisKey == 0)
			{
				omxDisp.displayMessage("AUX F3");
			}
			// if (!e.held() && e.down())
			// {
			// 	if (thisKey >= 3 && thisKey < 11)
			// 	{
			// 		cutMachineAt(thisKey - 3);
			// 		keyConsumed = true;
			// 	}
			// }
		}
		break;
		default:
		{
			// Toggle Mute
			if(!e.down() && e.clicks() == 2)
			{
				if (thisKey >= 3 && thisKey < 11)
				{
					selectMachine(thisKey - 3);
					keyConsumed = true;
					auto m = getSelectedMachine();
					m->setMute(!m->getMute());

					omxDisp.displayMessage(m->getMute() ? "MUTE" : "UNMUTE");
					// selectMachineMode_ = true;
				}
			}
			else if (!e.held() && e.down())
			{
				if (thisKey >= 3 && thisKey < 11)
				{
					selectMachine(thisKey - 3);
					keyConsumed = true;
					// selectMachineMode_ = true;
				}
			}
			// Key released
			else if (!e.held() && !e.down())
			{
			}

		}
		break;
		}

		if(!keyConsumed)
		{
			selMachine->onKeyUpdate(e);
		}
	}
	break;
	}
}

bool OmxModeForm::onKeyUpdateSelMidiFX(OMXKeypadEvent e)
{
	if (auxMacroManager_.onKeyUpdateAuxMFXShortcuts(e, omxFormGlobal.selMidiFX))
		return true;

	return false;
}

bool OmxModeForm::onKeyHeldSelMidiFX(OMXKeypadEvent e)
{
	if (auxMacroManager_.onKeyHeldAuxMFXShortcuts(e, omxFormGlobal.selMidiFX))
		return true;

	return false;
}

void OmxModeForm::onKeyHeldUpdate(OMXKeypadEvent e)
{
	if (auxMacroManager_.onKeyHeldUpdate(e))
		return;

	if (onKeyHeldSelMidiFX(e))
		return;

	auto selMachine = getSelectedMachine();

	if(selMachine->doesConsumeKeys())
	{
		selMachine->onKeyHeldUpdate(e);
		return;
	}

	int thisKey = e.key();

	// v2 single-engine: hold-a-track no longer opens the machine-type picker
	// (every track is the OMNI engine). Held keys go straight to the machine.
	selMachine->onKeyHeldUpdate(e);
}

void OmxModeForm::updateLEDs()
{
	omxLeds.setAllLEDS(0, 0, 0);

	if (midiSettings.midiAUX)
	{
		uint8_t selMFXIndex = getSelectedMachine()->getSelectedMidiFX();
		auxMacroManager_.UpdateAUXLEDS(selMFXIndex);
		updateAuxViewLEDs(); // v2 shell: overlay the view selector on keys 13-18
		return;
	}

	// v2 shell: container-rendered views
	if (formView_ == FORMVIEW_STEP)
	{
		if (stepMenuPage_ == 3)
		{
			getSelectedMachine()->updateLEDs(); // machine menu uses the machine's own LEDs
			return;
		}
		updateStepLEDs();
		return;
	}
	if (formView_ == FORMVIEW_PATTERNS)
	{
		updatePatternsLEDs();
		return;
	}
	if (formView_ == FORMVIEW_MI)
	{
		return; // stub: LEDs cleared
	}

	auto selMachine = getSelectedMachine();

	if(selMachine->doesConsumeLEDs())
	{
		selMachine->updateLEDs();
		return;
	}

	bool blinkState = omxLeds.getBlinkState();
	// bool slowBlink = omxLeds.getSlowBlinkState();

	// F3 machine might use these keys for shortcuts
	if (omxFormGlobal.shortcutMode != FORMSHORTCUT_F3)
	{
		for (uint8_t i = 0; i < kNumMachines; i++)
		{
			bool isMuted = machines_[i]->getMute();
			// Mix view: per-track colour from its hue. Other views keep the machine colour.
			uint32_t trackColor = (formView_ == FORMVIEW_MIX)
									   ? strip.gamma32(strip.ColorHSV((uint16_t)trackHue_[i] << 8, 255, 255))
									   : (uint32_t)getMachineColor(i);
			uint32_t color = isMuted ? (uint32_t)RED : trackColor;

			if(i == selectedMachine_)
			{
				color = isMuted ? SALMON : WHITE;
			}

			if(machines_[i]->didTriggerThisStep())
			{
				color = INDIGO;
			}

			// Mix view: soloed tracks flash.
			if (formView_ == FORMVIEW_MIX && machines_[i]->getSolo() && !blinkState)
			{
				color = LEDOFF;
			}

			strip.setPixelColor(i + 3, color);
		}
	}

	selMachine->updateLEDs();

	// Mix: while a track is held, the low row shows its per-track controls (over the machine).
	if (formView_ == FORMVIEW_MIX && heldTrackKey_ >= 0)
	{
		updateMixHoldLEDs();
	}
}

void OmxModeForm::onDisplayUpdate()
{
	updateShortcutMode();

	if (auxMacroManager_.updateLEDs() == false && omxLeds.isDirty())
	{
		// Macro or submode is off, update our LEDs
		updateLEDs();
	}

	// If true, macro or submode is on and consuming display
	if (auxMacroManager_.onDisplayUpdate())
		return;

	// If this is true we are in mode selection menu
	if (encoderConfig.enc_edit)
		return;

	if (omxDisp.canShowDisplay() == false)
		return;

	// v2 shell: container-rendered views
	if (formView_ == FORMVIEW_STEP)
	{
		onDisplayStep();
		return;
	}
	if (formView_ == FORMVIEW_PATTERNS)
	{
		onDisplayPatterns();
		return;
	}
	if (formView_ == FORMVIEW_MI)
	{
		onDisplayMI();
		return;
	}

	auto selMachine = getSelectedMachine();

	if(selMachine->doesConsumeDisplay())
	{
		selMachine->onDisplayUpdate();
		return;
	}

	// Mix view: held F3 (LEN | RATE) shows the selected track's rate on top and its length as
	// a 16-cell bar on the bottom (full boxes for steps within length, dashes past it).
	if (formView_ == FORMVIEW_MIX && omxFormGlobal.shortcutMode == FORMSHORTCUT_F3)
	{
		auto omni = static_cast<FormOmni::FormMachineOmni *>(selMachine);
		uint16_t pageStart = (uint16_t)omni->activePage() * 16;
		uint16_t trackLen = omni->trackPtr()->getLength(); // 1-64
		uint16_t rem = trackLen <= pageStart ? 0 : (trackLen - pageStart);
		uint8_t activeCount = rem > 16 ? 16 : (uint8_t)rem;
		char rbuf[12];
		snprintf(rbuf, sizeof(rbuf), "1:%u", (unsigned)kSeqRates[omni->getSeq().rate]);
		omxDisp.dispTrackLength(rbuf, activeCount);
		return;
	}

	// Mix view: held F1 shows the page-1 track overview (track squares + step glyphs already
	// carry mute state); the name reads "MUTE" instead of "TRK n".
	if (formView_ == FORMVIEW_MIX && omxFormGlobal.shortcutMode == FORMSHORTCUT_F1)
	{
		onDisplaySeqTrackPage();
		return;
	}

	// Mix view: held F2 shows the split key-function view: top = track solos, bottom = FILL.
	if (formView_ == FORMVIEW_MIX && omxFormGlobal.shortcutMode == FORMSHORTCUT_F2)
	{
		bool topFill[kNumMachines];
		for (uint8_t t = 0; t < kNumMachines; t++)
			topFill[t] = machines_[t]->getSolo();
		omxDisp.dispKeyFunctionSplit("SOLO", topFill, kNumMachines, "Fill", nullptr, 0);
		return;
	}

	bool dispLabel = false;
	bool dispParams = true;

	switch (omxFormGlobal.shortcutMode)
	{
	// case FORMSHORTCUT_AUX:
	// 	// tempString = "Aux";
	// 	break;
	case FORMSHORTCUT_F1:
		tempString = omxFormGlobal.shortcutPaste ? "Paste" : "Copy";
		dispLabel = true;
		break;
	case FORMSHORTCUT_F2:
		tempString = omxFormGlobal.shortcutPaste ? "Paste" : "Cut";
		dispLabel = true;
		break;
	case FORMSHORTCUT_F3:
		tempString = selMachine->getF3shortcutName();
		dispLabel = true;
		break;
	default:
	{
		// Mix's first page shows the same track/page overview as the Seq view.
		if (formView_ == FORMVIEW_MIX)
		{
			onDisplaySeqTrackPage();
			return;
		}
		selMachine->onDisplayUpdate();
		return;
	}
		break;
	}

	if(dispLabel){
		omxDisp.dispGenericModeLabel(tempString.c_str(), 0, 0);
	}
	else if(dispParams)
	{
		// omxDisp.clearLegends();

		// switch (params.getSelPage())
		// {
		// case FORMPAGE_INSPECT:
		// {
		// 	omxDisp.setLegend(0, "P CC", potSettings.potCC);
		// 	omxDisp.setLegend(1, "P VAL", potSettings.potCC);
		// 	omxDisp.setLegend(2, "NOTE", potSettings.potCC);
		// 	omxDisp.setLegend(3, "VEL", potSettings.potCC);
		// }
		// break;

		// default:
		// 	break;
		// }

		// omxDisp.dispGenericMode2(params.getNumPages(), params.getSelPage(), params.getSelParam(), getEncoderSelect());
		// // omxDisp.dispGenericModeLabelDoubleLine

	}
	// if (params.getSelPage() == FORMPAGE_INSPECT)
	// {
	// 	omxDisp.clearLegends();

	// 	omxDisp.legends[0] = "P CC";
	// 	omxDisp.legends[1] = "P VAL";
	// 	omxDisp.legends[2] = "NOTE";
	// 	omxDisp.legends[3] = "VEL";
	// 	omxDisp.legendVals[0] = potSettings.potCC;
	// 	omxDisp.legendVals[1] = potSettings.potVal;
	// 	omxDisp.legendVals[2] = midiSettings.midiLastNote;
	// 	omxDisp.legendVals[3] = midiSettings.midiLastVel;
	// }
	// else if (params.getSelPage() == FORMPAGE_POTSANDMACROS) // SUBMODE_MIDI3
	// {
	// 	omxDisp.clearLegends();

	// 	omxDisp.legends[0] = "PBNK"; // Potentiometer Banks
	// 	omxDisp.legends[1] = "THRU"; // MIDI thru (usb to hardware)
	// 	omxDisp.legends[2] = "MCRO"; // Macro mode
	// 	omxDisp.legends[3] = "M-CH";
	// 	omxDisp.legendVals[0] = potSettings.potbank + 1;
	// 	omxDisp.legendText[1] = midiSettings.midiSoftThru ? "On" : "Off";
	// 	omxDisp.legendText[2] = macromodes[midiMacroConfig.midiMacro];
	// 	omxDisp.legendVals[3] = midiMacroConfig.midiMacroChan;
	// }
	// else if (params.getSelPage() == FORMPAGE_SCALES) // SCALES
	// {
	// 	omxDisp.clearLegends();
	// 	omxDisp.legends[0] = "ROOT";
	// 	omxDisp.legends[1] = "SCALE";
	// 	omxDisp.legends[2] = "LOCK";
	// 	omxDisp.legends[3] = "GROUP";
	// 	omxDisp.legendVals[0] = -127;
	// 	if (scaleConfig.scalePattern < 0)
	// 	{
	// 		omxDisp.legendVals[1] = -127;
	// 		omxDisp.legendText[1] = "Off";
	// 	}
	// 	else
	// 	{
	// 		omxDisp.legendVals[1] = scaleConfig.scalePattern;
	// 	}

	// 	omxDisp.legendVals[2] = -127;
	// 	omxDisp.legendVals[3] = -127;

	// 	omxDisp.legendText[0] = musicScale->getNoteName(scaleConfig.scaleRoot);
	// 	omxDisp.legendText[2] = scaleConfig.lockScale ? "On" : "Off";
	// 	omxDisp.legendText[3] = scaleConfig.group16 ? "On" : "Off";
	// }
	// else if (params.getSelPage() == FORMPAGE_CFG) // CONFIG
	// {
	// 	omxDisp.clearLegends();
	// 	omxDisp.setLegend(0, "P CC", "CFG");
	// 	omxDisp.setLegend(1, "CLR", "STOR");
	// 	omxDisp.setLegend(2, "QUANT", "1/" + String(kArpRates[clockConfig.globalQuantizeStepIndex]));
	// 	omxDisp.setLegend(3, "CV M", cvNoteUtil.getTriggerModeDispName());
	// }

	// omxDisp.dispGenericMode2(params.getNumPages(), params.getSelPage(), params.getSelParam(), getEncoderSelect());
}

// void onDisplayUpdateLoadKit()
// {

// }

// incoming midi note on
void OmxModeForm::inMidiNoteOn(byte channel, byte note, byte velocity)
{
	// midiSettings.midiLastNote = note;
	// midiSettings.midiLastVel = velocity;
	// int whatoct = (note / 12);
	// int thisKey;
	// uint32_t keyColor = MIDINOTEON;

	// if ((whatoct % 2) == 0)
	// {
	// 	thisKey = note - (12 * whatoct);
	// }
	// else
	// {
	// 	thisKey = note - (12 * whatoct) + 12;
	// }
	// if (whatoct == 0)
	// { // ORANGE,YELLOW,GREEN,MAGENTA,CYAN,BLUE,LIME,LTPURPLE
	// }
	// else if (whatoct == 1)
	// {
	// 	keyColor = ORANGE;
	// }
	// else if (whatoct == 2)
	// {
	// 	keyColor = YELLOW;
	// }
	// else if (whatoct == 3)
	// {
	// 	keyColor = GREEN;
	// }
	// else if (whatoct == 4)
	// {
	// 	keyColor = MAGENTA;
	// }
	// else if (whatoct == 5)
	// {
	// 	keyColor = CYAN;
	// }
	// else if (whatoct == 6)
	// {
	// 	keyColor = LIME;
	// }
	// else if (whatoct == 7)
	// {
	// 	keyColor = CYAN;
	// }
	// strip.setPixelColor(midiKeyMap[thisKey], keyColor); //  Set pixel's color (in RAM)
	// 													//	dirtyPixels = true;
	// strip.show();
	// omxDisp.setDirty();
}

void OmxModeForm::inMidiNoteOff(byte channel, byte note, byte velocity)
{
	// int whatoct = (note / 12);
	// int thisKey;
	// if ((whatoct % 2) == 0)
	// {
	// 	thisKey = note - (12 * whatoct);
	// }
	// else
	// {
	// 	thisKey = note - (12 * whatoct) + 12;
	// }
	// strip.setPixelColor(midiKeyMap[thisKey], LEDOFF); //  Set pixel's color (in RAM)
	// 												  //	dirtyPixels = true;
	// strip.show();
	// omxDisp.setDirty();
}

void OmxModeForm::inMidiControlChange(byte channel, byte control, byte value)
{
	if (auxMacroManager_.inMidiControlChange(channel, control, value))
		return;
}

void OmxModeForm::SetScale(MusicScales *scale)
{
	omxFormGlobal.musicScale = scale;
	auxMacroManager_.SetScale(scale);
}

// void OmxModeForm::drumKeyDown(uint8_t keyIndex)
// {
//     auto drumKey = activeDrumKit.drumKeys[keyIndex - 1];

//     MidiNoteGroup noteGroup = omxUtil.midiDrumNoteOn(keyIndex, drumKey.noteNum, drumKey.vel, drumKey.chan);

// 	if (noteGroup.noteNumber == 255)
// 		return;

//     selDrumKey = keyIndex - 1;

// 	noteGroup.unknownLength = true;
// 	noteGroup.prevNoteNumber = noteGroup.noteNumber;

// 	if (drumKey.midifx < NUM_MIDIFX_GROUPS)
// 	{
// 		subModeMidiFx[drumKey.midifx].noteInput(noteGroup);
// 	}
// 	else
// 	{
// 		onNotePostFX(noteGroup);
// 	}
// }

// void OmxModeForm::drumKeyUp(uint8_t keyIndex)
// {
//     MidiNoteGroup noteGroup = omxUtil.midiDrumNoteOff(keyIndex);

// 	if (noteGroup.noteNumber == 255)
// 		return;

//     auto drumKey = activeDrumKit.drumKeys[keyIndex - 1];

// 	noteGroup.unknownLength = true;
// 	noteGroup.prevNoteNumber = noteGroup.noteNumber;

// 	if (drumKey.midifx < NUM_MIDIFX_GROUPS)
// 	{
// 		subModeMidiFx[drumKey.midifx].noteInput(noteGroup);
// 	}
// 	else
// 	{
// 		onNotePostFX(noteGroup);
// 	}
// }

void OmxModeForm::seqNoteOn(MidiNoteGroup noteGroup, uint8_t midifx)
{
	// Serial.println("seqNoteOn: " + String(noteGroup.noteNumber) + " " + String(midifx));
	// onNotePostFX(noteGroup);


	// MidiNoteGroup noteGroup = omxUtil.midiNoteOn2(musicScale, keyIndex, midiSettings.defaultVelocity, sysSettings.midiChannel);

	if (noteGroup.noteNumber == 255)
		return;

	// Serial.println("doNoteOn: " + String(noteGroup.noteNumber));

	// noteGroup.unknownLength = true;
	noteGroup.prevNoteNumber = noteGroup.noteNumber;

	if (midifx < NUM_MIDIFX_GROUPS)
	{
		// Serial.println("seqNoteOn Send to midifx");
		// noteGroup.Print();
		subModeMidiFx[midifx].noteInput(noteGroup);
		// subModeMidiFx.noteInput(noteGroup);
	}
	else
	{
		// Serial.println("Send to post");
		onNotePostFX(noteGroup);
	}
}

// Called via doNoteOnForwarder
void OmxModeForm::seqNoteOff(MidiNoteGroup noteGroup, uint8_t midifx)
{
	// Serial.println("seqNoteOff: " + String(noteGroup.noteNumber) + " " + String(midifx));

	// Serial.println("seqNoteOff: " + String(noteGroup.noteNumber));

	// onNotePostFX(noteGroup);

	// MidiNoteGroup noteGroup = omxUtil.midiNoteOff2(keyIndex, sysSettings.midiChannel);

	if (noteGroup.noteNumber == 255)
		return;

	// Serial.println("doNoteOff: " + String(noteGroup.noteNumber));

	// noteGroup.unknownLength = true;
	noteGroup.prevNoteNumber = noteGroup.noteNumber;

	if (midifx < NUM_MIDIFX_GROUPS)
	{
		// Serial.println("seqNoteOff Send to midifx");
		// noteGroup.Print();
		subModeMidiFx[midifx].noteInput(noteGroup);
		// subModeMidiFx.noteInput(noteGroup);
	}
	else
	{
		// Serial.println("Send to post");
		onNotePostFX(noteGroup);
	}
}

// Called via doNoteOnForwarder
void OmxModeForm::doNoteOn(uint8_t keyIndex)
{
	MidiNoteGroup noteGroup = omxUtil.midiNoteOn2(omxFormGlobal.musicScale, keyIndex, midiSettings.defaultVelocity, sysSettings.midiChannel);

	if (noteGroup.noteNumber == 255)
		return;

	// Serial.println("doNoteOn: " + String(noteGroup.noteNumber));

	noteGroup.unknownLength = true;
	noteGroup.prevNoteNumber = noteGroup.noteNumber;

	onNotePostFX(noteGroup);

	// if (mfxIndex_ < NUM_MIDIFX_GROUPS)
	// {
	// 	subModeMidiFx[mfxIndex_].noteInput(noteGroup);
	// 	// subModeMidiFx.noteInput(noteGroup);
	// }
	// else
	// {
	// 	onNotePostFX(noteGroup);
	// }
}

// Called via doNoteOnForwarder
void OmxModeForm::doNoteOff(uint8_t keyIndex)
{
	MidiNoteGroup noteGroup = omxUtil.midiNoteOff2(keyIndex, sysSettings.midiChannel);

	if (noteGroup.noteNumber == 255)
		return;

	// Serial.println("doNoteOff: " + String(noteGroup.noteNumber));

	noteGroup.unknownLength = true;
	noteGroup.prevNoteNumber = noteGroup.noteNumber;

	onNotePostFX(noteGroup);

	// if (mfxIndex_ < NUM_MIDIFX_GROUPS)
	// {
	// 	subModeMidiFx[mfxIndex_].noteInput(noteGroup);
	// 	// subModeMidiFx.noteInput(noteGroup);
	// }
	// else
	// {
	// 	onNotePostFX(noteGroup);
	// }
}

// Called by the midiFX group when a note exits it's FX Pedalboard
void OmxModeForm::onNotePostFX(MidiNoteGroup note)
{
	if (note.noteOff)
	{
		// Serial.println("OmxModeForm::onNotePostFX noteOff: " + String(note.noteNumber));

		if (note.sendMidi)
		{
			MM::sendNoteOff(note.noteNumber, note.velocity, note.channel);
		}
		if (note.sendCV)
		{
			cvNoteUtil.cvNoteOff(note.noteNumber);
		}
	}
	else
	{
		if (note.unknownLength == false)
		{
			uint32_t noteOnMicros = note.noteonMicros; // TODO Might need to be set to current micros
			pendingNoteOns.insert(note.noteNumber, note.velocity, note.channel, noteOnMicros, note.sendCV);

			// Serial.println("StepLength: " + String(note.stepLength));

			uint32_t noteOffMicros = noteOnMicros + (note.stepLength * clockConfig.step_micros);
			pendingNoteOffs.insert(note.noteNumber, note.channel, noteOffMicros, note.sendCV);

			// Serial.println("noteOnMicros: " + String(noteOnMicros));
			// Serial.println("noteOffMicros: " + String(noteOffMicros));
		}
		else
		{
			// Serial.println("OmxModeForm::onNotePostFX noteOn: " + String(note.noteNumber));

			if (note.sendMidi)
			{
				midiSettings.midiLastNote = note.noteNumber;
				midiSettings.midiLastVel = note.velocity;
				MM::sendNoteOn(note.noteNumber, note.velocity, note.channel);
			}
			if (note.sendCV)
			{
				cvNoteUtil.cvNoteOn(note.noteNumber);
			}
		}
	}

	// uint32_t noteOnMicros = note.noteonMicros; // TODO Might need to be set to current micros
	// pendingNoteOns.insert(note.noteNumber, note.velocity, note.channel, noteOnMicros, note.sendCV);

	// uint32_t noteOffMicros = noteOnMicros + (note.stepLength * clockConfig.step_micros);
	// pendingNoteOffs.insert(note.noteNumber, note.channel, noteOffMicros, note.sendCV);
}

void OmxModeForm::onPendingNoteOff(int note, int channel)
{
	// Serial.println("OmxModeEuclidean::onPendingNoteOff " + String(note) + " " + String(channel));
	// subModeMidiFx.onPendingNoteOff(note, channel);

	for (uint8_t i = 0; i < NUM_MIDIFX_GROUPS; i++)
	{
		subModeMidiFx[i].onPendingNoteOff(note, channel);
	}
}

void OmxModeForm::togglePlayback()
{
	omxFormGlobal.isPlaying = !omxFormGlobal.isPlaying;

	if(omxFormGlobal.isPlaying)
	{
		if(stopped_)
		{
			seqConfig.currentClockTick = 0;
			omxUtil.startClocks();
			stopped_ = false;
		}
		else
		{
			omxUtil.resumeClocks();
		}

		omxLeds.setBlinkAutoRefresh(false);
	}
	else
	{
		// Tell all the MidiFX to stop
		for (uint8_t i = 0; i < NUM_MIDIFX_GROUPS; i++)
		{
			subModeMidiFx[i].resync();
		}

		omxUtil.stopClocks();
		omxLeds.setBlinkAutoRefresh(true);
	}

	for(auto m : machines_)
	{
		m->playBackStateChanged(omxFormGlobal.isPlaying);
	}
}

void OmxModeForm::resetPlayback()
{
	if(omxFormGlobal.isPlaying)
	{
		// Send midi start so external sequencers reset to start
		omxUtil.startClocks();
	}
	else
	{
		// Means start will send a start instead of resume
		stopped_ = true;
	}

	for(auto m : machines_)
	{
		m->resetPlayback();
	}
}

int OmxModeForm::saveToDisk(int startingAddress, Storage *storage)
{
	int initStart = startingAddress;

	for (uint8_t i = 0; i < kNumMachines; i++)
	{
		// Serial.println((String)"startingAddress: " + startingAddress);

		auto machine = machines_[i];

		if(machine == nullptr || machine->getType() == FORMMACH_NULL)
		{
			Serial.println("machine is null");
			storage->write(startingAddress, FORMMACH_NULL);
			startingAddress++;
		}
		else
		{
			uint8_t machineType = machine->getType();

			// Serial.println("machine type is: " + String(machineType));

			storage->write(startingAddress, machineType);
			startingAddress++;

			startingAddress = machine->saveToDisk(startingAddress, storage);
		}
	}

	int totalSize = startingAddress - initStart;

	Serial.println("FORM Size = " + String(totalSize));

	return startingAddress;
}

int OmxModeForm::loadFromDisk(int startingAddress, Storage *storage)
{
	// Serial.println((String)"startingAddress: " + startingAddress);

	for (uint8_t i = 0; i < kNumMachines; i++)
	{
		uint8_t machineType = storage->read(startingAddress);

		// Serial.println("machine type is: " + String(machineType));
		startingAddress++;

		changeMachineAtIndex(i, machineType);

		auto machine = machines_[i];

		if (machine != nullptr)
		{
			// uint8_t newMachType = machine->getType();

			// Serial.println("Machine at " + String(i) + " is " + String(newMachType));
			// Serial.println("Machine.getMachineIndex() = " + String(machine->getMachineIndex()));

			startingAddress = machine->loadFromDisk(startingAddress, storage);
		}
		else
		{
			Serial.println("machine is null");
		}

		// Serial.println((String)"startingAddress: " + startingAddress);
	}

	return startingAddress;
}