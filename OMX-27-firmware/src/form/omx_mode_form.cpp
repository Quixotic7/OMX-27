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

#if BOARDTYPE == OMX2040
#include <LittleFS.h> // V3 pattern-bank persistence (see saveBankToFS)
#endif




// Full view names (popup on switch); the short tags live in the overview header.
static const char *kViewNames[FORMVIEW_COUNT] = {"MIX", "STEP", "TRANSPOSE", "NOTES", "PATTERNS", "MI", "TOOLS"};

OmxModeForm::OmxModeForm()
{

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
		machines_[i]->setChannel(i); // tracks default to MIDI ch 1-8
		trackHue_[i] = i * (256 / kNumMachines); // spread 8 hues around the wheel
	}

	for (uint8_t k = 0; k < 27; k++)
	{
		previewNote_[k] = -1;
		previewMach_[k] = 0;
	}

	for (uint8_t i = 0; i < kNumMachines; i++)
		trackAudible_[i] = true;

	selectMachine(0);

	ledUpdateTime_ = 0;
}

// F1 + page keys (3-6), shared by the Seq and Notes views: single-click selects the edit
// page; double-click solos that page; hold one page + press another enables just that
// range (loop). Muted pages don't play.
void OmxModeForm::handlePageGesture(FormOmni::FormMachineOmni *omni, uint8_t p, OMXKeypadEvent e)
{
	if (e.down() && !e.held())
	{
		if (heldPageMask_ != 0 && !(heldPageMask_ & (1 << p)))
		{
			// loop-range: enable pages between the first held page and p.
			uint8_t a = 0;
			for (uint8_t i = 0; i < 4; i++) if (heldPageMask_ & (1 << i)) { a = i; break; }
			uint8_t lo = a < p ? a : p, hi = a < p ? p : a, mask = 0;
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
			if (e.clicks() == 2) { omni->setEnabledPages(1 << p); omni->setActivePage(p); } // solo
			else if (e.quickClicked()) omni->setActivePage(p); // select edit page
		}
		heldPageMask_ &= ~(1 << p);
		if (heldPageMask_ == 0)
			pageGestureDone_ = false;
		omxLeds.setDirty();
	}
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

	if(patternBuffer_ != nullptr)
	{
		delete patternBuffer_;
		patternBuffer_ = nullptr;
	}
}

// ---- Preview-note bookkeeping ----
// Every manual note-on goes through here so its note-off can always be delivered from the
// key's release — even if a modifier (AUX/F-keys), an octave change, a track switch, or a
// view switch happens while the key is held.
// The selected track's keyboard scale: global, its own local scale, or null = chromatic.
MusicScales *OmxModeForm::kbScale()
{
	return getSelectedMachine()->keyboardScale();
}

void OmxModeForm::previewKeyOn(uint8_t key, int8_t note)
{
	if (key >= 27 || note < 0 || note > 127)
		return;
	// Monophonic track: live playing is mono too — a new note cuts the previous one.
	if (getSelectedMachine()->isMono())
	{
		for (uint8_t k2 = 1; k2 < 27; k2++)
			if (previewNote_[k2] >= 0 && previewMach_[k2] == selectedMachine_)
				previewKeyOff(k2);
	}
	previewKeyOff(key); // never leak an earlier note still ringing on this key
	getSelectedMachine()->previewNote(note, true);
	previewNote_[key] = note;
	previewMach_[key] = selectedMachine_;
}

int8_t OmxModeForm::previewKeyOff(uint8_t key)
{
	if (key >= 27)
		return -1;
	int8_t note = previewNote_[key];
	if (note >= 0)
	{
		machines_[previewMach_[key]]->previewNote(note, false);
		previewNote_[key] = -1;
	}
	return note;
}

bool OmxModeForm::isMachineValid(uint8_t machineIndex)
{
	return machineIndex < kNumMachines;
}

void OmxModeForm::selectMachine(uint8_t machineIndex)
{
	if (isMachineValid(machineIndex) == false)
		return;

	if (machineIndex != selectedMachine_)
	{
		seqF2Loaded_ = false; // the pick-up/drop buffer + hold are per-track
		seqF2Holding_ = false;
	}
	selectedMachine_ = machineIndex;
	machines_[machineIndex]->onSelected();
}

FormOmni::FormMachineOmni *OmxModeForm::getSelectedMachine()
{
	return machines_[selectedMachine_];
}

// ---- Patterns (v2 data layer) ----
// Every track is an OMNI machine (single-engine), so we can snapshot / restore each
// track's OmniSeq to/from the pattern bank.
void OmxModeForm::snapshotActivePattern()
{
	for (uint8_t i = 0; i < kNumMachines; i++)
	{
		auto omni = machines_[i];
		patterns_[activePattern_].tracks[i] = omni->getSeq();
	}
}

void OmxModeForm::loadPatternIntoMachines(uint8_t index)
{
	for (uint8_t i = 0; i < kNumMachines; i++)
	{
		auto omni = machines_[i];
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

void OmxModeForm::clearPattern(uint8_t index)
{
	if (index >= FORM_NUM_PATTERNS)
		return;

	patterns_[index] = FormPattern(); // default (empty) tracks
	if (index == activePattern_)
		loadPatternIntoMachines(activePattern_);
}

bool OmxModeForm::patternHasContent(uint8_t index)
{
	if (index >= FORM_NUM_PATTERNS)
		return false;
	if (index == activePattern_)
		snapshotActivePattern(); // fold live edits in before we inspect
	for (uint8_t t = 0; t < kNumMachines; t++)
		for (uint8_t s = 0; s < 64; s++)
			if (patterns_[index].tracks[t].tracks[0].steps[s].hasNotes())
				return true;
	return false;
}

// ---- v2 shell: view router ----
static uint8_t toolStepMode(uint8_t tool); // defined with the Tools view below

void OmxModeForm::setFormView(uint8_t view, bool silent)
{
	if (view >= FORMVIEW_COUNT)
		return;
	formView_ = view;
	pendingView_ = view; // keep the AUX-release commit from reverting a live switch
	heldTrackKey_ = -1;
	// Clear Notes-view hold state so a view switch mid-hold can't leave it stuck.
	notesPaletteEngaged_ = false;
	notesHoldMask_ = 0;
	notesModalHeld_ = false;
	notesHoldUIShown_ = false;
	notesF1Used_ = notesF2Used_ = false;
	// Every view REMEMBERS its menu position across switches (notesCursor_ / miCursor_ /
	// mixCursor_ / stepMenuPage_ / toolIndex_ / transParamsPage_ are deliberately not reset).
	if (view == FORMVIEW_TOOLS)
		stepEditMode_ = toolStepMode(toolIndex_); // the remembered tool's hold-step mode
	mixHeldStepMask_ = 0;
	mixHeldStepKey_ = -1;
	// Clear the F1+page gesture state too: a page key still physically held across a view
	// switch releases into the new view's handler, so its bit would otherwise stay stuck and
	// fire a phantom loop-range on the next F1+page press.
	heldPageMask_ = 0;
	pageGestureDone_ = false;
	seqF2Loaded_ = false; // start with an unloaded buffer in a new view
	seqF2Holding_ = false;

	// Editor views map to an OMNI UI mode, applied to every track so the view stays
	// consistent when you switch tracks. Patterns / MI are rendered by the container.
	uint8_t uiMode = 255;
	switch (view)
	{
	case FORMVIEW_MIX: uiMode = FormOmni::OMNIUIMODE_MIX; break;
	case FORMVIEW_TOOLS: uiMode = FormOmni::OMNIUIMODE_MIX; break; // step-row LEDs + playhead
	case FORMVIEW_STEP: uiMode = FormOmni::OMNIUIMODE_CONFIG; break;
	case FORMVIEW_TRANSPOSE: uiMode = FormOmni::OMNIUIMODE_TRANSPOSE; break;
	case FORMVIEW_NOTES: uiMode = FormOmni::OMNIUIMODE_NOTEEDIT; break;
	default: break;
	}
	if (uiMode != 255)
	{
		for (uint8_t i = 0; i < kNumMachines; i++)
			machines_[i]->setUiMode(uiMode);
	}

	if (!silent)
	{
		omxDisp.displayMessage(kViewNames[view]);
	}
	omxLeds.setDirty();
	omxDisp.setDirty();
}

// The MIX/SEQ page-1 track overview owns the encoder (for the view selector) only when no
// modifier is held, no track/step is held, and — in SEQ — we're on the overview (page 0).
// AUX is a modifier for browsing views via the key shortcuts (13-18); the tag boxes while it's held.
bool OmxModeForm::viewEditActive()
{
	return midiSettings.midiAUX;
}

// While AUX is held, keys 13-18 are the view selector: the selected (pending) view is
// lit WHITE, the rest dim. Whatever's lit is the view you'll drop into on release.
void OmxModeForm::updateAuxViewLEDs()
{
	for (uint8_t v = 0; v < FORMVIEW_COUNT; v++)
	{
		strip.setPixelColor(13 + v, v == pendingView_ ? WHITE : LOWWHITE);
	}
	// AUX layer transport/rec: 1 play · 2 reset · 3 rec-arm (red when armed) · 4 rec-mode.
	strip.setPixelColor(1, omxFormGlobal.isPlaying ? GREEN : LOWWHITE);
	strip.setPixelColor(2, LOWWHITE);
	strip.setPixelColor(3, omxFormGlobal.recArm ? RED : DKRED);
	strip.setPixelColor(4, omxFormGlobal.recReplace ? ORANGE : LOWWHITE);
}

static const char *kSwitchStyleNames[4] = {"FINISH LOOP", "NEXT BAR", "INSTANT", "CHAINED"};

void OmxModeForm::updatePatternsLEDs()
{
	bool blink = omxLeds.getBlinkState();
	// Top row 3-6: the switch style (active bright).
	for (uint8_t s = 0; s < 4; s++)
		strip.setPixelColor(3 + s, (s == switchStyle_) ? WHITE : LOWWHITE);
	// Low row 11-26: pattern slots. current = WHITE, queued blinks, chain members CYAN, rest DKCYAN.
	for (uint8_t i = 0; i < 16; i++)
	{
		uint32_t col = LEDOFF;
		if (i < FORM_NUM_PATTERNS)
		{
			col = (i == activePattern_) ? WHITE : DKCYAN;
			if (switchStyle_ == 3)
				for (uint8_t c = 0; c < chainLen_; c++)
					if (chain_[c] == i && i != activePattern_)
						col = CYAN;
			if ((int8_t)i == queuedPattern_)
				col = blink ? WHITE : LEDOFF;
		}
		strip.setPixelColor(11 + i, col);
	}
}

void OmxModeForm::onKeyUpdatePatterns(OMXKeypadEvent e)
{
	uint8_t k = e.key();
	bool down = e.down(), held = e.held();
	if (omxFormGlobal.shortcutMode == FORMSHORTCUT_AUX)
		return; // the AUX layer owns the keys while held

	// ---- Keys 1/2: quick tap = copy/paste the whole pattern; held = F1/F2 modifier ----
	if (k == 1)
	{
		if (down && !held)
			patF1Used_ = midiSettings.keyState[2]; // 2 already held -> F3, not a copy
		else if (!down && !patF1Used_ && e.quickClicked())
		{
			snapshotActivePattern();
			if (patternBuffer_ == nullptr)
				patternBuffer_ = new FormPattern(); // one-time alloc; null-guarded below
			if (patternBuffer_ != nullptr)
			{
				*patternBuffer_ = patterns_[activePattern_];
				omxDisp.displayMessage("COPY");
			}
		}
		return;
	}
	if (k == 2)
	{
		if (down && !held)
			patF2Used_ = midiSettings.keyState[1];
		else if (!down && !patF2Used_ && e.quickClicked() && patternBuffer_ != nullptr)
		{
			patterns_[activePattern_] = *patternBuffer_;
			loadPatternIntoMachines(activePattern_);
			omxDisp.displayMessage("PASTE");
			omxDisp.setDirty();
			omxLeds.setDirty();
		}
		return;
	}

	if (held || !down)
		return;
	// Top row 3-6: pick the switch style.
	if (k >= 3 && k <= 6)
	{
		switchStyle_ = k - 3;
		if (switchStyle_ != 3)
			chainLen_ = 0; // leaving Chained clears the chain
		// A pattern queued under Finish Loop / Next Bar must not be orphaned by the style
		// change (only those styles commit the queue): Instant fires it now, Chained drops it.
		if (queuedPattern_ >= 0)
		{
			if (switchStyle_ == 2)
			{
				uint8_t q = (uint8_t)queuedPattern_;
				queuedPattern_ = -1;
				switchPattern(q);
			}
			else if (switchStyle_ == 3)
			{
				queuedPattern_ = -1;
			}
		}
		omxDisp.displayMessage(kSwitchStyleNames[switchStyle_]);
		omxDisp.setDirty();
		omxLeds.setDirty();
		return;
	}
	// Low row 11-26: pattern slots.
	if (k >= 11 && k < 27)
	{
		uint8_t idx = k - 11;
		if (idx >= FORM_NUM_PATTERNS)
			return;
		// Hold F1 + slot = copy that slot into the buffer (paste with F2 on an empty slot).
		if (omxFormGlobal.shortcutMode == FORMSHORTCUT_F1)
		{
			patF1Used_ = true; // F1 acted as a modifier -> no quick-copy on its release
			if (idx == activePattern_)
				snapshotActivePattern(); // the live machines are the freshest copy
			if (patternBuffer_ == nullptr)
				patternBuffer_ = new FormPattern();
			if (patternBuffer_ != nullptr)
			{
				*patternBuffer_ = patterns_[idx];
				omxDisp.displayMessage("COPY " + String(idx + 1));
			}
			omxDisp.setDirty();
			omxLeds.setDirty();
			return;
		}
		// Hold F2 + slot = cut/paste: a filled slot is cut into the buffer + cleared, an empty
		// slot receives the buffer. Lets you move a pattern to another slot.
		if (omxFormGlobal.shortcutMode == FORMSHORTCUT_F2)
		{
			patF2Used_ = true; // F2 acted as a modifier -> no quick-paste on its release
			if (idx == activePattern_)
				snapshotActivePattern(); // the array copy is stale for the active slot
			if (patternHasContent(idx))
			{
				if (patternBuffer_ == nullptr)
					patternBuffer_ = new FormPattern();
				if (patternBuffer_ != nullptr)
				{
					*patternBuffer_ = patterns_[idx];
					clearPattern(idx);
					omxDisp.displayMessage("CUT");
				}
			}
			else if (patternBuffer_ != nullptr)
			{
				patterns_[idx] = *patternBuffer_;
				if (idx == activePattern_)
					loadPatternIntoMachines(activePattern_);
				omxDisp.displayMessage("PASTE");
			}
			omxDisp.setDirty();
			omxLeds.setDirty();
			return;
		}
		// Hold F3 + slot = clear that slot (no buffer involved — copy first if you need it).
		if (omxFormGlobal.shortcutMode == FORMSHORTCUT_F3)
		{
			patF1Used_ = patF2Used_ = true; // F3 = both keys; suppress their quick actions
			clearPattern(idx);
			if (idx == activePattern_)
				loadPatternIntoMachines(activePattern_);
			omxDisp.displayMessage("CLEAR " + String(idx + 1));
			omxDisp.setDirty();
			omxLeds.setDirty();
			return;
		}
		if (switchStyle_ == 3) // Chained: append to the chain (start it on the first tap)
		{
			if (chainLen_ < 16)
				chain_[chainLen_++] = idx;
			if (chainLen_ == 1)
			{
				chainPos_ = 0;
				switchPattern(idx);
			}
		}
		else if (switchStyle_ == 2 || !omxFormGlobal.isPlaying) // Instant (or stopped -> immediate)
		{
			switchPattern(idx);
		}
		else // Finish Loop / Next Bar: queue for the boundary
		{
			queuedPattern_ = idx;
		}
		omxDisp.setDirty();
		omxLeds.setDirty();
	}
}

void OmxModeForm::onDisplayPatterns()
{
	// Holding a modifier shows what the slot keys will do (like Seq's F-hold overlays).
	if (omxFormGlobal.shortcutMode == FORMSHORTCUT_F3)
	{
		omxDisp.dispGenericModeLabel("CLEAR SLOT", 0, 0);
		return;
	}
	if (omxFormGlobal.shortcutMode == FORMSHORTCUT_F1)
	{
		omxDisp.dispGenericModeLabel("COPY SLOT", 0, 0);
		return;
	}
	if (omxFormGlobal.shortcutMode == FORMSHORTCUT_F2)
	{
		omxDisp.dispGenericModeLabel("CUT PASTE", 0, 0);
		return;
	}

	// Switch-progress bar: Next Bar tracks the bar, the rest track the selected track's loop.
	float progress = 0.0f;
	if (omxFormGlobal.isPlaying)
	{
		if (switchStyle_ == 1)
			progress = (float)seqConfig.currentClockTick / 384.0f; // 1 bar = PPQ*4 ticks
		else
		{
			auto omni = getSelectedMachine();
			progress = omni->loopProgress();
		}
	}
	// Optional right-side tag: queued target (">Pn") or chain length ("CHn").
	char tag[10] = "";
	if (queuedPattern_ >= 0)
		snprintf(tag, sizeof(tag), ">P%d", queuedPattern_ + 1);
	else if (switchStyle_ == 3 && chainLen_ > 0)
		snprintf(tag, sizeof(tag), "CH%d", chainLen_);
	omxDisp.dispPatternPage(activePattern_, kSwitchStyleNames[switchStyle_], tag, progress);
}

// MI view — the standalone MI-mode keyboard, brought in for live playing over the running
// sequencer (§4.6). Plays the selected track's channel; records when armed (§7). Keys 1-26 map
// scale-aware via getNoteNumber (octave = AUX + 11/12); note-out via the track's previewNote.
void OmxModeForm::onKeyUpdateMI(OMXKeypadEvent e)
{
	uint8_t k = e.key();
	if (k == 0 || k >= 27)
		return; // AUX handled by the top-level layer

	// Releases are handled first, BEFORE the AUX-layer guard, and use the remembered note —
	// so a note started before AUX went down can't hang, and an octave/track change mid-hold
	// can't desync the note-off.
	if (!e.down())
	{
		int8_t played = previewKeyOff(k);
		if (played >= 0)
			recordNoteReleased(played); // capture the note's length (no-op if not being recorded)
		omxLeds.setDirty();
		return;
	}

	if (omxFormGlobal.shortcutMode == FORMSHORTCUT_AUX)
		return; // AUX layer owns the keys while held

	int8_t note = omxUtil.getNoteNumber(k, kbScale()); // per-track scale mode aware
	if (note < 0 || note > 127)
		return; // out of range / out-of-scale (locked)

	if (!e.held())
	{
		// Start-on-note: armed but stopped -> begin playback + recording on the first note played.
		if (omxFormGlobal.recArm && !omxFormGlobal.isPlaying)
		{
			resetPlayback();  // arm a fresh start from the loop beginning
			togglePlayback(); // start the transport
			recClearedMask_ = 0;
		}
		previewKeyOn(k, note); // note-on on the track's channel (remembered for the release)
		if (omxFormGlobal.recArm && omxFormGlobal.isPlaying)
			recordPlayedNote(note); // record into the selected track, keeping timing + length (§7)
		omxDisp.setDirty();
		omxLeds.setDirty();
	}
}

void OmxModeForm::updateMILEDs()
{
	// Scale-aware keyboard (root periwinkle / in-scale dim blue / off-scale dark), pressed =
	// white. Per-track scale mode: chromatic tracks stay dark until pressed; local-scale
	// tracks render from their own scale (getKeyColor consults the GLOBAL config, so the
	// local instance is rendered manually here).
	auto m = getSelectedMachine();
	MusicScales *ks = kbScale();
	if (ks == nullptr) // chromatic track
	{
		for (uint8_t q = 1; q < 27; q++)
			if (midiSettings.midiKeyState[q] == -1) // same guard as drawKeyboardScaleLEDs:
				strip.setPixelColor(q, LEDOFF);     // don't repaint MIDI-held keys
	}
	else if (m->getScaleMode() == FormOmni::FormMachineOmni::TRACKSCALE_LOCAL)
	{
		for (uint8_t q = 1; q < 27; q++)
		{
			if (midiSettings.midiKeyState[q] != -1)
				continue; // MIDI-held: leave its feedback alone (parity with the GLOBAL path)
			int kc = scaleConfig.group16 ? ks->getGroup16Color(q)
										 : ks->getScaleColor(((notes[q] % 12) + 12) % 12);
			uint32_t c = LEDOFF;
			if (kc == INSCALECOLOR)
				c = 0x000090;
			else if (kc == ROOTNOTECOLOR)
				c = 0xA2A2FF;
			strip.setPixelColor(q, c);
		}
	}
	else
	{
		omxLeds.drawKeyboardScaleLEDs(ks, 0xA2A2FF, 0x000090, LEDOFF);
	}
	for (uint8_t k = 1; k < 27; k++)
		if (midiSettings.keyState[k])
			strip.setPixelColor(k, WHITE);
	// REC FULL flash (P4): a short red blink on the AUX key when live-rec drops a note.
	if ((uint32_t)(millis() - recFullFlashMs_) < 150)
		strip.setPixelColor(0, RED);
}

// Encoder turn in the MI view: select mode moves the menu cursor; edit mode changes the value.
bool OmxModeForm::onEncoderMI(int dir)
{
	if (dir == 0)
		return true;
	// In the QUANTIZE submenu, the encoder scrubs the amount and previews it live on the track.
	if (miQuantSub_)
	{
		quantWork_ = (uint8_t)constrain((int)quantWork_ + dir * 5, 0, 100);
		quantMorphPreview();
		omxDisp.setDirty();
		return true;
	}
	if (miClearSub_)
	{
		clearSel_ = (uint8_t)constrain((int)clearSel_ + dir, 0, 1); // move between NO / YES
		omxDisp.setDirty();
		return true;
	}
	if (getEncoderSelect())
	{
		// Select mode: navigate the cursor. 0 keyboard; 1-5 SCALE (mode/root/scale/lock/group);
		// 6-8 MIDI; 9-10 MACROS; 11-13 ACTIONS; 14-19 CC slots+bank; 20 CC title.
		miCursor_ = (uint8_t)constrain((int)miCursor_ + dir, 0, 20);
		omxDisp.setDirty();
		return true;
	}
	// Edit mode (encoderSelect off, or AUX held):
	if (miCursor_ == 0)
	{
		// Page 0: the encoder changes the selected track (only in edit mode).
		int t = constrain((int)selectedMachine_ + dir, 0, kNumMachines - 1);
		if (t != (int)selectedMachine_)
			selectMachine((uint8_t)t);
		omxDisp.setDirty();
		omxLeds.setDirty();
		return true;
	}
	// Cursor map (menu-map §4): 0 keys · 1-5 SCALE (mode/root/scale/lock/group) · 6-8 MIDI ·
	// 9-10 MACROS · 11-13 ACTIONS · 14-19 CC · 20 CC title.
	auto omni = getSelectedMachine();
	if (miCursor_ == 1) // scale mode (GLOBAL / CHROMATIC / LOCAL)
		omni->editScaleMode(dir);
	else if (miCursor_ >= 2 && miCursor_ <= 5) // scale params: root/scale/lock/group
		notesEditScaleParam(miCursor_ - 2, dir);
	else if (miCursor_ == 6) // channel
		omni->setChannel((uint8_t)constrain((int)omni->getChannel() + dir, 0, 15));
	else if (miCursor_ == 7) // default velocity (also governs AUX-macro play, see doNoteOn)
		omni->editParamDefault(0, dir);
	else if (miCursor_ == 8) // octave
		midiSettings.octave = constrain(midiSettings.octave + dir, -5, 4);
	else if (miCursor_ == 9) // AUX macro select: Off / M8 / NRN / DEL
		midiMacroConfig.midiMacro = constrain(midiMacroConfig.midiMacro + dir, 0, nummacromodes);
	else if (miCursor_ == 10) // MPOT: may a selected macro take the pots in FORM? (default no)
	{
		omxFormGlobal.macroConsumesPots = (dir > 0);
		auxMacroManager_.setMacrosConsumePots(omxFormGlobal.macroConsumesPots);
	}
	else if (miCursor_ >= 14 && miCursor_ <= 19) // CC page (slots + bank), shared renderer
		editCCPage(miCursor_ - 14, dir);
	// 11-13 = action cells (click to fire); 20 = the CC title (click opens the editor)
	omxDisp.setDirty();
	omxLeds.setDirty();
	return true;
}

bool OmxModeForm::onEncoderButtonMI()
{
	// QUANTIZE (cursor 9) opens a submenu on click (like Pot Config): first click enters, next click
	// applies the previewed amount. Other items toggle select/edit as usual.
	if (miQuantSub_)
	{
		quantExitSubmenu(true); // apply
		return true;
	}
	if (miClearSub_)
	{
		// Confirm the Yes/No choice: YES clears the selected track's pattern, NO cancels.
		if (clearSel_ == 1)
		{
			getSelectedMachine()->clearTrackSteps();
			omxDisp.displayMessage("CLEARED");
		}
		closeClearSub();
		return true;
	}
	if (miCursor_ == 20) // the selectable "CC" title: open the CC-number editor for this bank
	{
		openPotConfig();
		return true;
	}
	if (miCursor_ == 11)
	{
		clearReturnView_ = -1; // opened from the MI menu -> stay in MI on exit
		quantEnterSubmenu();
		return true;
	}
	if (miCursor_ == 12)
	{
		miClearSub_ = true; // open the Yes/No confirm submenu (default NO)
		clearSel_ = 0;
		clearReturnView_ = -1; // opened from the MI menu -> stay in MI on exit
		omxDisp.setDirty();
		return true;
	}
	if (miCursor_ == 13)
	{
		openPotConfig();
		return true;
	}
	omxFormGlobal.encoderSelect = !omxFormGlobal.encoderSelect; // unified select/edit toggle
	omxDisp.setDirty();
	return true;
}

void OmxModeForm::onDisplayMI()
{
	auto omni = getSelectedMachine();

	// Modal submenus render FIRST, before any cursor-page branch: submenuSetReturn() lands
	// here from other views' ACTIONS cells with miCursor_ still parked wherever the user
	// last left MI — checking the pages first drew that page while the encoder was silently
	// scrubbing the live quantize preview (or the CLEAR confirm) underneath it.
	if (miQuantSub_)
	{
		omxDisp.dispGenericModeLabelDoubleLine("QUANTIZE", String(quantWork_).c_str(), 0, 0);
		return;
	}
	if (miClearSub_)
	{
		static const char *kYesNo[2] = {"NO", "YES"};
		omxDisp.dispOptionCombo("Clear Track?", kYesNo, 2, clearSel_, true);
		return;
	}

	// Scale page (cursor 1-5): the shared 5-cell Mode / Root / Scale / Lock / Group grid.
	if (miCursor_ >= 1 && miCursor_ <= 5)
	{
		dispScalePage5(miCursor_ - 1, !getEncoderSelect());
		return;
	}

	// MIDI page (cursor 6-8): Channel / Velocity / Octave. Velocity is the per-track
	// default that also governs AUX-macro play.
	if (miCursor_ >= 6 && miCursor_ <= 8)
	{
		const char *labels[4] = {"CHAN", "VEL", "OCT", ""};
		String vals[4];
		vals[0] = String(omni->getChannel() + 1);
		vals[1] = omni->paramDefaultBox(0);
		vals[2] = String((int)midiSettings.octave);
		const char *values[4] = {vals[0].c_str(), vals[1].c_str(), vals[2].c_str(), ""};
		bool locked[4] = {false, false, false, false};
		omxDisp.dispStepParams(labels, values, locked, miCursor_ - 6, !getEncoderSelect());
		return;
	}

	// MACROS page (cursor 9-10): AUX macro select (Off/M8/NRN/DEL) + MPOT (may the macro
	// take the pots in FORM).
	if (miCursor_ >= 9 && miCursor_ <= 10)
	{
		const char *labels[4] = {"MCRO", "MPOT", "", ""};
		String vals[4];
		vals[0] = String(macromodes[constrain(midiMacroConfig.midiMacro, 0, nummacromodes)]);
		vals[1] = omxFormGlobal.macroConsumesPots ? "Ĉ" : "Ć";
		const char *values[4] = {vals[0].c_str(), vals[1].c_str(), "", ""};
		bool locked[4] = {false, false, false, false};
		omxDisp.dispStepParams(labels, values, locked, miCursor_ - 9, !getEncoderSelect());
		return;
	}

	// CC page (cursor 14-20): the shared renderer. No P-Locks here — MI's low row plays
	// the keyboard, not steps.
	if (miCursor_ >= 14 && miCursor_ <= 20)
	{
		onDisplayCCPage(miCursor_ - 14, 0, -1);
		return;
	}

	// Actions page (cursor 11-13): QUANTIZE + CLEAR + POTS — click to fire/open
	// (@ = opens a submenu, µ = destructive).
	if (miCursor_ >= 11 && miCursor_ <= 13)
	{
		const char *labels[4] = {"QUANT", "CLEAR", "POTS", ""};
		const char *values[4] = {"@", "µ", "@", ""};
		bool locked[4] = {false, false, false, false};
		omxDisp.dispStepParams(labels, values, locked, miCursor_ - 11, false);
		return;
	}

	// Page 0: the shared track overview (tagged "MI"), but as a keyboard view — no page icons,
	// no step row. Overlay the active-page bars + playhead along the bottom.
	onDisplaySeqTrackPage(true);
	uint8_t pageLens[4] = {omni->getPageLen(0), omni->getPageLen(1), omni->getPageLen(2), omni->getPageLen(3)};
	// Show the playhead even while stopped — stop is a pause, so keep the position visible.
	int8_t playAbs = (int8_t)omni->playingStepIndex();
	omxDisp.drawPageBars(pageLens, omni->getEnabledPages(), playAbs);
}

// ---- Notes view (container-rendered chord editor with in-editor step nav) ----
//
// A real F4-start piano: keys 3-10 = sharps (F#4..A#5), 15-26 = naturals (F4..C6). The other
// keys are repurposed: 1/2 = copy/paste step (F1/F2), 11/12 = prev/next step, 14 = clear,
// 13 = unused. Holding both 1+2 (F3) turns the low row into a jump-to-step selector.
// -1 = not a note key; runtime note = base + octave*12.
static const int8_t kNotesKeyBase[27] = {
	-1,             // 0 AUX
	-1, -1,         // 1,2 copy/paste
	66, 68, 70,     // 3 F#4, 4 G#4, 5 A#4
	73, 75,         // 6 C#5, 7 D#5
	78, 80, 82,     // 8 F#5, 9 G#5, 10 A#5
	-1, -1, -1, -1, // 11 prev, 12 next, 13 unused, 14 clear
	65, 67, 69, 71, // 15 F4, 16 G4, 17 A4, 18 B4
	72, 74, 76,     // 19 C5, 20 D5, 21 E5
	77, 79, 81, 83, // 22 F5, 23 G5, 24 A5, 25 B5
	84};            // 26 C6

// Build the selected step's chord from every Notes-view piano key currently held (sorted,
// deduped, up to 6). A single held key writes a single note; holding several writes a chord.
void OmxModeForm::notesSetChordFromHeld()
{
	auto omni = getSelectedMachine();
	int8_t notes[6];
	uint8_t cnt = 0;
	for (uint8_t k = 3; k < 27; k++)
	{
		if (!midiSettings.keyState[k])
			continue;
		int8_t base = kNotesKeyBase[k];
		if (base < 0)
			continue;
		int16_t n = base + midiSettings.octave * 12;
		if (n < 0 || n > 127)
			continue;
		bool dup = false; // insertion sort, dedup
		uint8_t pos = cnt;
		for (uint8_t j = 0; j < cnt; j++)
		{
			if (notes[j] == n) { dup = true; break; }
			if (n < notes[j]) { pos = j; break; }
		}
		if (dup || cnt >= 6)
			continue;
		for (uint8_t j = cnt; j > pos; j--)
			notes[j] = notes[j - 1];
		notes[pos] = (int8_t)n;
		cnt++;
	}
	// Monophonic track: one note per step — keep the highest (matches playback, which
	// plays the last set note).
	if (omni->isMono() && cnt > 1)
	{
		notes[0] = notes[cnt - 1];
		cnt = 1;
	}
	for (uint8_t i = cnt; i < 6; i++)
		notes[i] = -1;
	omni->stepSetNotes(notesSelStep_, notes);
}

// Live recording: record a played note to the selected track's nearest playing step, keeping its
// micro-timing (nudge, scaled by recQuantize_) and — on release — its length. Replace mode clears
// the step the first time it's hit this pass; overdub just adds the note.
void OmxModeForm::recordPlayedNote(int8_t note)
{
	if (note < 0 || note > 127)
		return;
	if (recHeldCount_ >= 8)
	{
		// Full — don't clear a step's content for a note we can't record, but SAY so (P4):
		// a rate-limited REC FULL popup plus a short red flash on the AUX key, instead of
		// the note just silently vanishing mid-take.
		uint32_t now = millis();
		if ((uint32_t)(now - recFullWarnMs_) > 600)
		{
			recFullWarnMs_ = now;
			recFullFlashMs_ = now;
			omxDisp.displayMessage("REC FULL");
			omxLeds.setDirty();
		}
		return;
	}
	auto omni = getSelectedMachine();
	int8_t nudge;
	uint8_t step = omni->recordResolveStep(recQuantize_, nudge); // resolve step + nudge at play time
	if (step >= 64)
		return;
	// Replace mode: clear the target step once this pass, so old content stops immediately.
	if (omxFormGlobal.recReplace && !(recClearedMask_ & (1ULL << step)))
	{
		omni->clearStepNotesAbs(step);
		recClearedMask_ |= (1ULL << step);
	}
	// Defer the write until release: while the note is held it stays out of the pattern, so the
	// sequencer won't retrigger it and cut off the live-monitored note. The step/nudge — and the
	// track — are captured now (from where the playhead was), the length on release.
	recHeld_[recHeldCount_++] = {note, step, nudge, selectedMachine_, micros()};
	omxDisp.setDirty();
	omxLeds.setDirty();
}

// Commit one held note to its step: write the note + nudge (resolved at press) and the length —
// into the track it was recorded on, even if the selection changed while the note was held.
void OmxModeForm::commitRecHeld(const RecHeld &h)
{
	auto omni = machines_[h.track];
	omni->recordNoteToStep(h.step, h.note); // add note (dedup, default vel if the step was empty)
	omni->setStepNudge(h.step, h.nudge);
	float sm = omni->stepMicros();
	if (sm > 0.0f)
		omni->recordNoteLen(h.step, (float)(micros() - h.onMicros) / sm);
}

// Note released while recording: commit it (step/nudge/length) into the pattern.
void OmxModeForm::recordNoteReleased(int8_t note)
{
	for (uint8_t i = 0; i < recHeldCount_; i++)
	{
		if (recHeld_[i].note != note)
			continue;
		commitRecHeld(recHeld_[i]);
		recHeld_[i] = recHeld_[--recHeldCount_]; // remove (swap with last)
		omxDisp.setDirty();
		omxLeds.setDirty();
		return;
	}
}

// Commit every still-held recording note (called when playback stops).
void OmxModeForm::flushRecHeld()
{
	if (recHeldCount_ == 0)
		return;
	for (uint8_t i = 0; i < recHeldCount_; i++)
		commitRecHeld(recHeld_[i]);
	recHeldCount_ = 0;
	omxDisp.setDirty();
	omxLeds.setDirty();
}

// QUANTIZE submenu (entered by clicking the QUANT menu item). Snapshot the track's nudges so the
// amount can be scrubbed and previewed live, then applied (click) or cancelled (AUX).
// The Quant/Clear submenus render in the MI view. Opening them from another view's
// ACTIONS page switches there and remembers both the view AND its menu position, so
// closing lands exactly where the action was fired (setFormView resets the cursors).
void OmxModeForm::submenuSetReturn()
{
	clearReturnView_ = (formView_ == FORMVIEW_MI) ? (int8_t)-1 : (int8_t)formView_;
	subRetMixCursor_ = mixCursor_;
	subRetNotesCursor_ = notesCursor_;
	if (formView_ != FORMVIEW_MI)
		setFormView(FORMVIEW_MI, true);
}

// Return to the view (and menu position) the submenu was opened from.
void OmxModeForm::submenuReturn()
{
	if (clearReturnView_ < 0)
		return;
	setFormView((uint8_t)clearReturnView_, true);
	mixCursor_ = subRetMixCursor_;     // Mix: back to the ACTIONS cell (the machine's
	notesCursor_ = subRetNotesCursor_; // menu page survives in trackParams_)
	clearReturnView_ = -1;             // Seq: stepMenuPage_/Sel_ are never reset — already fine
}

void OmxModeForm::quantEnterSubmenu()
{
	auto omni = getSelectedMachine();
	for (uint8_t s = 0; s < 64; s++)
		quantOrigNudges_[s] = omni->trackPtr()->steps[s].nudge;
	quantWork_ = recQuantize_;
	miQuantSub_ = true;
	quantMorphPreview(); // reflect the current amount immediately
	omxDisp.setDirty();
	omxLeds.setDirty();
}

// Preview: morph each step's nudge from the snapshot toward the grid by quantWork_ (live, so it's
// audible while playing). 0 = original timing, 100 = fully snapped.
void OmxModeForm::quantMorphPreview()
{
	auto omni = getSelectedMachine();
	for (uint8_t s = 0; s < 64; s++)
	{
		int16_t n = (int16_t)lroundf((float)quantOrigNudges_[s] * (float)(100 - quantWork_) / 100.0f);
		omni->setStepNudge(s, (int8_t)n);
	}
}

void OmxModeForm::quantExitSubmenu(bool apply)
{
	if (apply)
		recQuantize_ = quantWork_; // keep the morphed nudges + remember the amount for live rec
	else
	{
		auto omni = getSelectedMachine();
		for (uint8_t s = 0; s < 64; s++)
			omni->setStepNudge(s, quantOrigNudges_[s]); // restore
	}
	miQuantSub_ = false;
	submenuReturn(); // opened from another view's ACTIONS page: go back where we were
	omxDisp.setDirty();
	omxLeds.setDirty();
}

// Close the CLEAR submenu, returning to whatever view it was opened from (if not the MI menu).
void OmxModeForm::closeClearSub()
{
	miClearSub_ = false;
	submenuReturn();
	omxDisp.setDirty();
	omxLeds.setDirty();
}

// The 5-cell track-aware scale page (MODE / ROOT / SCALE / LOCK / GROUP) is rendered by a single
// modular renderer that lives on the machine (it owns the scale state). Every view — Seq, MI,
// Notes, and the Mix machine menu — delegates here so the look/values stay identical everywhere.
void OmxModeForm::dispScalePage5(uint8_t sel, bool editing)
{
	getSelectedMachine()->drawScalePage5(sel, editing);
}

// Which param palette a Notes-view hold selects: 11 = velocity, 12 = length, 11+12 = math,
// 13 = chance. -1 = none held.
int8_t OmxModeForm::notesPaletteMode()
{
	bool h11 = notesHoldMask_ & 1, h12 = notesHoldMask_ & 2, h13 = notesHoldMask_ & 4;
	if (h13)
		return STEPMODE_CHANCE;
	if (h11 && h12)
		return STEPMODE_MATH;
	if (h11)
		return STEPMODE_VEL;
	if (h12)
		return STEPMODE_LENGTH;
	return -1;
}

// Scale page params: 0 root · 1 scale · 2 lock · 3 group. Root/scale are track-aware:
// on a LOCAL-scale track they edit that track's own root/pattern (via the machine);
// otherwise the global scaleConfig. Lock/group stay global.
void OmxModeForm::notesEditScaleParam(uint8_t param, int dir)
{
	if (dir == 0)
		return;
	switch (param)
	{
	case 0: getSelectedMachine()->editScaleRoot(dir); break;
	case 1: getSelectedMachine()->editScalePattern(dir); break;
	// LOCK/GROUP are inert while the track is effectively chromatic — same predicate the
	// renderer dims on. Arming GROUP with no active scale made getGroup16Note() return -1
	// for every key: a completely dead keyboard with the offending cell drawn greyed-out.
	case 2: if (!getSelectedMachine()->scaleIsChromatic()) scaleConfig.lockScale = (dir > 0); break; // lock
	case 3: if (!getSelectedMachine()->scaleIsChromatic()) scaleConfig.group16 = (dir > 0); break;   // group
	}
	omxDisp.setDirty();
	omxLeds.setDirty();
}

// Encoder turn in the Notes view: select mode moves the page cursor; edit mode changes the
// value (the step on the keyboard/notes pages, the param on the scale/step-param pages).
bool OmxModeForm::onEncoderNotes(int dir)
{
	if (dir == 0)
		return true;
	if (getEncoderSelect())
	{
		uint8_t prev = notesCursor_;
		notesCursor_ = (uint8_t)constrain((int)notesCursor_ + dir, 0, 24); // 21-24 = ACTIONS
		// Group messages (menu map): STEP LOCKS = the step-param grids, ACTIONS at the end.
		// (The notes/scale groups pop nothing.)
		if (notesCursor_ >= 21 && prev < 21)
			omxDisp.displayMessage("ACTIONS");
		else if (notesCursor_ >= 13 && notesCursor_ < 21 && (prev < 13 || prev >= 21))
			omxDisp.displayMessage("STEP LOCKS");
		omxDisp.setDirty();
		omxLeds.setDirty();
		return true;
	}
	auto omni = getSelectedMachine();
	if (notesCursor_ == 0)
	{
		// Keyboard page: the encoder walks the selected step and AUTO-ADVANCES across
		// pages — past step 16 rolls into the next page's step 1 (and back). Clamped at
		// page 1 step 1 (CCW) and page 4 step 16 (CW).
		int s = (int)notesSelStep_ + dir;
		uint8_t page = omni->activePage();
		if (s > 15 && page < 3)
		{
			omni->setActivePage(page + 1);
			s = 0;
		}
		else if (s < 0 && page > 0)
		{
			omni->setActivePage(page - 1);
			s = 15;
		}
		notesSelStep_ = (uint8_t)constrain(s, 0, 15);
	}
	else if (notesCursor_ <= 6) // seq notes page, note slots 0-5: edit that note's value
	{
		int8_t chord[6];
		omni->getStepNotes(notesSelStep_, chord);
		uint8_t slot = notesCursor_ - 1;
		chord[slot] = (int8_t)constrain(chord[slot] + dir, -1, 127);
		omni->stepSetNotes(notesSelStep_, chord);
	}
	else if (notesCursor_ == 7) // the names/numbers switch
		omxFormGlobal.useNoteNumbers = (dir > 0);
	else if (notesCursor_ == 8) // scale mode (GLOBAL / CHROMATIC / LOCAL)
		omni->editScaleMode(dir);
	else if (notesCursor_ <= 12) // scale params: root/scale/lock/group
		notesEditScaleParam(notesCursor_ - 9, dir);
	else if (notesCursor_ <= 20) // step params (pid 0-7)
		omni->editStepParam(notesSelStep_, notesCursor_ - 13, dir);
	else if (notesCursor_ == 24) // NTRY: note-entry behavior (Pressed / Toggle)
	{
		bool prev = omxFormGlobal.noteEntryToggle;
		omxFormGlobal.noteEntryToggle = dir > 0;
		if (prev != omxFormGlobal.noteEntryToggle)
			omxDisp.displayMessage(omxFormGlobal.noteEntryToggle ? "TOGGLE" : "PRESSED");
	}
	// cursors 21-23 = ACTIONS (Quant / Clear / Pots — no turn edit; fire on click)
	omxDisp.setDirty();
	omxLeds.setDirty();
	return true;
}

// Encoder click in the Notes view: the ACTIONS cells fire; otherwise toggle select/edit.
bool OmxModeForm::onEncoderButtonNotes()
{
	if (notesCursor_ == 21)
	{
		submenuSetReturn(); // the submenu renders in MI; come back here after
		quantEnterSubmenu();
		return true;
	}
	if (notesCursor_ == 22)
	{
		submenuSetReturn();
		miClearSub_ = true;
		clearSel_ = 0;
		omxDisp.setDirty();
		omxLeds.setDirty();
		return true;
	}
	if (notesCursor_ == 23)
	{
		openPotConfig();
		return true;
	}
	// cursor 24 (NTRY) is a normal value param: click toggles select/edit like any other.
	omxFormGlobal.encoderSelect = !omxFormGlobal.encoderSelect;
	omxDisp.setDirty();
	return true;
}

void OmxModeForm::onKeyUpdateNotes(OMXKeypadEvent e)
{
	uint8_t k = e.key();
	if (k == 0)
		return; // AUX handled by the top-level layer

	bool down = e.down();
	bool held = e.held();
	auto omni = getSelectedMachine();

	// Any release delivers the key's pending preview note-off first (with the remembered note),
	// so a modifier held over the release — or an octave/track change mid-hold — can't hang it.
	if (!down)
	{
		int8_t played = previewKeyOff(k);
		if (played >= 0)
		{
			recordNoteReleased(played); // commit the recorded note's length (no-op if not tracked)
			omxLeds.setDirty();
		}
	}

	// keyState[k] is still true on this key's own release event, so track the palette keys (11/12/13)
	// with an explicit mask and read F1/F2 with a release-aware check.
	if (k >= 11 && k <= 13)
	{
		if (down && !held)
			notesHoldMask_ |= (1 << (k - 11));
		else if (!down)
			notesHoldMask_ &= ~(1 << (k - 11));
	}
	bool f1h = midiSettings.keyState[1] && !(k == 1 && !down);
	bool f2h = midiSettings.keyState[2] && !(k == 2 && !down);

	// Track any modal-hold key (F1/F2 or a palette hold) so the popup can wait out a quick tap.
	bool modalNow = (notesHoldMask_ != 0) || f1h || f2h;
	if (modalNow && !notesModalHeld_)
	{
		notesHoldStartMs_ = millis();
		notesHoldUIShown_ = false;
	}
	notesModalHeld_ = modalNow;

	// Engage a param-palette hold when 11/12/13 is pressed with no F-key held (see notesPaletteMode).
	if ((k == 11 || k == 12 || k == 13) && down && !held && !f1h && !f2h &&
		omxFormGlobal.shortcutMode != FORMSHORTCUT_AUX)
	{
		if (!notesPaletteEngaged_)
			notesSuppressPrev_ = notesSuppressNext_ = false;
		notesPaletteEngaged_ = true;
		if ((notesHoldMask_ & 3) == 3) // both 11+12 held = math, no nav
			notesSuppressPrev_ = notesSuppressNext_ = true;
	}

	if (omxFormGlobal.shortcutMode == FORMSHORTCUT_AUX)
		return; // the AUX layer owns the keys while held

	// ---- Param-palette hold (11 vel / 12 len / 11+12 math / 13 chance): top row 1-10 = value ----
	if (notesPaletteEngaged_)
	{
		int8_t pmode = notesPaletteMode();
		if (pmode >= 0 && k >= 1 && k <= 10 && down && !held)
		{
			omni->setStepPalette(notesSelStep_, (uint8_t)pmode, k - 1);
			notesHoldUIShown_ = true; // an edit shows the popup immediately
			if (notesHoldMask_ & 1)
				notesSuppressPrev_ = true;
			if (notesHoldMask_ & 2)
				notesSuppressNext_ = true;
			omxDisp.setDirty();
			omxLeds.setDirty();
			return;
		}
		if (k == 11 || k == 12 || k == 13)
		{
			if (!down) // release: a clean quick tap of 11/12 navigates
			{
				if (k == 11 && !notesSuppressPrev_ && e.quickClicked() && notesSelStep_ > 0)
					notesSelStep_--;
				else if (k == 12 && !notesSuppressNext_ && e.quickClicked() && notesSelStep_ < 15)
					notesSelStep_++;
				if (notesHoldMask_ == 0)
					notesPaletteEngaged_ = false;
				omxDisp.setDirty();
				omxLeds.setDirty();
			}
			return;
		}
		return; // any other key is inert during a palette hold
	}

	// ---- Keys 1/2: quick tap = copy/paste step; held = F1/F2 modifier (handled via shortcutMode) ----
	if (k == 1)
	{
		if (down && !held)
			notesF1Used_ = midiSettings.keyState[2]; // 2 already held -> this is F3, not a copy
		else if (!down && !notesF1Used_ && e.quickClicked())
		{
			omni->stepCopy(notesSelStep_);
			omxDisp.displayMessage("COPY");
		}
		return;
	}
	if (k == 2)
	{
		if (down && !held)
			notesF2Used_ = midiSettings.keyState[1];
		else if (!down && !notesF2Used_ && e.quickClicked())
		{
			omni->stepPaste(notesSelStep_);
			omxDisp.displayMessage("PASTE");
		}
		return;
	}

	uint8_t sm = omxFormGlobal.shortcutMode;

	// Release of an F2-selected track key clears the hold, even if F2 was released first.
	if (!down && k >= 3 && k <= 10 && heldTrackKey_ == (int8_t)(k - 3))
	{
		heldTrackKey_ = -1;
		omxDisp.setDirty();
		omxLeds.setDirty();
		return;
	}

	// ---- F3 (hold F1+F2): rate (top 3-10) + page length (low 11-26), same as Seq ----
	if (sm == FORMSHORTCUT_F3)
	{
		if (down && !held && k >= 3 && k <= 10)
			omni->setRateShortcut(k - 3);
		else if (down && !held && k >= 11 && k < 27)
		{
			uint8_t page = omni->activePage();
			uint8_t pageLen = (k - 11) + 1;
			omni->setPageLen(page, pageLen);
			omxDisp.displayMessage("P" + String(page + 1) + " LEN " + String(pageLen));
		}
		notesF1Used_ = notesF2Used_ = true;
		notesHoldUIShown_ = true;
		omxLeds.setDirty();
		return;
	}

	// ---- Hold F1: pages (top 3-6, select/solo/loop), clear/undo (8-10) + jump-to-step ----
	if (sm == FORMSHORTCUT_F1)
	{
		if (k >= 3 && k <= 6)
		{
			handlePageGesture(omni, k - 3, e);
			notesF1Used_ = true;
			notesHoldUIShown_ = true;
			return;
		}
		if (handleF1PageActions(k, e))
		{
			notesF1Used_ = true;
			notesHoldUIShown_ = true;
			return;
		}
		if (down && !held && k >= 11 && k < 27)
		{
			notesSelStep_ = k - 11; // jump to step
			notesF1Used_ = true;
			notesHoldUIShown_ = true;
			omxDisp.setDirty();
			omxLeds.setDirty();
		}
		return;
	}

	// ---- Hold F2 + top row (3-10): select the track ----
	if (sm == FORMSHORTCUT_F2)
	{
		if (!down && k >= 3 && k <= 10 && heldTrackKey_ == (int8_t)(k - 3))
		{
			heldTrackKey_ = -1;
			omxDisp.setDirty();
			omxLeds.setDirty();
			return;
		}
		if (down && !held && k >= 3 && k < 3 + kNumMachines) // track keys past the count are inert
		{
			selectMachine(k - 3);
			heldTrackKey_ = k - 3;
			notesF2Used_ = true;
			notesHoldUIShown_ = true;
			omxDisp.setDirty();
			omxLeds.setDirty();
		}
		return;
	}

	// ---- No modifier: clear + piano ----
	// Key 14: quick tap = clear the step (into the buffer). Hold = keep the step but clear the
	// P-Lock on the currently-selected step param (on a step-param page). No message — the UI shows it.
	if (k == 14)
	{
		if (!down)
		{
			if (e.quickClicked())
				omni->stepCut(notesSelStep_);
			else if (notesCursor_ >= 13 && notesCursor_ <= 20)
				omni->clearStepParamLock(notesSelStep_, notesCursor_ - 13);
			omxDisp.setDirty();
			omxLeds.setDirty();
		}
		return;
	}
	if (k == 11 || k == 12 || k == 13)
		return; // stray release after a palette hold disengaged

	// Piano keys (3-10 sharps, 15-26 naturals).
	int8_t base = kNotesKeyBase[k];
	if (base < 0)
		return;
	int16_t note = base + midiSettings.octave * 12;
	if (down && !held && omxFormGlobal.recArm && !omxFormGlobal.isPlaying)
	{
		// Start-on-note (same as the MI view): armed but stopped -> the first note played
		// starts the transport + recording.
		resetPlayback();
		togglePlayback();
		recClearedMask_ = 0;
	}
	if (down && !held)
	{
		if (omxFormGlobal.recArm)
		{
			// Armed = a live MI-style keyboard: the keys play (preview on the track's
			// channel) and record — they NEVER step-edit. Gating on recArm alone (not
			// "armed && playing") means the very first start-on-note press can't fall
			// into the toggle/step-edit branch below.
			if (note >= 0 && note <= 127)
				previewKeyOn(k, (int8_t)note); // remembered per key; release sends the off
			if (omxFormGlobal.isPlaying)
				recordPlayedNote((int8_t)note); // quantize into the nearest playing step
		}
		else if (omxFormGlobal.noteEntryToggle)
		{
			// Toggle entry: each press adds the note to the step, or removes it if present.
			if (note >= 0 && note <= 127)
			{
				if (omni->stepHasNote(notesSelStep_, (int8_t)note))
					omni->stepRemoveNote(notesSelStep_, (int8_t)note);
				else
					omni->stepAddNote(notesSelStep_, (int8_t)note);
			}
		}
		else
			notesSetChordFromHeld(); // edit the selected step (not armed)
		// Audible feedback while auditioning the step edits (stopped, unarmed).
		if (!omxFormGlobal.recArm && !omxFormGlobal.isPlaying && note >= 0 && note <= 127)
			previewKeyOn(k, (int8_t)note);
		omxDisp.setDirty();
		omxLeds.setDirty();
	}
	// (Releases are fully handled by the preview flush at the top of this function.)
}

void OmxModeForm::updateNotesLEDs()
{
	auto omni = getSelectedMachine();
	bool blink = omxLeds.getBlinkState();

	for (uint8_t i = 1; i < 27; i++)
		strip.setPixelColor(i, LEDOFF);

	// Param-palette hold (11 vel / 12 len / 11+12 math / 13 chance): top row 1-10 = value palette,
	// current level bright, held key(s) lit. Gated on the same delay as the OLED so a quick nav
	// tap doesn't flash it.
	if (notesPaletteEngaged_ && notesHoldUIShown_)
	{
		int8_t pmode = notesPaletteMode();
		int16_t sel = (pmode >= 0) ? omni->stepPaletteSelected(notesSelStep_, (uint8_t)pmode) : -1;
		for (uint8_t p = 0; p < 10; p++)
			strip.setPixelColor(1 + p, ((int)p == sel) ? (uint32_t)LTYELLOW : (uint32_t)DKBLUE);
		if (midiSettings.keyState[11]) strip.setPixelColor(11, WHITE);
		if (midiSettings.keyState[12]) strip.setPixelColor(12, WHITE);
		if (midiSettings.keyState[13]) strip.setPixelColor(13, WHITE);
		return;
	}

	uint8_t sm = omxFormGlobal.shortcutMode;

	// F3 (F1+F2): top 3-10 = rate options, low row = page-length bar.
	if (sm == FORMSHORTCUT_F3)
	{
		int8_t rsel = omni->rateShortcutSel();
		for (uint8_t p = 0; p < 8; p++)
			strip.setPixelColor(3 + p, ((int)p == rsel) ? (uint32_t)CYAN : (uint32_t)DKCYAN);
		uint8_t plen = omni->getPageLen(omni->activePage());
		for (uint8_t i = 0; i < 16; i++)
			strip.setPixelColor(11 + i, (i < plen) ? ((i == plen - 1) ? (uint32_t)GREEN : (uint32_t)LOWWHITE) : (uint32_t)LEDOFF);
		return;
	}

	// Hold F1: top 3-6 = pages (selected GREEN / enabled BLUE / muted dim), low row = jump selector.
	if (sm == FORMSHORTCUT_F1)
	{
		uint8_t enabled = omni->getEnabledPages(), active = omni->activePage();
		for (uint8_t p = 0; p < 4; p++)
		{
			uint32_t c;
			if (p == active)
				c = (enabled & (1 << p)) ? (uint32_t)GREEN : (uint32_t)0xFF4040; // disabled+selected: BRIGHT red
			else
				c = (enabled & (1 << p)) ? (uint32_t)BLUE : (uint32_t)VLOWWHITE;
			strip.setPixelColor(3 + p, c);
		}
		paintF1ActionKeys(blink); // 8/9/10 = clear page / clear track / undo-redo
		for (uint8_t i = 0; i < 16; i++)
		{
			uint32_t c = omni->stepHasNotes(i) ? (uint32_t)LTBLUE : (uint32_t)DKBLUE;
			if (i == notesSelStep_)
				c = blink ? (uint32_t)WHITE : (uint32_t)LTBLUE;
			strip.setPixelColor(11 + i, c);
		}
		strip.setPixelColor(1, WHITE); // F1 held
		return;
	}

	// Hold F2: top 3-10 = tracks (selected WHITE, muted RED, else hue).
	if (sm == FORMSHORTCUT_F2)
	{
		for (uint8_t t = 0; t < kNumMachines; t++)
		{
			uint32_t hue = trackHueColor(t);
			uint32_t c = (t == selectedMachine_) ? (uint32_t)WHITE : (machines_[t]->getMute() ? (uint32_t)RED : hue);
			strip.setPixelColor(3 + t, c);
		}
		strip.setPixelColor(2, WHITE); // F2 held
		return;
	}

	strip.setPixelColor(1, DKCYAN);  // copy
	strip.setPixelColor(2, DKGREEN); // paste
	strip.setPixelColor(11, DKBLUE); // prev step
	strip.setPixelColor(12, DKBLUE); // next step
	strip.setPixelColor(14, DKRED);  // clear step

	// Piano: scale-aware colours like MI mode (root periwinkle / in-scale dim blue / off-scale
	// dark), with a chromatic fallback when no scale is set. The current step's chord = LTYELLOW
	// — but ONLY while editing. With record ARMED this is a live keyboard: highlighting the
	// selected step's chord read as "stuck notes" mid-take, so armed shows pressed keys WHITE
	// (like MI) and nothing else.
	bool haveScale = !omni->scaleIsChromatic(); // per-track scale mode aware
	int8_t chord[6] = {-1, -1, -1, -1, -1, -1};
	if (!omxFormGlobal.recArm)
		omni->getStepNotes(notesSelStep_, chord);
	for (uint8_t key = 3; key < 27; key++)
	{
		int8_t base = kNotesKeyBase[key];
		if (base < 0)
			continue;
		int16_t note = base + midiSettings.octave * 12;
		uint8_t pc = (uint8_t)(((note % 12) + 12) % 12);
		uint32_t c;
		if (haveScale)
			c = (uint32_t)omni->paletteScale()->getScaleColor(pc);
		else
			c = (pc == 0) ? (uint32_t)0xA2A2FF : (key <= 10 ? (uint32_t)DKBLUE : (uint32_t)0x000090);
		for (uint8_t n = 0; n < 6; n++)
			if (chord[n] == note) { c = (uint32_t)LTYELLOW; break; }
		if (omxFormGlobal.recArm && midiSettings.keyState[key])
			c = WHITE; // live playing feedback while armed
		strip.setPixelColor(key, c);
	}

	// REC FULL flash (P4): a short red blink on the AUX key when live-rec drops a note.
	if ((uint32_t)(millis() - recFullFlashMs_) < 150)
		strip.setPixelColor(0, RED);
}

void OmxModeForm::onDisplayNotes()
{
	auto omni = getSelectedMachine();

	uint8_t stepState[16];
	fillStepStates(omni, stepState);
	uint8_t pageLen = omni->getPageLen(omni->activePage());

	// Param-palette hold (vel / length / math / chance): the value + strip, after the popup delay.
	if (notesPaletteEngaged_ && notesHoldUIShown_)
	{
		int8_t pmode = notesPaletteMode();
		if (pmode >= 0)
		{
			String v = omni->stepValueString(notesSelStep_, (uint8_t)pmode);
			omxDisp.dispStepOverview(v.c_str(), stepState, pageLen, notesSelStep_);
			return;
		}
	}

	// F1/F2/F3 hold menus, only after the popup delay (so a quick tap doesn't flash them).
	uint8_t sm = omxFormGlobal.shortcutMode;
	if (notesHoldUIShown_)
	{
		// F3: the exact same LEN | RATE screen as holding F3 on the Seq page.
		if (sm == FORMSHORTCUT_F3)
		{
			dispF3RateLength(omni, omni->getPageLen(omni->activePage()));
			return;
		}
		// Hold F1: "JUMP" + the page-1 page icons on the right; the low row is the jump selector.
		if (sm == FORMSHORTCUT_F1)
		{
			omxDisp.dispNotesJump(stepState, pageLen, notesSelStep_, omni->getEnabledPages(), omni->activePage());
			return;
		}
		// Hold F2: track select.
		if (sm == FORMSHORTCUT_F2)
		{
			char tbuf[16];
			snprintf(tbuf, sizeof(tbuf), "TRACK %u", (unsigned)(selectedMachine_ + 1));
			omxDisp.dispStepOverview(tbuf, stepState, pageLen, notesSelStep_);
			return;
		}
	}

	int8_t chord[6];
	omni->getStepNotes(notesSelStep_, chord);
	int8_t noteKeys[6];
	for (uint8_t i = 0; i < 6; i++)
		noteKeys[i] = (chord[i] >= 0 && chord[i] <= 127) ? omxUtil.noteNumberToKeyNumber(chord[i]) : -1;

	// --- Encoder pages ---
	// Page 1 (cursor 1-7): the exact Seq STEPNOTES page — 6 selectable note slots + the
	// names/numbers switch (cursor 7 = switch). encoderSelect is true in select mode.
	if (notesCursor_ >= 1 && notesCursor_ <= 7)
	{
		const char *headers[1] = {omxFormGlobal.useNoteNumbers ? "Note Numbers" : "Notes"};
		String slots[6];
		const char *labels6[6];
		for (uint8_t i = 0; i < 6; i++)
		{
			if (chord[i] >= 0 && chord[i] <= 127)
			{
				slots[i] = omxFormGlobal.useNoteNumbers ? String((int)chord[i]) : String(MusicScales::getFullNoteName(chord[i]));
				labels6[i] = slots[i].c_str();
			}
			else
				labels6[i] = "-";
		}
		omxDisp.dispNoteSlots(labels6, headers[0], notesCursor_ - 1, getEncoderSelect());
		return;
	}

	// Scale page (cursor 8-12): the shared 5-cell Mode / Root / Scale / Lock / Group grid.
	if (notesCursor_ >= 8 && notesCursor_ <= 12)
	{
		dispScalePage5(notesCursor_ - 8, !getEncoderSelect());
		return;
	}

	// Step-param pages (cursor 13-20): pid 0-3 (Vel/Nudge/Len/MFX) or 4-7 (Prob/Cond/Func/Accum).
	if (notesCursor_ >= 13 && notesCursor_ <= 20)
	{
		uint8_t base = (notesCursor_ <= 16) ? 0 : 4;
		const char *labels[4];
		String vals[4];
		const char *values[4];
		bool locked[4];
		for (uint8_t i = 0; i < 4; i++)
		{
			uint8_t pid = base + i;
			labels[i] = omni->stepParamLabel(pid);
			vals[i] = omni->stepParamBox(notesSelStep_, pid);
			values[i] = vals[i].c_str();
			locked[i] = omni->stepParamLocked(notesSelStep_, pid);
		}
		omxDisp.dispStepParams(labels, values, locked, notesCursor_ - 13 - base, !getEncoderSelect());
		return;
	}

	// ACTIONS (cursor 21-24): Quant / Clear / Pots / Note entry — click to fire
	// (@ = submenu, µ = destructive); NTRY toggles Pressed/Toggle.
	if (notesCursor_ >= 21)
	{
		const char *labels[4] = {"QNT", "CLR", "POTS", "NTRY"};
		const char *values[4] = {"@", "µ", "@", omxFormGlobal.noteEntryToggle ? "TG" : "PR"};
		bool locked[4] = {false, false, false, false};
		bool editing = (notesCursor_ == 24 && !getEncoderSelect()); // only NTRY edits
		omxDisp.dispStepParams(labels, values, locked, notesCursor_ - 21, editing);
		return;
	}

	// Page 0: the main keyboard + step strip — except while RECORD is armed. Recording in
	// Notes captures live (like MI): the on-screen keyboard highlights the keys you're
	// PHYSICALLY holding (not the selected step's chord, which read as stuck notes
	// mid-take), and the bottom shows the MI-style page/playhead bars instead of the
	// selected-step markers.
	if (omxFormGlobal.recArm)
	{
		int8_t liveKeys[6] = {-1, -1, -1, -1, -1, -1};
		uint8_t cnt = 0;
		for (uint8_t kk = 3; kk < 27 && cnt < 6; kk++)
			if (midiSettings.keyState[kk] && kNotesKeyBase[kk] >= 0)
				liveKeys[cnt++] = (int8_t)kk;
		omxDisp.dispStepNoteKeyboard(liveKeys, stepState, pageLen, -1, false, (int8_t)(omni->activePage() + 1));
		uint8_t pageLens[4] = {omni->getPageLen(0), omni->getPageLen(1), omni->getPageLen(2), omni->getPageLen(3)};
		int8_t playAbs = (int8_t)omni->playingStepIndex();
		omxDisp.drawPageBars(pageLens, omni->getEnabledPages(), playAbs);
	}
	else
		omxDisp.dispStepNoteKeyboard(noteKeys, stepState, pageLen, notesSelStep_, true, (int8_t)(omni->activePage() + 1));
}

// ---- Tools view (AUX+19): destructive pattern tools on the selected track ----
// Each menu page is one tool; keys 3-10 are that tool's action buttons; the low row
// auditions the pattern (shared with Mix). Layout/LEDs mirror the Seq view's step row.

enum ToolIndex
{
	TOOL_ROTATE,   // shift steps left/right
	TOOL_MIRROR,   // reverse step order
	TOOL_PAGE,     // cut / copy / paste the active 16-step page (steps + page length)
	TOOL_BPM,      // change tempo + tap tempo (global)
	TOOL_SHUFFLE,  // random permutation of steps
	TOOL_HUM,      // humanize: random nudge within a % range
	TOOL_QUANT,    // pull nudges toward the grid by AMT%
	TOOL_TRANS,    // transpose notes (Oct-/Oct+/Semi-/Semi+ buttons)
	TOOL_SCALE,    // snap notes to the current scale
	TOOL_VEL,      // randomize velocities between MIN..MAX
	TOOL_CHANCE,   // randomize step probability between MIN..MAX
	TOOL_EUC,      // euclidean rhythm generator
	TOOL_GRIDS,    // grids (topographic drum) generator
	TOOL_COUNT
};

static const char *kToolNames[TOOL_COUNT] = {
	"ROTATE", "MIRROR", "PAGE", "BPM", "SHUFFLE", "HUMANIZE", "QUANTIZE", "TRANSPOSE",
	"SCALE SNAP", "VEL RANDOM", "CHANCE RND", "EUCLID", "GRIDS"};

// Encoder cells per tool: params first, then action buttons. The cursor walks these.
static const uint8_t kToolParams[TOOL_COUNT] = {1, 1, 0, 1, 1, 2, 2, 1, 3, 18, 18, 3, 5};
static const uint8_t kToolBtns[TOOL_COUNT]   = {2, 1, 3, 1, 1, 1, 1, 4, 1, 0, 0, 0, 0};

// Distinct hue per tool for the action keys.
static const uint32_t kToolColors[TOOL_COUNT] = {
	CYAN, LTCYAN, LTPURPLE, RBLUE, DKCYAN, MAGENTA, ROSE, ORANGE, DKORANGE, YELLOW, DKYELLOW, GREEN, BLUE};

static const char *kGridsInstNames[4] = {"BD", "SD", "HH", "AC"};

// Every tool's hold-a-step editing mode (the Seq-view palette machinery is reused
// wholesale): VEL/CHANCE tools edit their own value, everything else edits notes.
static uint8_t toolStepMode(uint8_t tool)
{
	if (tool == TOOL_VEL) return STEPMODE_VEL;
	if (tool == TOOL_CHANCE) return STEPMODE_CHANCE;
	return STEPMODE_NOTE;
}

// Which tools carry the shared SCOPE param (keys 9 = page / 10 = track everywhere).
static bool toolHasScope(uint8_t tool)
{
	return tool != TOOL_VEL && tool != TOOL_CHANCE && tool != TOOL_PAGE && tool != TOOL_BPM;
}

// Perform a tool's action button (shared by the top-row keys and the encoder click).
// No popups — the step row / bars show the result (per the Tools UI spec).
// Undo key LED (key 10): blue = restorable, dim blue = empty slot. FLASHES for ~2s right
// after any destructive action (a snapshot was just taken) to say "you can undo this";
// using undo ends the flash.
void OmxModeForm::paintUndoKey(bool blink)
{
	bool undoReady = undoTrack_ >= 0 && undoPattern_ == (int8_t)activePattern_;
	bool flashing = undoFlashMs_ != 0 && (uint32_t)(millis() - undoFlashMs_) < 2000;
	uint32_t c = undoReady ? (uint32_t)BLUE : (uint32_t)DKBLUE;
	if (flashing)
		c = blink ? (uint32_t)BLUE : (uint32_t)LEDOFF;
	strip.setPixelColor(10, c);
}

// F1 layer keys 8/9/10 (clear page / clear track / undo-redo) — lit so the shortcuts
// are discoverable: orange / bright red / blue.
void OmxModeForm::paintF1ActionKeys(bool blink)
{
	strip.setPixelColor(8, ORANGE); // clear the active page
	strip.setPixelColor(9, RED);    // clear every page (whole track)
	paintUndoKey(blink);
}

// F1 + keys 8/9/10 (Step/Notes/Tools — the F1 page layer's action keys): 8 = clear the
// ACTIVE page's steps, 9 = clear every step on all pages, 10 = undo/redo (the same slot as
// Tools key 10). Both clears snapshot the track first, so F1+10 immediately reverses them.
bool OmxModeForm::handleF1PageActions(uint8_t k, OMXKeypadEvent e)
{
	if (k < 8 || k > 10 || !e.down() || e.held())
		return false;
	auto omni = getSelectedMachine();
	if (k == 8)
	{
		toolSnapshotUndo();
		omni->clearPageSteps(omni->activePage());
		omxDisp.displayMessage("CLR P" + String(omni->activePage() + 1));
	}
	else if (k == 9)
	{
		toolSnapshotUndo();
		omni->clearTrackSteps();
		omxDisp.displayMessage("CLR TRACK");
	}
	else
		toolUndo();
	omxDisp.setDirty();
	omxLeds.setDirty();
	return true;
}

// AUX + double-tap a view key: jump that view back to its first page/overview. Views
// deliberately remember their menu position across switches — this is the escape hatch
// when you're parked deep in a menu and just want the top of the view.
void OmxModeForm::viewHome(uint8_t view)
{
	switch (view)
	{
	case FORMVIEW_MIX: mixCursor_ = 0; break;
	case FORMVIEW_STEP: stepMenuPage_ = 0; stepMenuSel_ = 0; break;
	case FORMVIEW_TRANSPOSE: transParamsPage_ = false; transSel_ = 0; break;
	case FORMVIEW_NOTES: notesCursor_ = 0; break;
	case FORMVIEW_MI: miCursor_ = 0; break;
	case FORMVIEW_TOOLS:
		toolIndex_ = 0;
		toolCell_ = 0;
		stepEditMode_ = toolStepMode(0); // keep the hold-step palette in sync with the tool
		break;
	// FORMVIEW_PATTERNS has no cursor to reset
	}
	omxDisp.displayMessage("HOME");
	omxDisp.setDirty();
	omxLeds.setDirty();
}

// Shared BPM edit (P1): used by the BPM tool's encoder cell and the global F3+encoder
// gesture, so the clamp and the reclock can never drift apart.
void OmxModeForm::editBpm(int delta)
{
	clockConfig.newtempo = constrain((int)clockConfig.clockbpm + delta, 40, 300);
	if (clockConfig.newtempo != clockConfig.clockbpm)
	{
		clockConfig.clockbpm = clockConfig.newtempo;
		omxUtil.resetClocks();
	}
}

// One-level undo (P2): snapshot the selected track before a destructive tool action.
void OmxModeForm::toolSnapshotUndo()
{
	undoSeq_ = getSelectedMachine()->getSeq();
	undoTrack_ = (int8_t)selectedMachine_;
	undoPattern_ = (int8_t)activePattern_;
	undoNextIsRedo_ = false;
	undoFlashMs_ = millis(); // flash the undo key briefly: "you can undo this"
}

// Tools key 10: swap the snapshot with the live track — pressing again swaps back (redo).
// The slot dies with a pattern switch: restoring across patterns would paste the wrong music.
void OmxModeForm::toolUndo()
{
	if (undoTrack_ < 0 || undoPattern_ != (int8_t)activePattern_)
	{
		omxDisp.displayMessage("NO UNDO");
		return;
	}
	undoFlashMs_ = 0; // using undo ends the "you can undo" flash
	FormOmni::OmniSeq cur = machines_[undoTrack_]->getSeq();
	machines_[undoTrack_]->setSeq(undoSeq_);
	undoSeq_ = cur;
	omxDisp.displayMessage(undoNextIsRedo_ ? "REDO" : "UNDO");
	undoNextIsRedo_ = !undoNextIsRedo_;
	omxDisp.setDirty();
	omxLeds.setDirty();
}

void OmxModeForm::toolAction(uint8_t tool, uint8_t action)
{
	auto omni = getSelectedMachine();
	// Every destructive action snapshots the track first (key 10 = undo). BPM/tap and the
	// PAGE tool's COPY don't mutate the pattern, so they leave the undo slot alone.
	if (!(tool == TOOL_BPM || (tool == TOOL_PAGE && action == 0)))
		toolSnapshotUndo();
	switch (tool)
	{
	case TOOL_ROTATE:  omni->toolRotate(action == 0 ? -1 : 1, toolScopeAll_); break;
	case TOOL_MIRROR:  omni->toolMirror(toolScopeAll_); break;
	case TOOL_SHUFFLE: omni->toolShuffle(toolScopeAll_); break;
	case TOOL_HUM:     omni->toolHumanize(toolHumAmt_, toolScopeAll_); break;
	case TOOL_QUANT:   omni->toolQuantize(toolQuantAmt_, toolScopeAll_); break;
	case TOOL_TRANS:
	{
		static const int8_t kAmt[4] = {-12, 12, -1, 1}; // Oct- Oct+ Semi- Semi+
		omni->toolTranspose(kAmt[action & 3], toolScopeAll_);
		break;
	}
	case TOOL_SCALE:   omni->toolScaleRemap(toolScopeAll_); break;
	case TOOL_VEL:     omni->toolRandomVel(toolVelMin_, toolVelMax_); break;
	case TOOL_CHANCE:  omni->toolChanceRnd(toolChanceMin_, toolChanceMax_); break;
	case TOOL_EUC:     omni->toolEuclid(toolEucPulses_, toolEucRot_, toolScopeAll_); break;
	case TOOL_GRIDS:   omni->toolGrids(toolGridsInst_, toolGridsX_, toolGridsY_, toolGridsDens_, toolScopeAll_); break;
	case TOOL_BPM: // the single button is TAP TEMPO
		tapTempo();
		break;
	case TOOL_PAGE: // COPY (0) / CUT (1) / PASTE (2) the active page (F1 selects the page)
	{
		// COPY is deliberately button 0: this tool has no params, so the cursor rests on
		// the first button and the universal encoder-click fires it — the default must
		// never be the destructive CUT (a bare click used to blank the whole page).
		uint8_t page = omni->activePage();
		if (action == 2) // PASTE
		{
			if (pageBufferLoaded_)
			{
				omni->pastePageIn(page, pageBuffer_, pageBufferLen_);
				omxDisp.displayMessage("PASTE P" + String(page + 1));
			}
		}
		else // COPY (0) or CUT (1)
		{
			omni->copyPageOut(page, pageBuffer_, pageBufferLen_);
			pageBufferLoaded_ = true;
			if (action == 1) // CUT: clear the page after grabbing it
			{
				omni->clearPageSteps(page);
				omxDisp.displayMessage("CUT P" + String(page + 1));
			}
			else
				omxDisp.displayMessage("COPY P" + String(page + 1));
		}
		break;
	}
	}
	omxDisp.setDirty();
	omxLeds.setDirty();
}

void OmxModeForm::onKeyUpdateTools(OMXKeypadEvent e)
{
	uint8_t k = e.key();
	if (k == 0)
		return;
	if (omxFormGlobal.shortcutMode == FORMSHORTCUT_AUX)
		return; // AUX layer owns the keys while held

	// F1/F2/F3 behave exactly like the Seq view — delegate to its handler wholesale.
	if (omxFormGlobal.shortcutMode != FORMSHORTCUT_NONE)
	{
		onKeyUpdateStep(e);
		return;
	}
	// Hold key 3 + a low-row key = jump straight to a tool (keys 11-26 -> tool 0..N-1).
	// Key 3 isn't an action key for any tool, so it's free to use as the jump modifier.
	if (midiSettings.keyState[3] && e.down() && !e.held() && k >= 11 && k < 27)
	{
		uint8_t t = k - 11;
		if (t < TOOL_COUNT)
		{
			toolIndex_ = t;
			toolCell_ = 0;
			stepEditMode_ = toolStepMode(toolIndex_);
			omxDisp.displayMessage(kToolNames[toolIndex_]);
			omxDisp.setDirty();
			omxLeds.setDirty();
		}
		return;
	}

	// Release of an F2-held track key clears the hold even if F2 lifted first (as in Seq).
	if (!e.down() && k >= 3 && k <= 10 && heldTrackKey_ == (int8_t)(k - 3))
	{
		heldTrackKey_ = -1;
		omxDisp.setDirty();
		omxLeds.setDirty();
		return;
	}

	// The low row and the hold-step value palette are the Seq view's, with the tool's
	// step-edit mode (notes for most tools, velocity/chance for their random tools).
	if ((k >= 11 && k < 27) || heldStepMask_ != 0)
	{
		stepEditMode_ = toolStepMode(toolIndex_);
		onKeyUpdateStep(e);
		return;
	}
	if (e.held() || !e.down() || k < 3 || k > 10)
		return;

	// Shared SCOPE shortcut: key 9 toggles page/track (tools that have a scope).
	// (Key 10 used to be "track"; it's the UNDO key now — the scope is a single toggle.)
	if (toolHasScope(toolIndex_) && k == 9)
	{
		toolScopeAll_ = !toolScopeAll_;
		omxDisp.displayMessage(toolScopeAll_ ? "TRACK" : "PAGE");
		omxDisp.setDirty();
		omxLeds.setDirty();
		return;
	}
	// UNDO (key 10, every tool): restore the last destructive action; press again = redo.
	if (k == 10)
	{
		toolUndo();
		return;
	}
	// Per-tool action keys.
	switch (toolIndex_)
	{
	case TOOL_ROTATE:
		if (k == 6) toolAction(TOOL_ROTATE, 0); // left
		if (k == 7) toolAction(TOOL_ROTATE, 1); // right
		break;
	case TOOL_TRANS:
		if (k >= 5 && k <= 8) toolAction(TOOL_TRANS, k - 5); // Oct- Oct+ Semi- Semi+
		break;
	case TOOL_PAGE:
		if (k >= 6 && k <= 8) toolAction(TOOL_PAGE, k - 6); // COPY / CUT / PASTE
		break;
	default:
		if (k == 7) toolAction(toolIndex_, 0); // single apply/action key
		break;
	}
}

bool OmxModeForm::onEncoderTools(int dir)
{
	if (dir == 0)
		return true;
	auto omni = getSelectedMachine();
	// Holding step(s) in a value tool (VEL/CHANCE): the encoder adjusts the held steps'
	// value directly, alongside the top-row palette (a hold is an edit gesture).
	if (heldStepMask_ != 0 && toolStepMode(toolIndex_) != STEPMODE_NOTE)
	{
		uint8_t pid = (toolStepMode(toolIndex_) == STEPMODE_VEL) ? 0 : 4;
		for (uint8_t st = 0; st < 16; st++)
			if (heldStepMask_ & (1 << st))
				omni->editStepParam(st, pid, dir);
		stepEdited_ = true;
		omxDisp.setDirty();
		omxLeds.setDirty();
		return true;
	}
	uint8_t cells = kToolParams[toolIndex_] + kToolBtns[toolIndex_];
	if (getEncoderSelect())
	{
		uint8_t prevTool = toolIndex_;
		if (dir > 0)
		{
			if (toolCell_ + 1 < cells)
				toolCell_++;
			else if (toolIndex_ + 1 < TOOL_COUNT)
			{
				toolIndex_++;
				toolCell_ = 0;
			}
		}
		else
		{
			if (toolCell_ > 0)
				toolCell_--;
			else if (toolIndex_ > 0)
			{
				toolIndex_--;
				toolCell_ = kToolParams[toolIndex_] + kToolBtns[toolIndex_] - 1;
			}
		}
		if (toolIndex_ != prevTool)
		{
			stepEditMode_ = toolStepMode(toolIndex_); // hold-step palette follows the tool
			omxDisp.displayMessage(kToolNames[toolIndex_]); // tool crossed: name it
		}
		omxDisp.setDirty();
		return true;
	}
	// EDIT mode: change the param under the cursor (buttons don't edit — they click).
	uint8_t cell = toolCell_;
	if (cell >= kToolParams[toolIndex_])
		return true;
	switch (toolIndex_)
	{
	case TOOL_ROTATE:
	case TOOL_MIRROR:
	case TOOL_SHUFFLE:
		toolScopeAll_ = dir > 0; // shared scope: page (left) / track (right)
		break;
	case TOOL_HUM:
		if (cell == 0) toolScopeAll_ = dir > 0;
		if (cell == 1) toolHumAmt_ = (uint8_t)constrain((int)toolHumAmt_ + dir, 0, 100);
		break;
	case TOOL_QUANT:
		if (cell == 0) toolScopeAll_ = dir > 0;
		if (cell == 1) toolQuantAmt_ = (uint8_t)constrain((int)toolQuantAmt_ + dir, 0, 100);
		break;
	case TOOL_TRANS:
		toolScopeAll_ = dir > 0;
		break;
	case TOOL_SCALE:
		if (cell == 0 || cell == 1)
			notesEditScaleParam(cell, dir); // root/scale — pops the scale name like elsewhere
		if (cell == 2) toolScopeAll_ = dir > 0;
		break;
	case TOOL_VEL:
		if (cell == 0) toolVelMin_ = (uint8_t)constrain((int)toolVelMin_ + dir, 0, 127);
		else if (cell == 1) toolVelMax_ = (uint8_t)constrain((int)toolVelMax_ + dir, 0, 127);
		else
		{
			// Editing an empty slot creates a ghost step, then sets its velocity.
			if (!omni->stepIsOn(cell - 2))
				omni->stepNotesToGhost(cell - 2);
			omni->editStepParam(cell - 2, 0, dir);
		}
		break;
	case TOOL_CHANCE:
		if (cell == 0) toolChanceMin_ = (uint8_t)constrain((int)toolChanceMin_ + dir, 0, 100);
		else if (cell == 1) toolChanceMax_ = (uint8_t)constrain((int)toolChanceMax_ + dir, 0, 100);
		else
		{
			if (!omni->stepIsOn(cell - 2))
				omni->stepNotesToGhost(cell - 2);
			omni->editStepParam(cell - 2, 4, dir);
		}
		break;
	case TOOL_EUC:
		if (cell == 0) toolEucPulses_ = (uint8_t)constrain((int)toolEucPulses_ + dir, 0, 64);
		if (cell == 1) toolEucRot_ = (uint8_t)constrain((int)toolEucRot_ + dir, 0, 31);
		if (cell == 2) toolScopeAll_ = dir > 0;
		break;
	case TOOL_GRIDS:
		if (cell == 0) toolGridsInst_ = (uint8_t)constrain((int)toolGridsInst_ + dir, 0, 3);
		if (cell == 1) toolGridsX_ = (uint8_t)constrain((int)toolGridsX_ + dir * 4, 0, 255);
		if (cell == 2) toolGridsY_ = (uint8_t)constrain((int)toolGridsY_ + dir * 4, 0, 255);
		if (cell == 3) toolGridsDens_ = (uint8_t)constrain((int)toolGridsDens_ + dir * 4, 0, 255);
		if (cell == 4) toolScopeAll_ = dir > 0;
		break;
	case TOOL_BPM:
		if (cell == 0)
			editBpm(dir); // shared with the global F3+encoder gesture
		break;
	// TOOL_PAGE has no params — only CUT/COPY/PASTE buttons; nothing to edit here.
	}
	omxDisp.setDirty();
	return true;
}

// Encoder click in Tools: on an action-button cell it FIRES the action; on a param cell
// it falls through to the usual select/edit toggle. Returns true when consumed.
bool OmxModeForm::onEncoderButtonTools()
{
	if (formView_ != FORMVIEW_TOOLS)
		return false;
	if (toolCell_ >= kToolParams[toolIndex_])
	{
		toolAction(toolIndex_, toolCell_ - kToolParams[toolIndex_]);
		return true;
	}
	return false;
}

void OmxModeForm::updateToolsLEDs()
{
	// Hold key 3: show the tool-jump map — every tool on the low row, current one bright;
	// key 3 (the modifier) lit white. Tap a low-row key to jump straight to that tool.
	if (midiSettings.keyState[3])
	{
		strip.setPixelColor(3, WHITE);
		for (uint8_t t = 0; t < TOOL_COUNT; t++)
		{
			uint32_t tc = kToolColors[t];
			strip.setPixelColor(11 + t, (t == toolIndex_) ? tc : ((tc >> 3) & 0x1f1f1f));
		}
		return;
	}

	// Hold-step palette: the Seq view's LED pass owns the board.
	if (heldStepMask_ != 0)
	{
		stepEditMode_ = toolStepMode(toolIndex_);
		updateStepLEDs();
		return;
	}
	uint32_t c = kToolColors[toolIndex_];
	// Key 3 is the tool-jump modifier: keep it dimly lit so it's obviously live (hold it +
	// a low-row key to jump straight to a tool). It brightens to white while actually held.
	strip.setPixelColor(3, 0x303030);
	// Action keys lit in the tool colour; scope keys 9/10 show the current scope.
	switch (toolIndex_)
	{
	case TOOL_ROTATE: strip.setPixelColor(6, c); strip.setPixelColor(7, c); break;
	case TOOL_TRANS:  for (uint8_t k = 5; k <= 8; k++) strip.setPixelColor(k, c); break;
	case TOOL_PAGE:   strip.setPixelColor(6, c); strip.setPixelColor(7, c); strip.setPixelColor(8, c); break;
	default:          strip.setPixelColor(7, c); break;
	}
	if (toolHasScope(toolIndex_))
		strip.setPixelColor(9, toolScopeAll_ ? WHITE : LOWWHITE); // scope toggle: bright = track
	// Key 10 = UNDO: same LED language as the F1 layer (blue; flashes after a destructive
	// action; dim when the slot is empty).
	paintUndoKey(omxLeds.getBlinkState());

	// Step row: only actual triggers light (notes bright, ghosts dim, muted dark red);
	// empty steps stay OFF so the pattern reads at a glance. Playhead = steady green.
	auto omni = getSelectedMachine();
	for (uint8_t i = 0; i < 16; i++)
	{
		uint32_t sc = LEDOFF;
		if (omni->stepIsOn(i))
		{
			if (omni->getStepMute(i))
				sc = DKRED;
			else
				sc = omni->stepHasNotes(i) ? (uint32_t)LTBLUE : (uint32_t)DKBLUE;
		}
		strip.setPixelColor(11 + i, sc);
	}
	if (omxFormGlobal.isPlaying)
	{
		int16_t ph = (int16_t)omni->playingStepIndex() - (int16_t)omni->activePage() * 16;
		if (ph >= 0 && ph < 16)
			strip.setPixelColor(11 + ph, GREEN);
	}
}

void OmxModeForm::onDisplayTools()
{
	auto omni = getSelectedMachine();

	// F-layer screens, exactly as the Seq view shows them.
	if (omxFormGlobal.shortcutMode == FORMSHORTCUT_F3)
	{
		dispF3RateLength(omni, omni->getPageLen(omni->activePage()));
		return;
	}
	if (omxFormGlobal.shortcutMode == FORMSHORTCUT_F1 || omxFormGlobal.shortcutMode == FORMSHORTCUT_F2)
	{
		onDisplaySeqTrackPage();
		return;
	}
	// Hold-step palette UI, exactly as the Seq view shows it.
	if (heldStepMask_ != 0 && (stepHoldUIShown_ || stepEdited_))
	{
		onDisplayStep();
		return;
	}

	// Shared step-row data.
	uint8_t stepState[16];
	fillStepStates(omni, stepState);
	uint8_t pageLen = omni->getPageLen(omni->activePage());
	int16_t pageStart = (int16_t)omni->activePage() * 16;
	int8_t playhead = omxFormGlobal.isPlaying ? (int8_t)((int16_t)omni->playingStepIndex() - pageStart) : -1;

	int8_t sel = (int8_t)toolCell_;
	bool editing = !getEncoderSelect();
	const char *scopeVal = toolScopeAll_ ? "TRACK" : "PAGE";

	switch (toolIndex_)
	{
	case TOOL_ROTATE:
	{
		const char *pl[1] = {"SCOPE"};
		const char *pv[1] = {scopeVal};
		const char *btns[2] = {"<", ">"};
		omxDisp.dispToolActionPage(pl, pv, 1, btns, 2, sel, editing, stepState, pageLen, playhead);
		return;
	}
	case TOOL_MIRROR:
	case TOOL_SHUFFLE:
	{
		const char *pl[1] = {"SCOPE"};
		const char *pv[1] = {scopeVal};
		const char *btns[1] = {toolIndex_ == TOOL_MIRROR ? "MIRROR" : "SHUFFLE"};
		omxDisp.dispToolActionPage(pl, pv, 1, btns, 1, sel, editing, stepState, pageLen, playhead);
		return;
	}
	case TOOL_HUM:
	case TOOL_QUANT:
	{
		String amt = String(toolIndex_ == TOOL_HUM ? toolHumAmt_ : toolQuantAmt_);
		const char *pl[2] = {"SCOPE", "AMT%"};
		const char *pv[2] = {scopeVal, amt.c_str()};
		const char *btns[1] = {toolIndex_ == TOOL_HUM ? "HUMANIZE" : "QUANTIZE"};
		omxDisp.dispToolActionPage(pl, pv, 2, btns, 1, sel, editing, stepState, pageLen, playhead);
		return;
	}
	case TOOL_TRANS:
	{
		const char *pl[1] = {"SCOPE"};
		const char *pv[1] = {scopeVal};
		const char *btns[4] = {"OCT-", "OCT+", "SEMI-", "SEMI+"};
		omxDisp.dispToolActionPage(pl, pv, 1, btns, 4, sel, editing, nullptr, 0, -1);
		return;
	}
	case TOOL_SCALE:
	{
		// Track-aware values via the shared accessor: the encoder edits (and SNAP applies)
		// the per-track scale, so the display must show the same one — the old hardcoded
		// global readout never moved while a LOCAL track's scale was being edited under it.
		String rootV, scaleV;
		getSelectedMachine()->scaleValueStrings(rootV, scaleV);
		const char *pl[3] = {"ROOT", "SCALE", "SCOPE"};
		const char *pv[3] = {rootV.c_str(), scaleV.c_str(), scopeVal};
		const char *btns[1] = {"SNAP"};
		omxDisp.dispToolActionPage(pl, pv, 3, btns, 1, sel, editing, nullptr, 0, -1);
		return;
	}
	case TOOL_VEL:
	case TOOL_CHANCE:
	{
		bool isVel = toolIndex_ == TOOL_VEL;
		int16_t bars[16];
		uint8_t styles[16]; // 0 = no step, 1 = filled (notes), 2 = outlined (ghost)
		for (uint8_t i = 0; i < 16; i++)
		{
			styles[i] = omni->stepHasNotes(i) ? 1 : (omni->stepIsOn(i) ? 2 : 0);
			bars[i] = styles[i] != 0 ? (int16_t)omni->stepParamValue(i, isVel ? 0 : 4) : (int16_t)-1;
		}
		omxDisp.dispToolBarsPage(isVel ? toolVelMin_ : toolChanceMin_,
								 isVel ? toolVelMax_ : toolChanceMax_,
								 isVel ? 127 : 100,
								 bars, styles, isVel ? 127 : 100, sel, editing, playhead);
		return;
	}
	case TOOL_EUC:
	{
		bool preview[64];
		uint8_t plen = omni->buildEuclidPattern(toolEucPulses_, toolEucRot_, toolScopeAll_, preview);
		String p0 = String(toolEucPulses_), p1 = String(toolEucRot_);
		const char *pl[3] = {"PLS", "ROT", "SCOPE"};
		const char *pv[3] = {p0.c_str(), p1.c_str(), scopeVal};
		omxDisp.dispToolGenPage(pl, pv, 3, sel, editing, preview, plen, stepState, pageLen, playhead);
		return;
	}
	case TOOL_GRIDS:
	{
		bool preview[64];
		uint8_t vels[64];
		uint8_t plen = omni->buildGridsPattern(toolGridsInst_, toolGridsX_, toolGridsY_, toolGridsDens_, toolScopeAll_, preview, vels);
		String px = String(toolGridsX_), py = String(toolGridsY_), pd = String(toolGridsDens_);
		const char *pl[5] = {"INST", "X", "Y", "DENS", "SCOPE"};
		const char *pv[5] = {kGridsInstNames[toolGridsInst_ & 3], px.c_str(), py.c_str(), pd.c_str(), scopeVal};
		omxDisp.dispToolGenPage(pl, pv, 5, sel, editing, preview, plen, stepState, pageLen, playhead);
		return;
	}
	case TOOL_PAGE:
	{
		// No params — three buttons act on the ACTIVE page (F1 selects it). The step row
		// shows that page's content so you can see what you're cutting / copying.
		const char *btns[3] = {"COPY", "CUT", "PASTE"};
		omxDisp.dispToolActionPage(nullptr, nullptr, 0, btns, 3, sel, editing, stepState, pageLen, playhead);
		return;
	}
	case TOOL_BPM:
	{
		String bpm = String((int)clockConfig.clockbpm);
		const char *pl[1] = {"BPM"};
		const char *pv[1] = {bpm.c_str()};
		const char *btns[1] = {"TAP"};
		// Flash the TAP button "pressed" (inverted) briefly on each tap, in place of a popup.
		// The button is cell index 1 (after the 1 BPM param), so pass that as sel while flashing.
		int8_t bsel = sel;
		bool bedit = editing;
		if (bpmTapFlashMs_ != 0 && (millis() - bpmTapFlashMs_) < 90)
		{
			bsel = 1;      // point the selection at the TAP button so it renders inverted
			bedit = false; // buttons invert when selected regardless, but keep the param plain
		}
		omxDisp.dispToolActionPage(pl, pv, 1, btns, 1, bsel, bedit, stepState, pageLen, playhead);
		return;
	}
	}
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
// Which value palette edits a param-page pid. Nudge/Accum have no StepMode of their
// own — they use the machine's pseudo palette modes 8/9 (param pages only).
static const uint8_t PALMODE_NUDGE = 8;
static const uint8_t PALMODE_ACCUM = 9;
static uint8_t pidToPaletteMode(uint8_t pid)
{
	switch (pid)
	{
	case 0: return STEPMODE_VEL;
	case 1: return PALMODE_NUDGE;
	case 2: return STEPMODE_LENGTH;
	case 3: return STEPMODE_MFX;
	case 4: return STEPMODE_CHANCE;
	case 5: return STEPMODE_MATH;
	case 6: return STEPMODE_FUNC;
	case 7: return PALMODE_ACCUM;
	default: return 255;
	}
}

// Boost a palette colour close to white for the SELECTED value key, so the current
// value reads clearly against the dim rest of the palette (a faint tint of the mode's
// colour remains so the mode stays identifiable).
static uint32_t ledBrighten(uint32_t c)
{
	uint8_t r = (c >> 16) & 0xFF, g = (c >> 8) & 0xFF, b = c & 0xFF;
	r = r + (uint8_t)(((uint16_t)(255 - r) * 7) >> 3);
	g = g + (uint8_t)(((uint16_t)(255 - g) * 7) >> 3);
	b = b + (uint8_t)(((uint16_t)(255 - b) * 7) >> 3);
	return ((uint32_t)r << 16) | ((uint32_t)g << 8) | b;
}

// Palette key colour per palette mode (pseudo-modes get their own hues).
static uint32_t paletteModeColor(uint8_t mode)
{
	if (mode < STEPMODE_COUNT)
		return kStepModeColors[mode];
	return (mode == PALMODE_NUDGE) ? (uint32_t)ORANGE : (uint32_t)MAGENTA;
}

void OmxModeForm::stepApplyToHeld(uint8_t paletteIndex)
{
	auto omni = getSelectedMachine();
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
	auto omni = getSelectedMachine();

	// SCALE page (5): top-row keys 3-10 are a value palette for the SELECTED scale cell,
	// like the P-Lock param pages. MODE: 3/4/5 = GLOBAL/CHROMATIC/LOCAL. ROOT: 3-9 = the 7
	// major-scale notes (C D E F G A B). SCALE: 3-10 = the first 8 scale patterns. LOCK &
	// GROUP: 6 = off, 7 = on.
	if (stepMenuPage_ == 5 && omxFormGlobal.shortcutMode == FORMSHORTCUT_NONE &&
		e.down() && !e.held() && thisKey >= 3 && thisKey <= 10)
	{
		uint8_t k = thisKey - 3; // 0..7 across keys 3..10
		bool localScale = omni->getScaleMode() == FormOmni::FormMachineOmni::TRACKSCALE_LOCAL;
		switch (stepMenuSel_)
		{
		case 0: // MODE
			if (k <= 2)
				omni->editScaleMode((int)k - (int)omni->getScaleMode());
			break;
		case 1: // ROOT -> the 7 major-scale notes
		{
			static const uint8_t kMajRoots[7] = {0, 2, 4, 5, 7, 9, 11};
			if (k < 7)
			{
				int cur = localScale ? omni->getLocalRoot() : scaleConfig.scaleRoot;
				omni->editScaleRoot((int)kMajRoots[k] - cur);
			}
			break;
		}
		case 2: // SCALE -> the first 8 scale patterns (keys 3-10)
		{
			int cur = localScale ? omni->getLocalPattern() : scaleConfig.scalePattern;
			omni->editScalePattern((int)k - cur);
			break;
		}
		case 3: // LOCK (inert while the track is effectively chromatic — cell renders dimmed)
			if (omni->scaleIsChromatic()) break;
			if (thisKey == 6) scaleConfig.lockScale = false;
			else if (thisKey == 7) scaleConfig.lockScale = true;
			break;
		case 4: // GROUP (same guard: arming it with no active scale kills the keyboard)
			if (omni->scaleIsChromatic()) break;
			if (thisKey == 6) scaleConfig.group16 = false;
			else if (thisKey == 7) scaleConfig.group16 = true;
			break;
		}
		omxDisp.setDirty();
		omxLeds.setDirty();
		return;
	}

	// While step(s) are held on a param page (1-2), the top row is the SELECTED param's
	// value palette (applied to the held steps).
	if (heldStepMask_ != 0 && (stepMenuPage_ == 1 || stepMenuPage_ == 2) && thisKey >= 1 && thisKey <= 10)
	{
		uint8_t pid = (stepMenuPage_ - 1) * 4 + stepMenuSel_;
		uint8_t mode = pidToPaletteMode(pid);
		if (mode == 255)
			return; // nudge/accum have no palette
		if (e.down() && !e.held())
		{
			uint8_t base = (mode == STEPMODE_MFX) ? 5 : 1;
			if (thisKey >= base)
			{
				uint8_t pi = thisKey - base;
				auto omni2 = getSelectedMachine();
				if (pi < omni2->stepPaletteCount(mode))
				{
					for (uint8_t st = 0; st < 16; st++)
						if (heldStepMask_ & (1 << st))
							omni2->setStepPalette(st, mode, pi);
					stepEdited_ = true;
					omxDisp.setDirty();
					omxLeds.setDirty();
				}
			}
		}
		return;
	}

	// While step(s) are held on the overview page, the top row is the value palette.
	if (heldStepMask_ != 0 && stepMenuPage_ == 0 && thisKey >= 1 && thisKey <= 10)
	{
		// Note mode: keys 1-10 = chord entry. Held keys build the chord; a fresh press (from
		// no note keys held) replaces. Notes audition while held.
		if (stepEditMode_ == STEPMODE_NOTE)
		{
			uint8_t degree = thisKey - 1;
			int8_t note = omni->paletteScale()->getNoteByDegree(degree, midiSettings.octave);
			if (e.down() && !e.held())
			{
				if (note < 0 || note > 127)
					return; // octave extremes can push a degree out of MIDI range
				// Pressed mode: a fresh press replaces the step's notes. Toggle mode: each
				// press adds the note, or removes it if the step already has it (drums).
				bool fresh = (heldNoteKeys_ == 0) && !omxFormGlobal.noteEntryToggle;
				for (uint8_t s = 0; s < 16; s++)
					if (heldStepMask_ & (1 << s))
					{
						if (omxFormGlobal.noteEntryToggle && omni->stepHasNote(s, note))
						{
							omni->stepRemoveNote(s, note);
							continue;
						}
						if (fresh) omni->stepClearNotes(s);
						omni->stepAddNote(s, note);
					}
				heldNoteKeys_ |= (1 << degree);
				stepEdited_ = true;
				if (!omxFormGlobal.isPlaying)
					previewKeyOn(thisKey, note); // audition only while stopped (remembered per key)
				if (heldStepKey_ >= 0) omni->getStepNotes(heldStepKey_, lastNotes_); // remember chord
				omxDisp.setDirty();
				omxLeds.setDirty();
			}
			else if (!e.down())
			{
				heldNoteKeys_ &= ~(1 << degree);
				previewKeyOff(thisKey); // sends the off with the note actually auditioning (if any)
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

	// Track whether F1/F2 act as modifiers during this hold (for the param-page
	// quick-tap palette below): pressing one while the other is down = F3 = used.
	if (thisKey == 1 && e.down() && !e.held())
		stepF1Used_ = midiSettings.keyState[2];
	if (thisKey == 2 && e.down() && !e.held())
		stepF2Used_ = midiSettings.keyState[1];
	if (omxFormGlobal.shortcutMode == FORMSHORTCUT_F1 && thisKey >= 3)
		stepF1Used_ = true;
	if (omxFormGlobal.shortcutMode == FORMSHORTCUT_F2 && thisKey >= 3)
		stepF2Used_ = true;
	if (omxFormGlobal.shortcutMode == FORMSHORTCUT_F3)
		stepF1Used_ = stepF2Used_ = true;

	// Param pages: a quick TAP of key 1/2 (release; not used as a modifier) is the
	// selected param's palette keys 1/2. keyState is still true during the release, so
	// this must run before the shortcut-mode gate below.
	if ((stepMenuPage_ == 1 || stepMenuPage_ == 2) && heldStepMask_ == 0 &&
		omxFormGlobal.shortcutMode != FORMSHORTCUT_AUX &&
		(thisKey == 1 || thisKey == 2) && !e.down() && e.quickClicked() &&
		!(thisKey == 1 ? stepF1Used_ : stepF2Used_))
	{
		uint8_t pid = (stepMenuPage_ - 1) * 4 + stepMenuSel_;
		uint8_t mode = pidToPaletteMode(pid);
		if (mode != 255 && mode != STEPMODE_MFX) // MFX palette starts at key 5
		{
			auto omni2 = getSelectedMachine();
			uint8_t pi = thisKey - 1;
			if (pi < omni2->stepPaletteCount(mode))
			{
				omni2->setParamDefaultPalette(mode, pi);
				omxDisp.setDirty();
				omxLeds.setDirty();
			}
		}
		return;
	}

	// F1 + page keys (3-6): the shared page gesture (select / solo / loop-range).
	if (omxFormGlobal.shortcutMode == FORMSHORTCUT_F1 && (thisKey >= 3 && thisKey <= 6))
	{
		handlePageGesture(omni, thisKey - 3, e);
		return;
	}
	// F1 + 8/9/10: clear page / clear all pages / undo-redo (shared with Notes).
	if (omxFormGlobal.shortcutMode == FORMSHORTCUT_F1 && handleF1PageActions(thisKey, e))
		return;
	// Release of an F2-held track key clears the hold (even if F2 was let go first).
	if (!e.down() && thisKey >= 3 && thisKey <= 10 && heldTrackKey_ == (int8_t)(thisKey - 3))
	{
		heldTrackKey_ = -1;
		omxDisp.setDirty();
		omxLeds.setDirty();
		return;
	}
	// F2 + top row (3-10) = select the track; holding one exposes its controls on the low row.
	if (omxFormGlobal.shortcutMode == FORMSHORTCUT_F2 && thisKey >= 3 && thisKey < 3 + kNumMachines)
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
	// F2 + step = pick-up / drop. The first press with nothing loaded grabs the step (even an
	// empty one). After that: non-empty steps alternate cut/paste; empty steps always paste.
	if (omxFormGlobal.shortcutMode == FORMSHORTCUT_F2 && heldTrackKey_ < 0 && e.down() && !e.held() && thisKey >= 11 && thisKey < 27)
	{
		uint8_t k = thisKey - 11;
		if (!seqF2Loaded_)
		{
			// Initial grab (buffer empty): cut this step, even if it's empty.
			omni->stepCut(k);
			omxDisp.displayMessage("CUT");
			seqF2Loaded_ = true;
			seqF2Holding_ = true;
		}
		else if (!omni->stepIsOn(k))
		{
			// Empty step, buffer already loaded: always a paste (never cut).
			omni->stepPaste(k);
			omxDisp.displayMessage("PASTE");
			seqF2Holding_ = false;
		}
		else if (seqF2Holding_)
		{
			// Non-empty step, holding: drop into it.
			omni->stepPaste(k);
			omxDisp.displayMessage("PASTE");
			seqF2Holding_ = false;
		}
		else
		{
			// Non-empty step, empty-handed: grab it.
			omni->stepCut(k);
			omxDisp.displayMessage("CUT");
			seqF2Holding_ = true;
		}
		return;
	}
	// F1 + step key = COPY, always. F3 = structure layer.
	if (omxFormGlobal.shortcutMode == FORMSHORTCUT_F1 && e.down() && !e.held() && thisKey >= 11 && thisKey < 27)
	{
		omni->stepCopy(thisKey - 11);
		omxDisp.displayMessage("COPY");
		seqF2Loaded_ = true;  // a copy loads the buffer, so F2 won't do an initial grab
		seqF2Holding_ = true; // ...and puts you in the holding state: the next F2 press pastes
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

	// Param pages, nothing held: the top row sets the selected param's DEFAULT from its
	// value palette. Keys 3-10 on press; keys 1/2 on a quick tap (holds stay F1/F2).
	if ((stepMenuPage_ == 1 || stepMenuPage_ == 2) && heldStepMask_ == 0 && thisKey >= 1 && thisKey <= 10)
	{
		uint8_t pid = (stepMenuPage_ - 1) * 4 + stepMenuSel_;
		uint8_t mode = pidToPaletteMode(pid);
		if (mode == 255)
			return;
		if (thisKey < 3)
			return; // keys 1/2 are the quick-tap path above (holds stay F1/F2)
		bool fire = e.down() && !e.held();
		if (fire)
		{
			uint8_t base = (mode == STEPMODE_MFX) ? 5 : 1;
			if (thisKey >= base)
			{
				uint8_t pi = thisKey - base;
				auto omni2 = getSelectedMachine();
				if (pi < omni2->stepPaletteCount(mode))
				{
					omni2->setParamDefaultPalette(mode, pi);
					omxDisp.setDirty();
					omxLeds.setDirty();
				}
			}
		}
		return;
	}

	// Mode selector (overview only; on param pages the top row is the value palette).
	if (stepMenuPage_ == 0 && heldStepMask_ == 0 && e.down() && !e.held() && thisKey >= 3 && thisKey <= 10)
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
						previewKeyOff(d + 1); // key = degree + 1; uses the remembered note
				heldNoteKeys_ = 0;
			}
			omxDisp.setDirty();
			omxLeds.setDirty();
		}
	}
}

// Step-row LED colour for the Step view's pattern display: a step with notes reads
// bright blue-white (an "active" trigger), a ghost trigger (on, but no notes — a locked
// value/CC) reads bright orange, a muted step dark red, an empty step off.
static const uint32_t kStepActiveColor = 0xC0C0FF; // bright blue, almost white
static const uint32_t kStepGhostColor  = 0xFF6000; // bright orange
static uint32_t stepRowColor(FormOmni::FormMachineOmni *omni, uint8_t i)
{
	if (!omni->stepIsOn(i))
		return LEDOFF;
	if (omni->getStepMute(i))
		return DKRED;
	return omni->stepHasNotes(i) ? kStepActiveColor : kStepGhostColor;
}

void OmxModeForm::paintStepRow(FormOmni::FormMachineOmni *omni)
{
	int16_t pageStart = (int16_t)omni->activePage() * 16;
	int16_t playhead = (int16_t)omni->playingStepIndex() - pageStart;
	for (uint8_t i = 0; i < 16; i++)
	{
		uint32_t col = stepRowColor(omni, i);
		if (omxFormGlobal.isPlaying && i == playhead)
			col = GREEN; // playhead: steady bright green over the step
		strip.setPixelColor(11 + i, col);
	}
}

void OmxModeForm::updateStepLEDs()
{
	auto omni = getSelectedMachine();
	bool blink = omxLeds.getBlinkState();

	// F1 / F2 keys lit in the track colour (brighter when that modifier is pressed). The
	// hold-a-step palette overrides keys 1-2, so this is skipped there.
	if (heldStepMask_ == 0)
	{
		uint32_t hueFull = trackHueColor(selectedMachine_);
		uint32_t hueDim = (hueFull >> 3) & 0x1f1f1f;
		bool f1 = (omxFormGlobal.shortcutMode == FORMSHORTCUT_F1 || omxFormGlobal.shortcutMode == FORMSHORTCUT_F3);
		bool f2 = (omxFormGlobal.shortcutMode == FORMSHORTCUT_F2 || omxFormGlobal.shortcutMode == FORMSHORTCUT_F3);
		strip.setPixelColor(1, f1 ? hueFull : hueDim);
		strip.setPixelColor(2, f2 ? hueFull : hueDim);
	}

	// F1: top row 3-6 = pages; step row keeps the normal pattern colours (copy targets).
	// Colours: selected = GREEN (RED if muted) · enabled = BLUE · muted = very dim ·
	// currently-playing page = flashing YELLOW.
	if (omxFormGlobal.shortcutMode == FORMSHORTCUT_F1 && heldStepMask_ == 0)
	{
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
				c = enabled ? (uint32_t)GREEN : (uint32_t)0xFF4040; // disabled+selected: BRIGHT red
			else if (enabled)
				c = (uint32_t)BLUE;
			else
				c = (uint32_t)VLOWWHITE;
			if (omxFormGlobal.isPlaying && p == playingPage && blink)
				c = (uint32_t)YELLOW; // flashing playhead page
			strip.setPixelColor(3 + p, c);
		}
		paintF1ActionKeys(blink); // 8/9/10 = clear page / clear track / undo-redo
		paintStepRow(omni); // same step colours as the overview
		return;
	}
	// F2: top row 3-10 = the 8 tracks (track colour; selected white, muted red). Holding one
	// shows its controls on the low row (mute/solo/play mode/colour), else the low row is content.
	if (omxFormGlobal.shortcutMode == FORMSHORTCUT_F2 && heldStepMask_ == 0)
	{
		for (uint8_t t = 0; t < kNumMachines; t++)
		{
			uint32_t tc = trackHueColor(t);
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
			paintStepRow(omni); // same step colours as the overview
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

	// SCALE page (5): the top row is a value palette for the selected scale cell (the
	// current value lit bright, the other choices dim); the low row keeps the pattern.
	if (stepMenuPage_ == 5)
	{
		for (uint8_t k = 1; k <= 10; k++)
			strip.setPixelColor(k, LEDOFF);
		paintStepRow(omni);
		bool localScale = omni->getScaleMode() == FormOmni::FormMachineOmni::TRACKSCALE_LOCAL;
		const uint32_t AV = DKCYAN, HOT = WHITE;
		if (stepMenuSel_ == 0) // MODE 3/4/5
		{
			for (uint8_t i = 0; i < 3; i++)
				strip.setPixelColor(3 + i, i == omni->getScaleMode() ? HOT : AV);
		}
		else if (stepMenuSel_ == 1) // ROOT 3-9 = major-scale notes
		{
			static const uint8_t kMajRoots[7] = {0, 2, 4, 5, 7, 9, 11};
			uint8_t cur = localScale ? omni->getLocalRoot() : (uint8_t)scaleConfig.scaleRoot;
			for (uint8_t i = 0; i < 7; i++)
				strip.setPixelColor(3 + i, kMajRoots[i] == cur ? HOT : AV);
		}
		else if (stepMenuSel_ == 2) // SCALE 3-10 = first 8 patterns
		{
			int cur = localScale ? omni->getLocalPattern() : scaleConfig.scalePattern;
			for (uint8_t i = 0; i < 8; i++)
				strip.setPixelColor(3 + i, (int)i == cur ? HOT : AV);
		}
		else if (stepMenuSel_ == 3) // LOCK 6/7
		{
			strip.setPixelColor(6, !scaleConfig.lockScale ? HOT : AV);
			strip.setPixelColor(7, scaleConfig.lockScale ? HOT : AV);
		}
		else if (stepMenuSel_ == 4) // GROUP 6/7
		{
			strip.setPixelColor(6, !scaleConfig.group16 ? HOT : AV);
			strip.setPixelColor(7, scaleConfig.group16 ? HOT : AV);
		}
		return;
	}

	// Param pages (1-2): the top row is the SELECTED param's value palette — the held
	// step's value lights, or the track default's when nothing is held (mirrors page 0).
	if (stepMenuPage_ == 1 || stepMenuPage_ == 2)
	{
		// Step row: held steps blink white; the rest show content (active blue-white / ghost orange).
		for (uint8_t i = 0; i < 16; i++)
		{
			if (heldStepMask_ & (1 << i))
				strip.setPixelColor(11 + i, blink ? WHITE : LOWWHITE);
			else
				strip.setPixelColor(11 + i, stepRowColor(omni, i));
		}
		for (uint8_t k = 3; k <= 10; k++)
			strip.setPixelColor(k, LEDOFF);

		uint8_t pid = (stepMenuPage_ - 1) * 4 + stepMenuSel_;
		uint8_t mode = pidToPaletteMode(pid);
		if (mode == 255)
			return; // (unreachable: every pid has a palette now)
		int8_t focus = heldStepKey_;
		if (mode == STEPMODE_MFX)
		{
			int16_t sel = focus >= 0 ? omni->stepPaletteSelected(focus, STEPMODE_MFX)
									 : omni->defaultPaletteSelected(STEPMODE_MFX);
			strip.setPixelColor(5, sel == 0 ? colorConfig.selMidiFXGRPOffColor : colorConfig.midiFXGRPOffColor);
			for (uint8_t i = 0; i < NUM_MIDIFX_GROUPS; i++)
				strip.setPixelColor(6 + i, (sel == (int16_t)(i + 1)) ? colorConfig.selMidiFXGRPColor : colorConfig.midiFXGRPColor);
			return;
		}
		if (mode == STEPMODE_MATH)
		{
			uint8_t a = 0, b = 0, kind = focus >= 0 ? omni->stepMathInfo(focus, a, b) : 0;
			strip.setPixelColor(1, kind == 1 ? 0xff8000 : 0x4d2600); // Fill
			strip.setPixelColor(2, kind == 2 ? 0xff0080 : 0x4d0026); // !Fill
			for (uint8_t i = 0; i < 4; i++)
				strip.setPixelColor(3 + i, (kind == 3 && a == i + 1) ? 0x00ff00 : 0x264d00); // ratio A
			for (uint8_t i = 0; i < 4; i++)
				strip.setPixelColor(7 + i, (kind == 3 && b == i + 1) ? 0x00ffff : 0x004c4d); // ratio B
			return;
		}
		uint8_t count = omni->stepPaletteCount(mode);
		int16_t sel = focus >= 0 ? omni->stepPaletteSelected(focus, mode)
								 : omni->defaultPaletteSelected(mode);
		uint32_t col = paletteModeColor(mode);
		uint32_t dim = (col >> 3) & 0x1f1f1f;
		bool isBar = (mode == STEPMODE_VEL || mode == STEPMODE_LENGTH || mode == STEPMODE_CHANCE ||
					  mode == PALMODE_NUDGE || mode == PALMODE_ACCUM);
		for (uint8_t i = 0; i < count; i++)
		{
			uint32_t c;
			if (isBar)
				c = ((int16_t)i == sel) ? ledBrighten(col) : ((int16_t)i < sel ? col : (uint32_t)VLOWWHITE);
			else
				c = ((int16_t)i == sel) ? ledBrighten(col) : dim;
			strip.setPixelColor(1 + i, c);
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
			bool chromatic = omni->scaleIsChromatic();
			for (uint8_t i = 0; i < 10; i++)
			{
				int8_t note = omni->paletteScale()->getNoteByDegree(i, midiSettings.octave);
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
				strip.setPixelColor(1 + i, selected ? ledBrighten(full) : ((full >> 2) & 0x3f3f3f));
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
					c = ((int16_t)i == sel) ? ledBrighten(col) : ((int16_t)i < sel ? col : (uint32_t)VLOWWHITE);
				else
					c = ((int16_t)i == sel) ? ledBrighten(col) : dim;
				strip.setPixelColor(1 + i, c);
			}
		}
		return;
	}

	// Top row 3-10 = the 8 edit modes; active bright, others dim.
	for (uint8_t m = 0; m < STEPMODE_COUNT; m++)
		strip.setPixelColor(3 + m, (m == stepEditMode_) ? kStepModeColors[m] : LOWWHITE);

	// Step row 11-26 = the current page's 16 steps (active = blue-white, ghost = orange).
	int16_t pageStart = (int16_t)omni->activePage() * 16;
	int16_t playhead = (int16_t)omni->playingStepIndex() - pageStart;

	for (uint8_t i = 0; i < 16; i++)
	{
		uint32_t col = stepRowColor(omni, i);
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
	auto omni = getSelectedMachine();

	// Machine menu (page 4, STEPNOTES): let the machine navigate/edit (it reads
	// getEncoderSelect() too), except backing off its first param returns to the CC page
	// and forward off its end crosses into the TRACK SETUP group (SCALE page).
	if (stepMenuPage_ == 4)
	{
		if (getEncoderSelect() && dir < 0 && omni->seqMenuAtStart())
		{
			stepMenuPage_ = 3; // back to the CC page's title cell
			stepMenuSel_ = 6;
			omxDisp.setDirty();
			omxLeds.setDirty();
			return true;
		}
		if (getEncoderSelect() && dir > 0 && omni->seqMenuAtEnd())
		{
			stepMenuPage_ = 5; // SCALE — first page of the TRACK SETUP group
			stepMenuSel_ = 0;
			omxDisp.displayMessage("TRACK SETUP");
			omxDisp.setDirty();
			omxLeds.setDirty();
			return true;
		}
		return false; // forward to the machine
	}

	// CC page (3): 5 slots + bank + title. A held low-row step makes the turn write that
	// step's CC P-Lock (a hold is an edit gesture); otherwise select navigates / edit
	// bumps the live value (or the bank).
	if (stepMenuPage_ == 3)
	{
		if (heldStepMask_ != 0 && stepMenuSel_ < 5)
		{
			uint8_t slot = stepMenuSel_;
			for (uint8_t st = 0; st < 16; st++)
				if (heldStepMask_ & (1 << st))
				{
					int base = omni->getStepPotLock(st, slot);
					if (base < 0)
						base = ccBankRow()[slot];
					omni->setStepPotLock(st, slot, (int8_t)constrain(base + dir, 0, 127));
				}
			stepEdited_ = true;
			omxDisp.setDirty();
			omxLeds.setDirty();
			return true;
		}
		if (!getEncoderSelect())
		{
			if (stepMenuSel_ <= 5)
				editCCPage(stepMenuSel_, dir); // slots 0-4 + bank; the title has no turn edit
			omxDisp.setDirty();
			omxLeds.setDirty();
			return true;
		}
		// Select: walk the 7 cells, then into the machine's STEPNOTES page.
		int s = (int)stepMenuSel_ + dir;
		if (s > 6)
		{
			stepMenuPage_ = 4;
			omni->seqMenuEnter();
		}
		else if (s < 0)
		{
			stepMenuPage_ = 2;
			stepMenuSel_ = 3;
		}
		else
			stepMenuSel_ = (uint8_t)s;
		omxDisp.setDirty();
		omxLeds.setDirty();
		return true;
	}

	// SCALE page (5, TRACK SETUP group): 4 grid cells; back off the start returns to the
	// notes editor (STEP group), forward off the end lands on ACTIONS.
	if (stepMenuPage_ == 5)
	{
		// 5 cells: 0 MODE (per-track scale mode), 1 ROOT, 2 SCALE, 3 LOCK, 4 GROUP.
		if (!getEncoderSelect())
		{
			if (stepMenuSel_ == 0)
				omni->editScaleMode(dir);
			else
				notesEditScaleParam(stepMenuSel_ - 1, dir); // 0 root / 1 scale / 2 lock / 3 group
			omxDisp.setDirty();
			return true;
		}
		int s = (int)stepMenuSel_ + dir;
		if (s > 4)
		{
			stepMenuPage_ = 6;
			stepMenuSel_ = 0;
		}
		else if (s < 0)
		{
			stepMenuPage_ = 4;
			omni->seqMenuEnterEnd();
			omxDisp.displayMessage("STEP");
		}
		else
			stepMenuSel_ = (uint8_t)s;
		omxDisp.setDirty();
		omxLeds.setDirty();
		return true;
	}

	// ACTIONS page (6): QNT / CLR / POTS action cells (click fires them) + NTRY switch.
	if (stepMenuPage_ == 6)
	{
		if (!getEncoderSelect())
		{
			if (stepMenuSel_ == 3) // NTRY: note-entry behavior (Pressed / Toggle)
			{
				bool prev = omxFormGlobal.noteEntryToggle;
				omxFormGlobal.noteEntryToggle = dir > 0;
				if (prev != omxFormGlobal.noteEntryToggle)
					omxDisp.displayMessage(omxFormGlobal.noteEntryToggle ? "TOGGLE" : "PRESSED");
				omxDisp.setDirty();
			}
			return true; // the action cells have no turn edit
		}
		int s = (int)stepMenuSel_ + dir;
		if (s < 0)
		{
			stepMenuPage_ = 5;
			stepMenuSel_ = 4; // SCALE page now has 5 cells (0-4); land on the last (GROUP)
		}
		else
			stepMenuSel_ = (uint8_t)constrain(s, 0, 3);
		omxDisp.setDirty();
		omxLeds.setDirty();
		return true;
	}

	// Holding a step on a custom param page always edits the selected param (a hold is an edit
	// gesture), regardless of select/edit mode.
	if (heldStepMask_ != 0 && (stepMenuPage_ == 1 || stepMenuPage_ == 2))
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

	// Holding step(s) on the OVERVIEW: the encoder edits the current step-edit mode's
	// value on the held steps — it must not navigate off into the param pages mid-hold.
	if (heldStepMask_ != 0 && stepMenuPage_ == 0)
	{
		int delta = enc.accel(1);
		if (delta == 0)
			delta = dir;
		for (uint8_t st = 0; st < 16; st++)
		{
			if (!(heldStepMask_ & (1 << st)))
				continue;
			switch (stepEditMode_)
			{
			case STEPMODE_NOTE: // shift the step's notes by a semitone per click
			{
				int8_t nts[6];
				omni->getStepNotes(st, nts);
				for (uint8_t n = 0; n < 6; n++)
					if (nts[n] >= 0)
						nts[n] = (int8_t)constrain(nts[n] + dir, 0, 127);
				omni->stepSetNotes(st, nts);
				break;
			}
			case STEPMODE_VEL:    omni->editStepParam(st, 0, delta); break;
			case STEPMODE_LENGTH: omni->editStepParam(st, 2, dir); break;
			case STEPMODE_REPEAT: omni->editStepRepeat(st, dir); break;
			case STEPMODE_CHANCE: omni->editStepParam(st, 4, delta); break;
			case STEPMODE_MATH:   omni->editStepParam(st, 5, dir); break;
			case STEPMODE_FUNC:   omni->editStepParam(st, 6, dir); break;
			case STEPMODE_MFX:    omni->editStepParam(st, 3, dir); break;
			}
		}
		stepEdited_ = true;
		stepHoldUIShown_ = true; // an edit engages the hold UI immediately
		omxDisp.setDirty();
		omxLeds.setDirty();
		return true;
	}

	// EDIT mode (encoderSelect off, or AUX held) — no step held:
	if (!getEncoderSelect())
	{
		if (stepMenuPage_ == 0) // overview: change the selected track
		{
			int8_t t = constrain((int)selectedMachine_ + dir, 0, kNumMachines - 1);
			if (t != (int8_t)selectedMachine_)
				selectMachine(t);
		}
		else // custom param page: edit the track default for that param
		{
			uint8_t pid = (stepMenuPage_ - 1) * 4 + stepMenuSel_;
			int delta = enc.accel(1);
			if (delta == 0)
				delta = dir;
			omni->editParamDefault(pid, delta);
		}
		omxDisp.setDirty();
		omxLeds.setDirty();
		return true;
	}

	// SELECT mode: navigate the custom cursor [overview=0, params 1..8], then the CC page.
	int idx = (stepMenuPage_ == 0) ? 0 : (1 + (stepMenuPage_ - 1) * 4 + stepMenuSel_);
	idx += dir;
	if (idx > 8)
	{
		stepMenuPage_ = 3; // CC page (still the STEP group — no popup)
		stepMenuSel_ = 0;
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
	if (stepMenuPage_ == 6) // ACTIONS: Quant / Clear / Pots (+ NTRY, a normal value param)
	{
		if (stepMenuSel_ == 0)
		{
			submenuSetReturn(); // the submenu renders in MI; come back here after
			quantEnterSubmenu();
			return true;
		}
		if (stepMenuSel_ == 1)
		{
			submenuSetReturn();
			miClearSub_ = true;
			clearSel_ = 0;
			omxDisp.setDirty();
			omxLeds.setDirty();
			return true;
		}
		if (stepMenuSel_ == 2)
		{
			openPotConfig();
			return true;
		}
		// sel 3 (NTRY) is a normal value param: fall through to the select/edit toggle.
	}
	if (stepMenuPage_ == 3) // CC page
	{
		// Click while holding step(s) on a slot = clear that slot's P-Lock.
		if (heldStepMask_ != 0 && stepMenuSel_ < 5)
		{
			auto omni = getSelectedMachine();
			for (uint8_t st = 0; st < 16; st++)
				if (heldStepMask_ & (1 << st))
					omni->setStepPotLock(st, stepMenuSel_, -1);
			stepEdited_ = true;
			omxDisp.setDirty();
			omxLeds.setDirty();
			return true;
		}
		if (stepMenuSel_ == 6) // the selectable "CC" title: open the CC-number editor
		{
			openPotConfig();
			return true;
		}
		return false; // otherwise toggle the global select/edit
	}
	if (stepMenuPage_ == 4)
		return false; // machine menu: falls through to toggle the global select/edit
	// Holding a step on a param page: clear that param's P-Lock (a distinct action, not select/edit).
	if (heldStepMask_ != 0 && (stepMenuPage_ == 1 || stepMenuPage_ == 2))
	{
		uint8_t pid = (stepMenuPage_ - 1) * 4 + stepMenuSel_;
		auto omni = getSelectedMachine();
		for (uint8_t s = 0; s < 16; s++)
			if (heldStepMask_ & (1 << s))
				omni->clearStepParamLock(s, pid);
		stepEdited_ = true;
		// No message — the header un-inverting communicates the cleared lock.
		omxDisp.setDirty();
		omxLeds.setDirty();
		return true;
	}
	// Otherwise toggle the unified select/edit state.
	omxFormGlobal.encoderSelect = !omxFormGlobal.encoderSelect;
	omxDisp.setDirty();
	return true;
}

// Render a Step param page: the held step's values (with per-param lock indicators) or the
// built-in defaults when no step is held.
void OmxModeForm::onDisplayStepMenu()
{
	auto omni = getSelectedMachine();
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
	omxDisp.dispStepParams(labels, values, locked, stepMenuSel_, holding || !getEncoderSelect());
}

void OmxModeForm::onDisplayStep()
{
	auto omni = getSelectedMachine();

	// F3 structure layer: rate on top, the active page's length bar on the bottom.
	if (omxFormGlobal.shortcutMode == FORMSHORTCUT_F3)
	{
		dispF3RateLength(omni, omni->getPageLen(omni->activePage()));
		return;
	}
	// Holding F1/F2 always shows the track page (with the copy/track overlay), from any menu
	// page — so the whole seq view is consistent while a modifier is held.
	if (omxFormGlobal.shortcutMode == FORMSHORTCUT_F1 || omxFormGlobal.shortcutMode == FORMSHORTCUT_F2)
	{
		onDisplaySeqTrackPage();
		return;
	}

	// CC page (3): shared renderer; held low-row steps show/edit that step's P-Locks.
	if (stepMenuPage_ == 3)
	{
		onDisplayCCPage(stepMenuSel_, heldStepMask_, heldStepKey_);
		return;
	}

	// Machine menu (page 4): the STEPNOTES editor, machine-rendered.
	if (stepMenuPage_ == 4)
	{
		omni->onDisplayUpdate();
		return;
	}

	// SCALE page (5, TRACK SETUP group): 5-cell Mode / Root / Scale / Lock / Group.
	if (stepMenuPage_ == 5)
	{
		dispScalePage5(stepMenuSel_, !getEncoderSelect());
		return;
	}

	// ACTIONS page (6): Quant / Clear / Pots / Note entry — QNT/CLR/POTS click to fire
	// (@ = submenu, µ = destructive); NTRY is a value param (Pressed/Toggle) — its value box
	// inverts while editing it, like the SCALE page, so edit mode reads clearly.
	if (stepMenuPage_ == 6)
	{
		const char *labels[4] = {"QNT", "CLR", "POTS", "NTRY"};
		const char *values[4] = {"@", "µ", "@", omxFormGlobal.noteEntryToggle ? "TG" : "PR"};
		bool locked[4] = {false, false, false, false};
		bool editing = (stepMenuSel_ == 3 && !getEncoderSelect()); // only NTRY edits
		omxDisp.dispStepParams(labels, values, locked, stepMenuSel_, editing);
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
		fillStepStates(omni, stepState);
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
void OmxModeForm::onDisplaySeqTrackPage(bool keyboardMode)
{
	auto omni = getSelectedMachine();
	int16_t pageStart = (int16_t)omni->activePage() * 16;
	int8_t playhead = omxFormGlobal.isPlaying ? (int8_t)((int16_t)omni->playingStepIndex() - pageStart) : -1;

	// Step states: 0 empty · 1 notes · 2 ghost · 3 muted ghost · 4 muted notes.
	uint8_t stepState[16];
	fillStepStates(omni, stepState);

	// Track mute states, with solo override (any soloed -> non-soloed render muted).
	bool anySolo = false;
	for (uint8_t t = 0; t < kNumMachines; t++)
		if (machines_[t]->getSolo()) { anySolo = true; break; }
	bool trackMuted[kNumMachines];
	for (uint8_t t = 0; t < kNumMachines; t++)
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
		overlayLabel = (heldTrackKey_ >= 0) ? "MUTE / PLAY MODE" : "PASTE / CUT";
	}
	else if (formView_ == FORMVIEW_MIX && heldTrackKey_ >= 0)
	{
		// Mix hold-track: same box + label as Seq's F2 + Track. Key 18 arms the track
		// copy; while armed the label asks for the destination.
		modOverlay = 2;
		overlayLabel = (mixCopyMode_ == 1) ? "COPY PAT TO?"
		               : (mixCopyMode_ == 2) ? "COPY ALL TO?" : "MUTE / PLAY MODE";
	}
	else if (!getEncoderSelect() && heldStepMask_ == 0)
	{
		// Overview in EDIT mode (encoder clicked to edit, or AUX held): box the "TRK n" name to show
		// the encoder now selects the track.
		modOverlay = 2;
		overlayLabel = nullptr;
	}

	// View tag (the page-1 view selector). While editing, it shows the browsed pendingView_ and
	// boxes/inverts; otherwise it names the current view. MIX = "MIX", STEP = "SEQ".
	if (keyboardMode)
	{
		// MI keyboard view: no F1/F2 overlays (keys 1/2 are notes here, not modifiers). In edit mode
		// (encoder clicked on page 0), box the "TRK n" name to show the encoder now selects the track.
		modOverlay = (formView_ == FORMVIEW_MI && !getEncoderSelect() && miCursor_ == 0) ? 2 : 0;
		overlayLabel = nullptr;
	}

	static const char *kViewTags[FORMVIEW_COUNT] = {"MIX", "SEQ", "TRSP", "NOTE", "PTRN", "MI", "TOOL"};
	const char *viewLabel = kViewTags[formView_]; // live switch: the tag is always the current view
	bool viewLabelSel = viewEditActive();          // boxed while the selector is live
	uint8_t transport = omxFormGlobal.recArm ? 2 : (omxFormGlobal.isPlaying ? 1 : 0);
	omxDisp.dispSeqTrackPage(title, trackMuted, selectedMachine_, rateStr,
							 mixPlayModeIndex(omni->trackPtr()), (uint16_t)clockConfig.clockbpm,
							 omni->getEnabledPages(), omni->activePage(), stepState, playhead,
							 modOverlay, overlayLabel, omni->getPageLen(omni->activePage()), transport,
							 viewLabel, viewLabelSel, !keyboardMode, ccMeterActive(), kNumMachines);
}

// Mix view key routing (from the shell's key dispatch): true = a Mix handler took the
// event; false = fall through to the machine (F3 + track keys = rate / page length).
bool OmxModeForm::onKeyUpdateMixRoute(OMXKeypadEvent e)
{
	uint8_t thisKey = e.key();

	// Track keys 3-10 (except under F3, which the machine handles as rate).
	if (thisKey >= 3 && thisKey < 11 && omxFormGlobal.shortcutMode != FORMSHORTCUT_F3)
	{
		onKeyUpdateMix(e);
		return true;
	}
	// A low-row step that began an audition must finish it on release even if a track key
	// went down meanwhile — otherwise the release routes to onKeyUpdateMixHold below and
	// leaves the mask bit set with the audition note still ringing.
	if (!e.down() && thisKey >= 11 && thisKey < 27 && (mixHeldStepMask_ & (1 << (thisKey - 11))))
	{
		onKeyUpdateMixStep(e);
		return true;
	}
	// Low-row per-track controls while a track is held.
	if (heldTrackKey_ >= 0 && thisKey >= 11 && thisKey < 27)
	{
		onKeyUpdateMixHold(e);
		return true;
	}
	// Low-row taps (no track held, no modifier) audition the selected track's steps.
	if (heldTrackKey_ < 0 && thisKey >= 11 && thisKey < 27 && omxFormGlobal.shortcutMode == FORMSHORTCUT_NONE)
	{
		onKeyUpdateMixStep(e);
		return true;
	}
	// F1/F2 + low row = step mute/solo on the selected track (not the machine's copy/cut).
	if (thisKey >= 11 && thisKey < 27 &&
		(omxFormGlobal.shortcutMode == FORMSHORTCUT_F1 || omxFormGlobal.shortcutMode == FORMSHORTCUT_F2))
	{
		onKeyUpdateMixStepMute(e);
		return true;
	}
	return false;
}

// Mix view — track keys (3-10): F1+tap = mute, F2+tap = solo, single tap = select
// (+ hold for the low-row per-track controls). (Low-row keys go to the step editor.)
void OmxModeForm::onKeyUpdateMix(OMXKeypadEvent e)
{
	uint8_t thisKey = e.key();
	if (thisKey < 3 || thisKey >= 3 + kNumMachines) // track keys past the count are inert
		return;
	uint8_t track = thisKey - 3;

	// Release: clear the held-track marker (used by K5 hue) — and cancel an armed copy.
	if (!e.down())
	{
		if (heldTrackKey_ == track)
		{
			heldTrackKey_ = -1;
			mixCopyMode_ = 0;
		}
		return;
	}

	if (omxFormGlobal.shortcutMode == FORMSHORTCUT_F1) // Mute — toggles the tapped track only,
	{                                                  // without changing the selected track.
		if (!e.held())
		{
			auto m = machines_[track];
			m->setMute(!m->getMute());
			omxDisp.setDirty(); // the MUTE page's track squares show the state; no popup
			omxLeds.setDirty();
		}
		return;
	}
	if (omxFormGlobal.shortcutMode == FORMSHORTCUT_F2) // Solo — same: no track switch, no popup
	{                                                  // (the on-screen SOLO split shows state).
		if (!e.held())
		{
			auto m = machines_[track];
			m->setSolo(!m->getSolo());
			omxDisp.setDirty();
			omxLeds.setDirty();
		}
		return;
	}

	// Armed track copy (hold track + key 18): tap a destination track key to copy onto it.
	// Mode 1 = pattern only (steps/pages/play mode/step defaults), mode 2 = everything
	// (settings + colour too). Destructive on the destination, like the tools. Stays armed
	// for more destinations; releasing the held track cancels. Unarmed taps are ignored —
	// a second track key while holding one must never copy by accident.
	if (heldTrackKey_ >= 0 && heldTrackKey_ != (int8_t)track)
	{
		if (!e.held() && mixCopyMode_ != 0)
		{
			if (mixCopyMode_ == 2)
			{
				// setSeq carries the whole OmniSeq — including the per-track scale (v9).
				machines_[track]->setSeq(machines_[heldTrackKey_]->getSeq());
				trackHue_[track] = trackHue_[heldTrackKey_];
			}
			else
				machines_[track]->setTrackData(machines_[heldTrackKey_]->getSeq().tracks[0]);
			omxDisp.displayMessage("TRK " + String(heldTrackKey_ + 1) + " > " + String(track + 1));
			omxLeds.setDirty();
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
// 11 Mute · 12 Solo · 13-17 play mode (fwd/rev/fwd-pong/rev-pong/random) · 19-26 colour presets.
void OmxModeForm::onKeyUpdateMixHold(OMXKeypadEvent e)
{
	if (e.held() || !e.down())
		return;
	uint8_t k = e.key();
	auto omni = machines_[heldTrackKey_];
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
	else if (k == 18 && formView_ == FORMVIEW_MIX) // arm track copy: 1st press = pattern only, 2nd = everything (toggles)
	{
		mixCopyMode_ = (mixCopyMode_ == 1) ? 2 : 1;
		omxDisp.displayMessage(mixCopyMode_ == 1 ? "COPY PAT TO?" : "COPY ALL TO?");
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
	auto omni = getSelectedMachine();
	if (e.down())
	{
		mixHeldStepMask_ |= (1 << key16);
		mixHeldStepKey_ = (int8_t)key16;
	}
	else
	{
		mixHeldStepMask_ &= ~(1 << key16);
		if (mixHeldStepKey_ == (int8_t)key16)
		{
			mixHeldStepKey_ = -1;
			for (int8_t st = 15; st >= 0; st--)
				if (mixHeldStepMask_ & (1 << st)) { mixHeldStepKey_ = st; break; }
		}
	}
	omni->auditionStep(key16, e.down());
	omxDisp.setDirty();
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
	auto omni = getSelectedMachine();
	omni->toggleStepMute(key16);
	omxDisp.setDirty(); // the MUTE page's step glyphs show the state; no popup
	omxLeds.setDirty();
}

void OmxModeForm::updateMixHoldLEDs()
{
	auto omni = machines_[heldTrackKey_];
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

	// 18: track copy arm — dim until pressed; PAT mode magenta, ALL mode white.
	strip.setPixelColor(18, mixCopyMode_ == 0 ? (uint32_t)DKMAGENTA
	                        : mixCopyMode_ == 1 ? (uint32_t)MAGENTA : (uint32_t)WHITE);

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
	// The armed track copy only lives while its source track is held (in Mix).
	if (heldTrackKey_ < 0 || formView_ != FORMVIEW_MIX)
		mixCopyMode_ = 0;

	if (omxFormGlobal.auxBlock && midiSettings.keyState[0] == false)
	{
		omxFormGlobal.auxBlock = false;
		omxDisp.setDirty();
		omxLeds.setDirty();
	}

	// Step view: while a step is held the top row 1-10 is the value palette, so keys 1/2
	// must not become the F1/F2 shortcut. Freeze the shortcut mode at NONE.
	if ((formView_ == FORMVIEW_STEP || formView_ == FORMVIEW_TOOLS) && heldStepMask_ != 0)
	{
		omxFormGlobal.shortcutMode = FORMSHORTCUT_NONE;
		return;
	}
	// Notes view: same during a param-palette hold (11/12/13 held without an F-key) — keys 1-10
	// are the value palette, so keys 1/2 must not flip to F1/F2.
	if (formView_ == FORMVIEW_NOTES && notesPaletteEngaged_)
	{
		omxFormGlobal.shortcutMode = FORMSHORTCUT_NONE;
		return;
	}

	uint8_t prevMode = omxFormGlobal.shortcutMode;

	// Keys 1/2 pressed as AUX transport (swallow mask set) are the AUX layer's until they
	// release — they must not flip a phantom F1/F2/F3 on when AUX lifts before they do.
	// MI view: the whole keybed is the playable keyboard — keys 1/2 are NOTES there, never
	// F1/F2/F3 (the AUX layer below is unaffected).
	bool fkeys = (formView_ != FORMVIEW_MI);
	bool k1Held = fkeys && midiSettings.keyState[1] && !(auxSwallowMask_ & (1u << 1));
	bool k2Held = fkeys && midiSettings.keyState[2] && !(auxSwallowMask_ & (1u << 2));

	if (omxFormGlobal.shortcutMode != FORMSHORTCUT_AUX && k1Held && k2Held)
	{
		omxFormGlobal.shortcutMode = FORMSHORTCUT_F3;
	}
	else if (omxFormGlobal.shortcutMode != FORMSHORTCUT_AUX && k1Held)
	{
		omxFormGlobal.shortcutMode = FORMSHORTCUT_F1;
	}
	else if (omxFormGlobal.shortcutMode != FORMSHORTCUT_AUX && k2Held)
	{
		omxFormGlobal.shortcutMode = FORMSHORTCUT_F2;
	}
	else if (midiSettings.keyState[0] && midiSettings.midiAUX)
	{
		// Gate on midiAUX (set only by a real AUX press event) so an AUX press that was
		// swallowed by an F-key hold can't flip the shortcut layer on when the F-key lifts,
		// with the AUX LEDs / view-commit logic still disarmed.
		omxFormGlobal.shortcutMode = FORMSHORTCUT_AUX;
	}
	else
	{
		omxFormGlobal.shortcutMode = FORMSHORTCUT_NONE;
	}

	if (prevMode != omxFormGlobal.shortcutMode)
	{
		omxFormGlobal.shortcutPaste = false; // Transpose/machine F1-F2 copy-cut/paste toggle resets
		// Releasing F2 ends the hold: the next F2 press starts a fresh initial grab.
		// (A fresh F1 copy re-loads the buffer — F1 release doesn't reset it.)
		if (prevMode == FORMSHORTCUT_F2)
		{
			seqF2Loaded_ = false;
			seqF2Holding_ = false;
		}


		// Mix: holding F2 activates FILL on all tracks (steps with a Fill condition play).
		bool fillOn = (formView_ == FORMVIEW_MIX && omxFormGlobal.shortcutMode == FORMSHORTCUT_F2);
		for (uint8_t i = 0; i < kNumMachines; i++)
			machines_[i]->setFill(fillOn);

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

	// A previous session can't leave a modal submenu or record state armed behind:
	// re-entering FORM with a stale QUANTIZE snapshot (quantOrigNudges_) or a half-open
	// CLEAR confirm would act on data that has since changed.
	miQuantSub_ = false;
	miClearSub_ = false;
	clearReturnView_ = -1;
	recHeldCount_ = 0;
	recClearedMask_ = 0;
	seqF2Loaded_ = false; // start each FORM session with an unloaded F2 buffer
	seqF2Holding_ = false;

	// Serial.println("AuxMacroActivated");
	auxMacroManager_.onModeActivated();
	// FORM keeps the pots on the track's CC bank by default — a selected macro shouldn't steal
	// them unless the user opts in on the MI MIDI page (MPOT).
	auxMacroManager_.setMacrosConsumePots(omxFormGlobal.macroConsumesPots);
	// Serial.println("onModeActivated complete");


	// activeDrumKit.CopyFrom(drumKits[selDrumKit]);

	// selectMidiFx(mfxIndex_, false);
}

void OmxModeForm::onModeDeactivated()
{
	// Release any manual preview notes still sounding before the mode goes away.
	for (uint8_t k = 1; k < 27; k++)
		previewKeyOff(k);
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
}

void OmxModeForm::onPotChanged(int potIndex, int prevValue, int newValue, int analogDelta)
{
	lastPotMs_ = millis(); // show the transient CC meter for a moment

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

	// Mix view: hold a low-row step + turn a pot = CC P-Lock, same gesture as the Step view
	// (the CC page shows the lock dots; auditioning and locking compose naturally).
	if (formView_ == FORMVIEW_MIX && mixHeldStepMask_ != 0 && potIndex >= 0 && potIndex < 5)
	{
		auto omni = getSelectedMachine();
		int8_t v = (int8_t)constrain(newValue, 0, 127);
		for (uint8_t st = 0; st < 16; st++)
			if (mixHeldStepMask_ & (1 << st))
				omni->setStepPotLock(st, (uint8_t)potIndex, v);
		omxDisp.setDirty();
		omxLeds.setDirty();
		return;
	}

	// Step view: hold a step + turn a pot = P-Lock — lock that pot slot's CC to the pot value on
	// every held step. Sent when the step fires (see triggerStep). Directly maps 0-127 (no pickup).
	if (formView_ == FORMVIEW_STEP && heldStepMask_ != 0 && potIndex >= 0 && potIndex < 5)
	{
		auto omni = getSelectedMachine();
		int8_t v = (int8_t)constrain(newValue, 0, 127);
		for (uint8_t s = 0; s < 16; s++)
			if (heldStepMask_ & (1 << s))
				omni->setStepPotLock(s, (uint8_t)potIndex, v);
		// No popup — the step's P-Lock tint conveys the lock (and pot drift while a step is
		// merely pressed shouldn't flash a "CC" message).
		stepEdited_ = true; // a P-Lock edit suppresses the quick-click clear on release
		omxDisp.setDirty();
		omxLeds.setDirty();
		return;
	}

	auto selMachine = getSelectedMachine();

	if(selMachine->doesConsumePots())
	{
		if (potIndex >= 0 && potIndex < 5)
		{
			ccBankRow()[potIndex] = (uint8_t)constrain(newValue, 0, 127); // Mix/MI CC page mirror
			omxDisp.setDirty(); // redraw the CC bar now, independent of the pot-meter timer / sendMidi
		}
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
	// Self-heal the AUX swallow mask: a bit must never outlive its key being physically
	// held. If any consumer ate the release event before onKeyUpdate's clear (e.g. a
	// macro's early-return), the stale bit would report keys 1/2 as "swallowed" forever
	// and silently disable the F1/F2/F3 layers. This runs AFTER event dispatch (keypad
	// events are handled synchronously in tick(), before loopUpdate), so it can never
	// steal the bit from a release that onKeyUpdate is still about to route.
	if (auxSwallowMask_ != 0)
	{
		for (uint8_t k = 1; k < 27; k++)
			if ((auxSwallowMask_ & (1u << k)) && !midiSettings.keyState[k])
				auxSwallowMask_ &= ~(1u << k);
	}

	// While the REC FULL flash window is live (plus a beat after), keep the LEDs repainting
	// so the red blink both appears and CLEARS even when nothing else dirties them.
	if (recFullFlashMs_ != 0 && (uint32_t)(millis() - recFullFlashMs_) < 300)
		omxLeds.setDirty();
	// Same for the undo-key flash (~2s after a destructive action).
	if (undoFlashMs_ != 0 && (uint32_t)(millis() - undoFlashMs_) < 2200)
		omxLeds.setDirty();

	// Solo/mute audibility: keep anySolo current and flush notes on any track that just
	// became inaudible, so muting or soloing can never leave notes ringing (stuck).
	bool anySolo = false;
	for (uint8_t i = 0; i < kNumMachines; i++)
		if (machines_[i]->getSolo()) { anySolo = true; break; }
	omxFormGlobal.anySolo = anySolo;
	for (uint8_t i = 0; i < kNumMachines; i++)
	{
		bool aud = machines_[i]->isAudible();
		if (!aud && trackAudible_[i])
			machines_[i]->flushNotes();
		trackAudible_[i] = aud;
	}

	// BPM tool: keep the display live briefly after a tap so the TAP button shows pressed
	// and then releases (the flash renders in onDisplayTools).
	if (bpmTapFlashMs_ != 0)
	{
		if ((millis() - bpmTapFlashMs_) < 120)
			omxDisp.setDirty();
		else
		{
			bpmTapFlashMs_ = 0;
			omxDisp.setDirty(); // final repaint to release the button
		}
	}

	// Keep repainting while the transient CC meter is up (and once as it expires) so it clears.
	bool ccActive = ccMeterActive();
	if (ccActive || ccMeterWasActive_)
		omxDisp.setDirty();
	ccMeterWasActive_ = ccActive;

	// Engage the hold-step UI once the hold passes a short delay (prevents quick-click flashes).
	if (heldStepMask_ != 0 && !stepHoldUIShown_ && (millis() - stepHoldStartMs_) >= 150)
	{
		stepHoldUIShown_ = true;
		omxDisp.setDirty();
	}
	// Same delay for every Notes-view hold popup (palette + F1/F2/F3 menus) so a quick tap of a
	// modal key doesn't flash it.
	if (formView_ == FORMVIEW_NOTES && notesModalHeld_ && !notesHoldUIShown_ && (millis() - notesHoldStartMs_) >= 150)
	{
		notesHoldUIShown_ = true;
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
		auto selOmni = getSelectedMachine();
		int16_t curStep = (int16_t)selOmni->playingStepIndex();
		bool loopWrapped = false;
		if (curStep != lastPlayheadStep_)
		{
			loopWrapped = (curStep < lastPlayheadStep_); // selected track's loop end
			if (loopWrapped)
				recClearedMask_ = 0; // new record pass (replace mode)
			lastPlayheadStep_ = curStep;
			omxLeds.setDirty();
			omxDisp.setDirty();
		}

		// Pattern switch styles: commit a queued switch at the boundary the style asks for.
		bool barBoundary = (seqConfig.currentClockTick < lastBarTick_); // currentClockTick wrapped (1 bar)
		lastBarTick_ = seqConfig.currentClockTick;
		if (queuedPattern_ >= 0 &&
			((switchStyle_ == 1 && barBoundary) || (switchStyle_ == 0 && loopWrapped)))
		{
			switchPattern((uint8_t)queuedPattern_);
			queuedPattern_ = -1;
			omxDisp.setDirty();
			omxLeds.setDirty();
		}
		// Chained: advance to the next pattern in the chain at each loop end.
		if (switchStyle_ == 3 && chainLen_ > 1 && loopWrapped)
		{
			chainPos_ = (chainPos_ + 1) % chainLen_;
			switchPattern(chain_[chainPos_]);
			omxDisp.setDirty();
			omxLeds.setDirty();
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

	// F3 + encoder = BPM from ANY view (P1). F3 is otherwise unused with the encoder, and
	// AUX+turn must stay "edit the selected cell". Tap tempo stays in the BPM tool.
	if (omxFormGlobal.shortcutMode == FORMSHORTCUT_F3)
	{
		editBpm(enc.accel(5));
		omxDisp.displayMessage("BPM " + String((int)clockConfig.clockbpm));
		omxDisp.setDirty();
		return;
	}

	// F1 + encoder = change the ACTIVE page (Mix/Step/Notes/Tools): the F1 page layer's
	// encoder counterpart. Replaces the old Mix behavior (track select — redundant, the
	// dedicated track keys already do that).
	if (omxFormGlobal.shortcutMode == FORMSHORTCUT_F1 &&
		(formView_ == FORMVIEW_MIX || formView_ == FORMVIEW_STEP ||
		 formView_ == FORMVIEW_NOTES || formView_ == FORMVIEW_TOOLS))
	{
		auto omni = getSelectedMachine();
		int p = constrain((int)omni->activePage() + enc.dir(), 0, 3);
		omni->setActivePage((uint8_t)p);
		pagePopupMs_ = millis(); // show the large page icons instead of a text popup
		stepF1Used_ = true; // the hold was "used": releasing F1 must not fire a quick-tap
		notesF1Used_ = true;
		omxDisp.setDirty();
		omxLeds.setDirty();
		return;
	}

	switch (formView_)
	{
	case FORMVIEW_NOTES: // pages (select) / values (edit)
		onEncoderNotes(enc.dir());
		return;
	case FORMVIEW_MI: // scale / track-settings menu
		onEncoderMI(enc.dir());
		return;
	case FORMVIEW_MIX: // its own pages (overview / LEVELS / CC / TRACK / menu)
		onEncoderMix(enc.dir());
		return;
	case FORMVIEW_TOOLS:
		onEncoderTools(enc.dir());
		return;
	case FORMVIEW_PATTERNS:
		// No encoder job yet — swallow it rather than letting it fall through to
		// the machine and walk its param menu blind (invisible edits).
		return;
	case FORMVIEW_STEP:
		if (onEncoderStep(enc))
			return;
		break; // machine menu page: the machine navigates/edits below
	case FORMVIEW_TRANSPOSE:
		if (onEncoderTranspose(enc.dir()))
			return;
		break; // the pattern editor (machine) handles it below
	}

	getSelectedMachine()->onEncoderChanged(enc);
}

// Transpose view: past the pattern editor's end lies the live-transpose params page
// (TPOS / TYPE / TPAT-apply — the SEQTPOSE grid, also in the Mix menu). Returns true
// when handled; false lets the machine's pattern editor take the turn.
bool OmxModeForm::onEncoderTranspose(int dir)
{
	auto omni = getSelectedMachine();
	if (transParamsPage_)
	{
		if (getEncoderSelect())
		{
			if (dir < 0 && transSel_ == 0)
				transParamsPage_ = false; // back into the pattern editor
			else
				transSel_ = (uint8_t)constrain((int)transSel_ + dir, 0, 2);
		}
		else
			omni->transParamsEdit(transSel_, dir);
		omxDisp.setDirty();
		return true;
	}
	if (getEncoderSelect() && dir > 0 && omni->transMenuAtEnd())
	{
		transParamsPage_ = true;
		transSel_ = 0;
		omxDisp.displayMessage("TPOSE PARAMS");
		omxDisp.setDirty();
		return true;
	}
	return false;
}

// Mix encoder pages (flat cursor, like MI/Notes; click toggles select/edit):
//   0     = the track overview (edit-turn selects the track)
//   1-8   = LEVELS: per-track default-velocity mixer (edit-turn adjusts that track;
//           the change pushes to every step without its own velocity lock)
//   9-14  = CC: the selected track's 5 pot-bank CC slots as bars (9-13) plus the big
//           bank number (14). Edit-turn on a slot sends the CC live — or, while a
//           low-row step is held, writes that step's CC P-Lock (lock dots mark locked
//           slots; encoder click clears them). Editing the bank number switches the
//           track's pot bank — the knobs and P-Locks follow it.
//   15    = the selectable "CC" title (click = open the CC-number editor for the bank)
//   16-19 = TRACK: Mute / Solo / Gate / Rate for the selected track
//   20    = the machine's track/global param menu (Length/MFX, modes, transpose,
//           MIDI, timings, scale) — moved here from the Seq view (Mix = track level)
// Shared CC-page edit (Mix + MI). cell 0-4 = a pot-bank CC slot: bump the live value and send it
// on the track's channel. cell 5 = the track's pot bank. (Mix's held-step P-Lock path is separate.)
void OmxModeForm::editCCPage(uint8_t cell, int dir)
{
	auto omni = getSelectedMachine();
	if (cell <= 4)
	{
		int v = constrain((int)ccBankRow()[cell] + dir, 0, 127);
		ccBankRow()[cell] = (uint8_t)v;
		omni->sendPotCC(cell, (uint8_t)v);
	}
	else // cell 5: the big bank number
	{
		omni->setPotBank((uint8_t)constrain((int)omni->getPotBank() + dir, 0, NUM_CC_BANKS - 1));
	}
}

// Open the shared pot-config submode (edits the global pots[][] CC-number map). Seed its bank
// from the selected track's pot bank so it opens on the bank actually in use. The submode owns
// its own keys/encoder/display and returns to this view on exit (AUX or its Exit page).
void OmxModeForm::openPotConfig()
{
	potSettings.potbank = getSelectedMachine()->getPotBank();
	auxMacroManager_.enableSubmode(&omxUtil.subModePotConfig);
	omxDisp.setDirty();
	omxLeds.setDirty();
}

// Display state of the active page's 16 steps: 0 empty, 1 has-notes, 2 on, 3 on+muted,
// 4 has-notes+muted. Shared by the Seq/Notes/Mix/MI step-row renderers.
void OmxModeForm::fillStepStates(FormOmni::FormMachineOmni *omni, uint8_t out[16])
{
	for (uint8_t i = 0; i < 16; i++)
	{
		bool m = omni->getStepMute(i);
		out[i] = omni->stepHasNotes(i) ? (m ? 4 : 1) : (omni->stepIsOn(i) ? (m ? 3 : 2) : 0);
	}
}

uint32_t OmxModeForm::trackHueColor(uint8_t idx)
{
	return strip.gamma32(strip.ColorHSV((uint16_t)trackHue_[idx] << 8, 255, 255));
}

// The F3 "rate | length" screen: rate as "1:<divisor>" over a length bar of activeCount steps.
void OmxModeForm::dispF3RateLength(FormOmni::FormMachineOmni *omni, uint8_t activeCount)
{
	char rbuf[12];
	snprintf(rbuf, sizeof(rbuf), "1:%u", (unsigned)kSeqRates[omni->getSeq().rate]);
	omxDisp.dispTrackLength(rbuf, activeCount);
}

bool OmxModeForm::onEncoderMix(int dir)
{
	if (dir == 0)
		return true;
	// In the track/global param menu (last cursor): the machine navigates/edits its own
	// pages; backing off the first page returns to the TRACK grid.
	if (mixCursor_ == kMixMenu)
	{
		auto omni = getSelectedMachine();
		if (getEncoderSelect() && dir < 0 && omni->mixMenuAtStart())
		{
			mixCursor_ = kMixMenu - 1; // back to the TRACK grid's last cell (RATE)
			omxDisp.setDirty();
			return true;
		}
		omni->onEncoderChanged(Encoder::makeUpdate(dir, 0));
		return true;
	}
	if (getEncoderSelect())
	{
		uint8_t prev = mixCursor_;
		mixCursor_ = (uint8_t)constrain((int)mixCursor_ + dir, 0, kMixMenu);
		if (mixCursor_ == kMixMenu && prev != kMixMenu)
			getSelectedMachine()->mixMenuEnter();
		// Group messages (menu map): MIX = overview + LEVELS, TRACK = grid + menu.
		if (prev < kMixTrack && mixCursor_ >= kMixTrack)
			omxDisp.displayMessage("TRACK");
		else if (prev >= kMixTrack && mixCursor_ < kMixTrack)
			omxDisp.displayMessage("MIX");
		omxDisp.setDirty();
		return true;
	}
	// Edit mode:
	if (mixCursor_ == 0)
	{
		if (heldTrackKey_ < 0)
		{
			int t = constrain((int)selectedMachine_ + dir, 0, kNumMachines - 1);
			if (t != (int)selectedMachine_)
				selectMachine((uint8_t)t);
		}
	}
	else if (mixCursor_ <= kNumMachines) // LEVELS: edits the bar under the cursor, not the selection
	{
		machines_[mixCursor_ - 1]->editParamDefault(0, dir);
	}
	else if (mixCursor_ >= kMixTrack) // TRACK grid
	{
		auto omni = getSelectedMachine();
		switch (mixCursor_ - kMixTrack)
		{
		case 0: omni->setMute(!(dir < 0)); break;  // turn right = mute, left = unmute
		case 1: omni->setSolo(!(dir < 0)); break;
		case 2: omni->editGate(dir); break;
		case 3:
			omni->editRate(dir);
			// §4 label rule: the cell shows the bare divisor; the full form pops while turning.
			omxDisp.displayMessage("RATE 1:" + String(kSeqRates[omni->getRate()]));
			break;
		}
	}
	omxDisp.setDirty();
	omxLeds.setDirty();
	return true;
}

// Shared CC-page renderer (Seq page 3 + MI): 5 slots + bank + selectable title.
// heldMask/heldKey: held low-row steps (that step's P-Locks show instead of live values).
void OmxModeForm::onDisplayCCPage(uint8_t sel, uint16_t heldMask, int8_t heldKey)
{
	auto omni = getSelectedMachine();
	bool held = (heldMask != 0);
	int8_t vals[5];
	bool locked[5];
	for (uint8_t i = 0; i < 5; i++)
	{
		int8_t lockVal = held && heldKey >= 0 ? omni->getStepPotLock(heldKey, i) : (int8_t)-1;
		locked[i] = lockVal >= 0;
		// Holding a step shows THAT step's locks (no bar = unlocked slot);
		// otherwise the live last-sent values.
		vals[i] = held ? lockVal : (int8_t)ccBankRow()[i];
	}
	char tbuf[12], vbuf[14];
	snprintf(tbuf, sizeof(tbuf), held ? "CC LOCK" : "CC");
	if (sel < 5)
	{
		int v = held ? vals[sel] : (int)ccBankRow()[sel];
		if (v < 0)
			snprintf(vbuf, sizeof(vbuf), "C%u --", (unsigned)omni->potLockCC(sel));
		else
			snprintf(vbuf, sizeof(vbuf), "C%u %d", (unsigned)omni->potLockCC(sel), v);
	}
	else if (sel == 5)
		snprintf(vbuf, sizeof(vbuf), "BANK %u", (unsigned)(omni->getPotBank() + 1));
	else
		snprintf(vbuf, sizeof(vbuf), "EDIT"); // title selected: click opens the CC editor
	omxDisp.dispMixLevels(tbuf, vbuf, vals, 5, sel, !getEncoderSelect(),
						  held ? locked : nullptr, (int8_t)(omni->getPotBank() + 1));
}

// Render the Mix view's encoder pages.
void OmxModeForm::onDisplayMix()
{
	if (mixCursor_ >= 1 && mixCursor_ <= kNumMachines) // LEVELS
	{
		int8_t vals[kNumMachines];
		for (uint8_t i = 0; i < kNumMachines; i++)
			vals[i] = (int8_t)machines_[i]->trackPtr()->paramDefaults[0];
		uint8_t sel = mixCursor_ - 1;
		char vbuf[12];
		snprintf(vbuf, sizeof(vbuf), "T%u %u", (unsigned)(sel + 1), (unsigned)vals[sel]);
		omxDisp.dispMixLevels("LEVELS", vbuf, vals, kNumMachines, sel, !getEncoderSelect());
		return;
	}
	if (mixCursor_ == kMixMenu) // track/global param menu: the machine renders its pages
	{
		getSelectedMachine()->onDisplayUpdate();
		return;
	}
	if (mixCursor_ >= kMixTrack) // TRACK: Mute / Solo / Gate / Rate (selected track)
	{
		auto omni = getSelectedMachine();
		const char *labels[4] = {"MUTE", "SOLO", "GATE", "RATE"};
		String vals[4];
		vals[0] = omni->getMute() ? "Ĉ" : "Ć";
		vals[1] = omni->getSolo() ? "Ĉ" : "Ć";
		vals[2] = omni->gateBox();
		vals[3] = String(kSeqRates[omni->getRate()]); // full "1:n" pops while turning
		const char *values[4] = {vals[0].c_str(), vals[1].c_str(), vals[2].c_str(), vals[3].c_str()};
		bool locked[4] = {false, false, false, false};
		omxDisp.dispStepParams(labels, values, locked, mixCursor_ - kMixTrack, !getEncoderSelect());
		return;
	}
	onDisplaySeqTrackPage(); // cursor 0: the shared track/page overview
}

void OmxModeForm::onEncoderButtonDown()
{
	if (auxMacroManager_.onEncoderButtonDown())
		return;

	switch (formView_)
	{
	case FORMVIEW_NOTES:
		if (onEncoderButtonNotes())
			return;
		break;
	case FORMVIEW_MI:
		if (onEncoderButtonMI())
			return;
		break;
	case FORMVIEW_TOOLS: // click on an action-button cell fires the action
		if (onEncoderButtonTools())
			return;
		break;
	case FORMVIEW_STEP:
		if (onEncoderButtonStep())
			return;
		break;
	case FORMVIEW_MIX:
		break;
	default:
		break;
	}

	auto selMachine = getSelectedMachine();
	selMachine->onEncoderButtonDown();
	int8_t action = selMachine->takeActionRequest(); // ACTIONS page in the machine menu (Mix)
	if (action == 0)
	{
		submenuSetReturn(); // the quant submenu renders in MI; return here after
		quantEnterSubmenu();
		return;
	}
	if (action == 1)
	{
		submenuSetReturn();
		miClearSub_ = true;
		clearSel_ = 0;
		omxDisp.setDirty();
		omxLeds.setDirty();
		return;
	}
	if (action == 2)
	{
		openPotConfig();
		return;
	}
	if (action == 3)
	{
		// TRACK page's FX cell (P3): open the routed MidiFX group's editor — the menu
		// front door for what AUX+hold already does. Unrouted tracks get told, not sent
		// into an editor for a group they aren't using.
		uint8_t g = selMachine->getSelectedMidiFX();
		if (g >= NUM_MIDIFX_GROUPS)
			omxDisp.displayMessage("MFX OFF");
		else
			auxMacroManager_.enableSubmode(&subModeMidiFx[g]);
		return;
	}
	omxFormGlobal.encoderSelect = !omxFormGlobal.encoderSelect;
	omxDisp.setDirty();
}

void OmxModeForm::onEncoderButtonUp()
{
}

void OmxModeForm::onEncoderButtonDownLong()
{
}

void OmxModeForm::onEncoderButtonUpLong()
{
	// (Long-press is reserved for returning to the OMX mode switcher — no FORM action here.)
}

bool OmxModeForm::shouldBlockEncEdit()
{
	if (auxMacroManager_.shouldBlockEncEdit())
		return true;

	return false;
}

// FORM has no drum-kit concept; these satisfy the shared presetManager save/load callback
// interface (registered in the constructor) and intentionally do nothing.
void OmxModeForm::saveKit(uint8_t saveIndex)
{
	(void)saveIndex;
}

void OmxModeForm::loadKit(uint8_t loadIndex)
{
	(void)loadIndex;
}

void OmxModeForm::onKeyUpdate(OMXKeypadEvent e)
{
	omxDisp.setDirty();
	omxLeds.setDirty();

	auto selMachine = machines_[selectedMachine_];

	updateShortcutMode();

	if (auxMacroManager_.onKeyUpdate(e))
		return; // Key consumed by macro

	// Releases of keys whose DOWN the AUX layer consumed finish in the AUX layer: the
	// transport singles and rec-arm act HERE (on release, so the STOP chord can never
	// fire Play/Reset on the way in), and nothing leaks into the active view — even
	// when AUX itself was let go first.
	{
		uint8_t k = e.key();
		if (!e.down() && k > 0 && (auxSwallowMask_ & (1u << k)))
		{
			auxSwallowMask_ &= ~(1u << k);
			// Play/Pause + Reset fire on release whenever the key wasn't consumed by the
			// STOP chord or the hold action — aux1Used_/aux2Used_ carry that protection.
			// (An extra quickClicked() gate here created a dead zone: any press longer than
			// the ~200 ms click window but shorter than the hold threshold did NOTHING.)
			if (k == 1 && !aux1Used_)
			{
				togglePlayback();
				omxDisp.displayMessage(omxFormGlobal.isPlaying ? "PLAY" : "PAUSE");
			}
			else if (k == 2 && !aux2Used_)
			{
				resetPlayback();
				omxDisp.displayMessage("RESET");
			}
			else if (k == 3 && e.quickClicked())
			{
				// Quick-tap AUX + Rec Arm = toggle rec arm (a hold opens the CLEAR submenu).
				omxFormGlobal.recArm = !omxFormGlobal.recArm;
				recClearedMask_ = 0; // fresh replace pass
				omxDisp.displayMessage(omxFormGlobal.recArm ? "REC ARM" : "REC OFF");
			}
			omxDisp.setDirty();
			omxLeds.setDirty();
			return;
		}
	}

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

	// F3 + AUX = TAP TEMPO (the tempo companion to F3+encoder BPM). While F3 is held the
	// AUX key has no other job — the AUX layer only engages from NONE/AUX below — and the
	// reversed order (F-keys first, THEN AUX) can never collide with an AUX-layer shortcut,
	// which all require AUX to go down first. The BPM pops after each tap.
	if (thisKey == 0 && e.down() && !e.held() && omxFormGlobal.shortcutMode == FORMSHORTCUT_F3)
	{
		tapTempo();
		omxDisp.displayMessage("BPM " + String((int)clockConfig.clockbpm));
		omxDisp.setDirty();
		return;
	}

	// Don't go into aux mode if shortcuts F1 or F2 are being used
	if (omxFormGlobal.shortcutMode == FORMSHORTCUT_NONE || omxFormGlobal.shortcutMode == FORMSHORTCUT_AUX)
	{
		if (thisKey == 0)
		{
			// MI submenus: a fresh AUX press exits (QUANTIZE restores, CLEAR cancels). Keep the AUX
			// held-state bookkeeping correct either way (don't leave midiAUX stuck).
			if (formView_ == FORMVIEW_MI && (miQuantSub_ || miClearSub_))
			{
				midiSettings.midiAUX = e.down();
				if (e.down() && !e.held())
				{
					if (miQuantSub_)
					{
						quantExitSubmenu(false);
						omxDisp.setDirty();
						omxLeds.setDirty();
					}
					else
						closeClearSub(); // returns to the view the submenu was opened from
				}
				return;
			}
			// Step/Tools views: AUX while holding step(s) resets their value (no view browsing).
			if ((formView_ == FORMVIEW_STEP || formView_ == FORMVIEW_TOOLS) && heldStepMask_ != 0)
			{
				if (e.down() && !e.held())
				{
					auto omni = getSelectedMachine();
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
			else if (auxMacroManager_.isMFXQuickEditEnabled() == false && (thisKey == 1 || thisKey == 2)) // transport
			{
				// Singles act on RELEASE (the swallow handler above), so the STOP chord —
				// AUX + 1 + 2, in either order — can never fire Play or Reset on the way in.
				uint8_t other = (thisKey == 1) ? 2 : 1;
				if (thisKey == 1) aux1Used_ = false; else aux2Used_ = false;
				if (midiSettings.keyState[other] && (auxSwallowMask_ & (1u << other)))
				{
					aux1Used_ = aux2Used_ = true; // the chord consumed both keys
					doStopOrKill();               // STOP; STOP again while stopped = KILL
				}
				keyConsumed = true;
			}
			else if (thisKey == 3) // rec arm (latching) — §7
			{
				// Rec arm acts on release: a quick tap toggles arm, a hold opens the CLEAR
				// submenu (onKeyHeldUpdate). Nothing happens on the down press.
				keyConsumed = true;
			}
			else if (thisKey == 4) // rec mode: overdub / replace — §7
			{
				omxFormGlobal.recReplace = !omxFormGlobal.recReplace;
				omxDisp.displayMessage(omxFormGlobal.recReplace ? "REPLACE" : "OVERDUB");
				keyConsumed = true;
			}
			else if (thisKey >= 13 && thisKey <= 19) // v2 shell: preview view (commit on AUX release)
			{
				// Switch live (the view renders immediately, while AUX is still held).
				setFormView(thisKey - 13, true);
				omxDisp.displayMessage(kViewNames[pendingView_]);
				// AUX + DOUBLE-tap the view key = also jump the view to its first page
				// (views deliberately remember their position; this is the way back up).
				// clicks() counts RELEASES, so on a second press within the click window
				// it reads 1 — >=1 on a down event IS the double-tap.
				if (e.clicks() >= 1)
					viewHome((uint8_t)(thisKey - 13));
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
					aux1Used_ = true; // consumed by the hold — no Play on its release
					resetPlayback();
					omxDisp.displayMessage("STOP");
					keyConsumed = true;
				}
			}
		}

		// The AUX layer is modal: swallow every remaining key-down so the layer's FREE
		// keys can't fall through into the active view (AUX+5 was silently setting the
		// Patterns switch style, AUX+20-26 were switching pattern slots). The swallow
		// mask makes their RELEASES the AUX layer's too (handled at the top of this
		// function) so quick-tap view actions can't fire from an AUX chord either.
		if (e.down())
		{
			if (thisKey > 0)
				auxSwallowMask_ |= (1u << thisKey);
			keyConsumed = true;
		}
	}


	// v2 shell: container-rendered views take their own keys (not the machine).
	switch (formView_)
	{
	case FORMVIEW_STEP:
		if (keyConsumed)
			return;
		// Machine menu (page 4, STEPNOTES): F1/F2/F3 still copy/paste/length; a plain step
		// tap selects which step the notes editor edits.
		if (stepMenuPage_ == 4)
		{
			if (omxFormGlobal.shortcutMode != FORMSHORTCUT_NONE)
			{
				onKeyUpdateStep(e);
				return;
			}
			if (e.down() && !e.held() && thisKey >= 11 && thisKey < 27)
			{
				getSelectedMachine()->setSelStepByKey(thisKey - 11);
				omxLeds.setDirty();
				omxDisp.setDirty();
			}
			return;
		}
		onKeyUpdateStep(e);
		return;
	case FORMVIEW_PATTERNS:
		if (!keyConsumed)
			onKeyUpdatePatterns(e);
		return;
	case FORMVIEW_NOTES:
		if (!keyConsumed)
			onKeyUpdateNotes(e);
		return;
	case FORMVIEW_MI:
		if (!keyConsumed)
			onKeyUpdateMI(e);
		return;
	case FORMVIEW_TOOLS:
		if (!keyConsumed)
			onKeyUpdateTools(e);
		return;
	case FORMVIEW_MIX:
		if (!keyConsumed && onKeyUpdateMixRoute(e))
			return;
		break; // F3 + track falls through to the machine (rate + page length)
	case FORMVIEW_TRANSPOSE:
		// Params page: the keys belong to the pattern editor — a press drops back
		// to it (and still acts there), so nothing edits invisibly behind the params grid.
		if (transParamsPage_ && e.down() && thisKey >= 1 && thisKey < 27)
		{
			transParamsPage_ = false;
			omxDisp.setDirty();
		}
		break; // the machine's pattern editor takes the key below
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

	if (keyConsumed)
		return;

	// Whatever the shell didn't capture goes to the machine. In Mix this is the F3
	// fall-through (the machine's F3 branch handles rate + page length); the old
	// FORMMODE_BASE machine copy/cut/paste layer that lived here is gone.
	selMachine->onKeyUpdate(e);
}

bool OmxModeForm::onKeyUpdateSelMidiFX(OMXKeypadEvent e)
{
	// The selected track's actual MidiFX group (255 = off) — omxFormGlobal.selMidiFX is
	// never written, so passing it would always target group 1.
	if (auxMacroManager_.onKeyUpdateAuxMFXShortcuts(e, getSelectedMachine()->getSelectedMidiFX()))
		return true;

	return false;
}

bool OmxModeForm::onKeyHeldSelMidiFX(OMXKeypadEvent e)
{
	if (auxMacroManager_.onKeyHeldAuxMFXShortcuts(e, getSelectedMachine()->getSelectedMidiFX()))
		return true;

	return false;
}

void OmxModeForm::onKeyHeldUpdate(OMXKeypadEvent e)
{
	// Hold AUX + Rec Arm (key 3) = quick-access the CLEAR TRACK submenu. Handle it first, before the
	// macro / MFX / machine handlers that otherwise consume held AUX keys.
	if (omxFormGlobal.shortcutMode == FORMSHORTCUT_AUX && e.key() == 3 && !miClearSub_)
	{
		// A hold does not arm (the release toggle is suppressed because it's not a quick click).
		// Remember the current view + menu position, then show the CLEAR submenu (MI view).
		submenuSetReturn();
		miClearSub_ = true;
		clearSel_ = 0;
		omxDisp.setDirty();
		omxLeds.setDirty();
		return;
	}

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
	(void)thisKey;

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
	switch (formView_)
	{
	case FORMVIEW_STEP:
		if (stepMenuPage_ == 4)
		{
			getSelectedMachine()->updateLEDs(); // STEPNOTES uses the machine's own LEDs
			return;
		}
		updateStepLEDs();
		return;
	case FORMVIEW_PATTERNS:
		updatePatternsLEDs();
		return;
	case FORMVIEW_NOTES:
		updateNotesLEDs();
		return;
	case FORMVIEW_MI:
		updateMILEDs();
		return;
	case FORMVIEW_TOOLS:
		if (omxFormGlobal.shortcutMode != FORMSHORTCUT_NONE)
		{
			updateStepLEDs(); // F1/F2/F3 layers light exactly like the Seq view
			return;
		}
		updateToolsLEDs(); // action keys + its own step row (triggers only, empties dark)
		return;
	case FORMVIEW_MIX:
	case FORMVIEW_TRANSPOSE:
		break; // shared machine + track-row path below
	}

	auto selMachine = getSelectedMachine();

	if(selMachine->doesConsumeLEDs())
	{
		selMachine->updateLEDs();
		return;
	}

	bool blinkState = omxLeds.getBlinkState();
	// bool slowBlink = omxLeds.getSlowBlinkState();

	// Mix F1 (Mute) / F2 (Solo) layers: the top row shows mute/solo state in bright blue rather
	// than track colours, matching the on-screen MUTE/SOLO view. F1: blue = unmuted, off = muted.
	// F2: blue = soloed, off = not soloed. No selected highlight — the state is what matters here.
	if (formView_ == FORMVIEW_MIX &&
		(omxFormGlobal.shortcutMode == FORMSHORTCUT_F1 || omxFormGlobal.shortcutMode == FORMSHORTCUT_F2))
	{
		selMachine->updateLEDs(); // low row = the selected track's step content
		bool soloLayer = (omxFormGlobal.shortcutMode == FORMSHORTCUT_F2);
		const uint32_t kMuteSoloOn = 0xCCCCFF; // bright pale blue — reads clearly on the top row
		for (uint8_t i = 0; i < 8; i++)
		{
			bool on = (i < kNumMachines) && (soloLayer ? machines_[i]->getSolo() : !machines_[i]->getMute());
			strip.setPixelColor(3 + i, on ? kMuteSoloOn : (uint32_t)LEDOFF);
		}
		return;
	}

	// F3 machine might use these keys for shortcuts
	if (omxFormGlobal.shortcutMode != FORMSHORTCUT_F3)
	{
		for (uint8_t i = 0; i < kNumMachines; i++)
		{
			bool isMuted = machines_[i]->getMute();
			// Mix view: per-track colour from its hue. Other views use a single colour
			// (every track is the same engine — the per-type machine colours are gone).
			uint32_t trackColor = (formView_ == FORMVIEW_MIX)
									   ? trackHueColor(i)
									   : (uint32_t)ORANGE;
			// Mix: a muted (unselected) track goes dark so mute state is obvious; other views dim-red.
			uint32_t color = isMuted ? (formView_ == FORMVIEW_MIX ? (uint32_t)LEDOFF : (uint32_t)RED) : trackColor;

			if(i == selectedMachine_)
			{
				color = isMuted ? SALMON : WHITE; // keep the selected track visible even when muted
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

	// Render every loop (like Euclidean) so the GFX buffer — cleared each loop in the .ino — is
	// always repopulated. The old canShowDisplay() gate left it blank between the 60ms OLED
	// flushes, which the screen-mirror then captured as blank frames (flicker). The physical OLED
	// flush stays throttled in showDisplay(); this only refills the in-memory buffer.

	// F1+encoder page change: a large version of the overview's page icons pops for a
	// moment (while F1 is still held) instead of a text message.
	if (omxFormGlobal.shortcutMode == FORMSHORTCUT_F1 && pagePopupMs_ != 0 &&
		(uint32_t)(millis() - pagePopupMs_) < 900)
	{
		auto omni = getSelectedMachine();
		omxDisp.dispPageIconsLarge(omni->getEnabledPages(), omni->activePage());
		return;
	}

	// v2 shell: container-rendered views
	switch (formView_)
	{
	case FORMVIEW_STEP:
		onDisplayStep();
		return;
	case FORMVIEW_PATTERNS:
		onDisplayPatterns();
		return;
	case FORMVIEW_NOTES:
		onDisplayNotes();
		return;
	case FORMVIEW_MI:
		onDisplayMI();
		return;
	case FORMVIEW_TOOLS:
		onDisplayTools();
		return;
	case FORMVIEW_TRANSPOSE:
		if (transParamsPage_)
		{
			getSelectedMachine()->transParamsDraw(transSel_);
			return;
		}
		getSelectedMachine()->onDisplayUpdate(); // the machine draws the pattern editor
		return;
	case FORMVIEW_MIX:
		onDisplayMixView();
		return;
	}
}

// Mix view display routing: the F1/F2/F3 modifier screens, else its encoder pages
// (overview / LEVELS / CC / TRACK / menu).
void OmxModeForm::onDisplayMixView()
{
	auto selMachine = getSelectedMachine();

	if (selMachine->doesConsumeDisplay())
	{
		selMachine->onDisplayUpdate();
		return;
	}

	// Held F3 (LEN | RATE) shows the selected track's rate on top and its length as
	// a 16-cell bar on the bottom (full boxes for steps within length, dashes past it).
	if (omxFormGlobal.shortcutMode == FORMSHORTCUT_F3)
	{
		auto omni = selMachine;
		uint16_t pageStart = (uint16_t)omni->activePage() * 16;
		uint16_t trackLen = omni->trackPtr()->getLength(); // 1-64
		uint16_t rem = trackLen <= pageStart ? 0 : (trackLen - pageStart);
		uint8_t activeCount = rem > 16 ? 16 : (uint8_t)rem;
		dispF3RateLength(omni, activeCount);
		return;
	}

	// Held F1 shows the page-1 track overview (track squares + step glyphs already
	// carry mute state); the name reads "MUTE" instead of "TRK n".
	if (omxFormGlobal.shortcutMode == FORMSHORTCUT_F1)
	{
		onDisplaySeqTrackPage();
		return;
	}

	// Held F2 shows the split key-function view: top = track solos, bottom = FILL.
	if (omxFormGlobal.shortcutMode == FORMSHORTCUT_F2)
	{
		bool topFill[kNumMachines];
		for (uint8_t t = 0; t < kNumMachines; t++)
			topFill[t] = machines_[t]->getSolo();
		omxDisp.dispKeyFunctionSplit("SOLO", topFill, kNumMachines, "Fill", nullptr, 0);
		return;
	}

	onDisplayMix();
}

// incoming midi note on
void OmxModeForm::inMidiNoteOn(byte channel, byte note, byte velocity)
{
	// FORM does not consume incoming MIDI notes (the old drum-key idea was dropped).
	(void)channel; (void)note; (void)velocity;
}

void OmxModeForm::inMidiNoteOff(byte channel, byte note, byte velocity)
{
	(void)channel; (void)note; (void)velocity;
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
	// AUX-macro notes follow the selected track's routing (channel + default velocity), like the
	// normal FORM keyboard (previewNote) — not the global sysSettings.midiChannel/defaultVelocity.
	auto omni = getSelectedMachine();
	MidiNoteGroup noteGroup = omxUtil.midiNoteOn2(kbScale(), keyIndex,
												  omni->trackPtr()->paramDefaults[0], omni->getChannel() + 1);

	if (noteGroup.noteNumber == 255)
		return;

	// Serial.println("doNoteOn: " + String(noteGroup.noteNumber));

	noteGroup.unknownLength = true;
	noteGroup.prevNoteNumber = noteGroup.noteNumber;

	onNotePostFX(noteGroup);
}

// Called via doNoteOnForwarder
void OmxModeForm::doNoteOff(uint8_t keyIndex)
{
	MidiNoteGroup noteGroup = omxUtil.midiNoteOff2(keyIndex, getSelectedMachine()->getChannel() + 1);

	if (noteGroup.noteNumber == 255)
		return;

	// Serial.println("doNoteOff: " + String(noteGroup.noteNumber));

	noteGroup.unknownLength = true;
	noteGroup.prevNoteNumber = noteGroup.noteNumber;

	onNotePostFX(noteGroup);
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

	// Stopping ends the recording session: commit any held notes and disarm.
	if (!omxFormGlobal.isPlaying)
	{
		flushRecHeld();
		if (omxFormGlobal.recArm)
		{
			omxFormGlobal.recArm = false;
			omxLeds.setDirty();
		}
	}
}

// The AUX+1+2 STOP chord: stop + reset while playing; STOP again while already
// stopped = KILL (force note-offs everywhere — the escape hatch for stuck notes).
void OmxModeForm::doStopOrKill()
{
	if (omxFormGlobal.isPlaying)
	{
		togglePlayback();
		resetPlayback();
		omxDisp.displayMessage("STOP");
	}
	else
	{
		resetPlayback();
		killAllNotes();
		omxDisp.displayMessage("KILL");
	}
}

void OmxModeForm::killAllNotes()
{
	for (uint8_t k = 1; k < 27; k++)
		previewKeyOff(k);
	flushRecHeld();
	for (uint8_t i = 0; i < kNumMachines; i++)
		machines_[i]->flushNotes();
	pendingNoteOffs.allOff();
	for (uint8_t i = 0; i < NUM_MIDIFX_GROUPS; i++)
		subModeMidiFx[i].resync();
	// Belt and braces for anything a MidiFX already put on the wire: All Notes Off +
	// All Sound Off on every channel the tracks use.
	bool done[16] = {};
	for (uint8_t i = 0; i < kNumMachines; i++)
	{
		uint8_t ch = machines_[i]->getChannel(); // 0-15
		if (done[ch])
			continue;
		done[ch] = true;
		MM::sendControlChange(123, 0, ch + 1);
		MM::sendControlChange(120, 0, ch + 1);
	}
	omxLeds.setDirty();
}

// Tap tempo: AUX + encoder click, tapped in time. Rolling average over the run;
// a >2s gap starts a new run.
void OmxModeForm::tapTempo()
{
	bpmTapFlashMs_ = millis(); // flash the TAP button pressed (no popup — see onDisplayTools)
	uint32_t now = bpmTapFlashMs_;
	uint32_t gap = now - lastTapMs_;
	lastTapMs_ = now;
	omxDisp.setDirty();
	if (gap > 2000 || gap < 100) // new run (or switch bounce)
	{
		tapCount_ = 1;
		return;
	}
	tapAvgMs_ = (tapCount_ <= 1) ? (float)gap : (tapAvgMs_ + ((float)gap - tapAvgMs_) / 4.0f);
	if (tapCount_ < 255)
		tapCount_++;
	float bpm = constrain(60000.0f / tapAvgMs_, 40.0f, 300.0f);
	clockConfig.newtempo = bpm;
	if (clockConfig.newtempo != clockConfig.clockbpm)
	{
		clockConfig.clockbpm = clockConfig.newtempo;
		omxUtil.resetClocks();
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

	// Global FORM prefs (1 byte): note-entry Pressed/Toggle. In the FRAM stream so it
	// persists on every board (the old RP2040-only bank tail is gone). FORM sits LAST in
	// the storage stream, so growing this region can't shift any other mode.
	storage->write(startingAddress, omxFormGlobal.noteEntryToggle ? 1 : 0);
	startingAddress++;

	for (uint8_t i = 0; i < kNumMachines; i++)
	{
		// Single-engine: the per-slot type byte stays in the format (always OMNI) so
		// existing saves keep loading, but no machine picker exists any more.
		storage->write(startingAddress, 1); // 1 = the old FORMMACH_OMNI
		startingAddress++;
		startingAddress = machines_[i]->saveToDisk(startingAddress, storage);
	}

	int totalSize = startingAddress - initStart;
	Serial.println("FORM Size = " + String(totalSize));

	// V3: the full pattern bank goes to the flash filesystem alongside the FRAM copy
	// of the active pattern (no-op on Teensy).
	saveBankToFS();

	return startingAddress;
}

int OmxModeForm::loadFromDisk(int startingAddress, Storage *storage)
{
	// Global FORM prefs (mirrors saveToDisk): note-entry Pressed/Toggle.
	omxFormGlobal.noteEntryToggle = storage->read(startingAddress) == 1;
	startingAddress++;

	for (uint8_t i = 0; i < kNumMachines; i++)
	{
		startingAddress++; // skip the legacy machine-type byte (every track is OMNI)

		// Load in place (no machine re-creation): reset to defaults first so a
		// version-mismatched save leaves a clean track, then re-apply the default
		// channel (track index -> MIDI ch 1-8); a valid load overrides it.
		machines_[i]->setSeq(FormOmni::OmniSeq());
		machines_[i]->setChannel(i);
		startingAddress = machines_[i]->loadFromDisk(startingAddress, storage);
	}

	// V3: restore the full pattern bank from flash. The machines just loaded from FRAM
	// are the freshest copy of the active pattern, so re-snapshot them into the bank.
	if (loadBankFromFS())
	{
		snapshotActivePattern();
		omxDisp.setDirty();
		omxLeds.setDirty();
	}

	return startingAddress;
}

// Recover the bank after an FRAM header failure: the flash file is independent of
// FRAM, so a wiped/glitched header must not cost the patterns. Loads the bank and
// makes its active pattern live (the FRAM per-machine load never ran on this path).
void OmxModeForm::restoreBankFromFS()
{
	if (!loadBankFromFS())
		return;
	loadPatternIntoMachines(activePattern_);
	omxDisp.setDirty();
	omxLeds.setDirty();
	Serial.println("FORM bank recovered from flash");
}

// ---- Pattern-bank persistence (V3/RP2040: LittleFS; see the header note) ----
#if BOARDTYPE == OMX2040

static const char *kFormBankPath = "/formbank.dat";

struct FormBankHeader
{
	uint8_t magic0, magic1; // 'F','B'
	uint8_t version;        // couples to the OmniSeq layout (FormOmni::kOmniSaveVersion)
	uint8_t numPatterns;
	uint8_t numTracks;
	uint16_t seqSize;       // sizeof(OmniSeq) sanity check
};

void OmxModeForm::saveBankToFS()
{
	snapshotActivePattern(); // fold live edits into the bank before writing
	if (!LittleFS.begin())
	{
		Serial.println("FORM bank: LittleFS begin failed");
		return;
	}
	File f = LittleFS.open(kFormBankPath, "w");
	if (!f)
	{
		Serial.println("FORM bank: open for write failed");
		return;
	}
	FormBankHeader h = {'F', 'B', FormOmni::kOmniSaveVersion,
						FORM_NUM_PATTERNS, FORM_NUM_TRACKS, (uint16_t)sizeof(FormOmni::OmniSeq)};
	f.write((uint8_t *)&h, sizeof(h));
	f.write(&activePattern_, 1);
	f.write(&switchStyle_, 1);
	f.write(&recQuantize_, 1);
	f.write(trackHue_, sizeof(trackHue_));
	f.write((uint8_t *)patterns_, sizeof(patterns_));
	// (No tail any more: per-track scale lives inside OmniSeq since v9 — saved with every
	// pattern above — and note-entry persists in the FORM FRAM stream on all boards.)
	f.close();
	Serial.println("FORM bank saved (" + String((unsigned)sizeof(patterns_)) + " bytes)");
}

bool OmxModeForm::loadBankFromFS()
{
	if (!LittleFS.begin())
		return false;
	File f = LittleFS.open(kFormBankPath, "r");
	if (!f)
		return false; // no bank saved yet
	FormBankHeader h;
	bool headerOk = f.read((uint8_t *)&h, sizeof(h)) == (int)sizeof(h) &&
					h.magic0 == 'F' && h.magic1 == 'B' &&
					h.version == FormOmni::kOmniSaveVersion &&
					h.numPatterns == FORM_NUM_PATTERNS && h.numTracks == FORM_NUM_TRACKS &&
					h.seqSize == (uint16_t)sizeof(FormOmni::OmniSeq);
	if (!headerOk)
	{
		f.close();
		Serial.println("FORM bank: header mismatch, skipping");
		return false;
	}
	// Read the fixed fields into locals first: a truncated body (e.g. power loss mid-write —
	// the ~165 KB write is not atomic) must not leave activePattern_/switchStyle_ half-written
	// and unclamped, since they index patterns_[]/kSwitchStyleNames[] elsewhere.
	uint8_t activePat = 0, swStyle = 0, recQ = 0;
	uint8_t hues[sizeof(trackHue_)]; // matches the write's sizeof(trackHue_) exactly
	bool ok = f.read(&activePat, 1) == 1 &&
			  f.read(&swStyle, 1) == 1 &&
			  f.read(&recQ, 1) == 1 &&
			  f.read(hues, sizeof(hues)) == (int)sizeof(hues) &&
			  f.read((uint8_t *)patterns_, sizeof(patterns_)) == (int)sizeof(patterns_);
	f.close();
	if (!ok)
	{
		Serial.println("FORM bank: short read, skipping");
		return false;
	}
	// Commit, clamped. (Per-track scale rides inside each pattern's OmniSeq since v9 —
	// no tail to parse; note-entry comes from the FORM FRAM stream.)
	activePattern_ = (activePat >= FORM_NUM_PATTERNS) ? 0 : activePat;
	switchStyle_ = (swStyle > 3) ? 0 : swStyle;
	recQuantize_ = recQ;
	memcpy(trackHue_, hues, sizeof(trackHue_));
	Serial.println("FORM bank loaded");
	return true;
}

#else // Teensy: no filesystem — only the active pattern persists (FRAM/EEPROM blit above).

void OmxModeForm::saveBankToFS() {}
bool OmxModeForm::loadBankFromFS() { return false; }

#endif