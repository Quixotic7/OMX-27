#include "midimacro_m8v2.h"
#include "../globals.h"
#include "../utils/omx_util.h"
#include "../hardware/omx_disp.h"
#include "../hardware/omx_leds.h"
#include "../midi/midi.h"
#include "../consts/consts.h"
#include "../consts/colors.h"
#include "lpp_palette.h"

namespace midimacro
{
	// Launchpad Pro MK3 identity reply.
	// Full message: F0 7E 00 06 02 00 20 29 23 01 00 00 00 01 00 00 F7
	// MM::sendSysEx is called with hasBeginEnd = false and the F0/F7 omitted, the same
	// convention SysEx::sendCurrentState() uses.
	static const uint8_t kIdentityReply[] = {
		0x7E, 0x00, 0x06, 0x02,	// device inquiry reply
		0x00, 0x20, 0x29,		// Novation
		0x23, 0x01,				// Launchpad Pro MK3 family
		0x00, 0x00,				// member
		0x00, 0x01, 0x00, 0x00	// version
	};

	static void sendIdentityReply()
	{
		MM::sendSysExUSB(sizeof(kIdentityReply), kIdentityReply, false); // USB only: the M8 (or iOS app) is on USB, and TRS would eat 17 bytes/s while unlinked
	}

	void onDeviceInquiry(const uint8_t *data, unsigned length)
	{
		(void)data;
		(void)length;

#ifndef OMX_M8_MACRO_LEGACY
		// Slot 1 is the M8 macro. Reply even when the macro isn't entered, and from any
		// OMX mode - the M8 asks as soon as it's plugged in.
		if (midiMacroConfig.midiMacro == 1)
		{
			sendIdentityReply();
		}
#endif
	}

#ifndef OMX_M8_MACRO_LEGACY

	enum M8V2Page
	{
		M8V2PAGE_MAIN,	// row display, encoder scrolls
		M8V2PAGE_LATCH, // MUTE / SOLO latch-vs-momentary
	};

	// Returns a half-brightness version of a 0xRRGGBB colour by shifting each channel
	// right by 1 - used for the "off" phase of a pulsing (mode 2) LED.
	static inline uint32_t halfColor(uint32_t c)
	{
		uint8_t r = (uint8_t)(c >> 16);
		uint8_t g = (uint8_t)(c >> 8);
		uint8_t b = (uint8_t)c;
		r >>= 1;
		g >>= 1;
		b >>= 1;
		return ((uint32_t)r << 16) | ((uint32_t)g << 8) | b;
	}

	MidiMacroM8V2::MidiMacroM8V2()
	{
		params_.addPage(1); // Main
		params_.addPage(2); // Latch (MUTE, SOLO)
		encoderSelect_ = false;

		for (uint8_t i = 0; i < kNumLppNotes; i++)
		{
			ledColor_[i] = 0;
			ledMode_[i] = 0;
		}
		for (uint8_t i = 0; i < kNumKeys; i++)
		{
			keyNoteSent_[i] = 0;
		}

		dispLabel_[0] = 0;
		dispStatus_[0] = 0;
	}

	String MidiMacroM8V2::getName()
	{
		// Must stay "M8" so the macro list reads Off, M8, NRN, DEL.
		return String("M8");
	}

	void MidiMacroM8V2::sendIdentity()
	{
		sendIdentityReply();
		lastIdentityMs_ = millis();
	}

	void MidiMacroM8V2::onEnabled()
	{
		linked_ = false;
		auxHeld_ = false;

		// Always land back in Session/CLIP and release any modifier that might still be
		// held from a previous session (e.g. macro switched away while Mute was down).
		MM::sendNoteOff(2, 0, 1);
		MM::sendNoteOff(3, 0, 1);
		padMode_ = PAD_CLIP;
		view_ = VIEW_SESSION;

		sendIdentity();

		// Ask the M8 for Session view (LPP Session button = note 93).
		MM::sendNoteOn(93, 127, 1);
		MM::sendNoteOff(93, 0, 1);

		omxLeds.setDirty();
		omxDisp.setDirty();
	}

	void MidiMacroM8V2::onDisabled()
	{
		releaseAllKeys();

		// Release the Mute/Solo modifiers regardless of which pad mode was active -
		// harmless if they weren't held.
		MM::sendNoteOff(2, 0, 1);
		MM::sendNoteOff(3, 0, 1);

		for (uint8_t i = 0; i < kNumLppNotes; i++)
		{
			ledColor_[i] = 0;
			ledMode_[i] = 0;
		}

		auxHeld_ = false;
		omxLeds.setDirty();
	}

	void MidiMacroM8V2::releaseAllKeys()
	{
		for (uint8_t i = 0; i < kNumKeys; i++)
		{
			if (keyNoteSent_[i] != 0)
			{
				MM::sendNoteOff(keyNoteSent_[i], 0, 1);
				keyNoteSent_[i] = 0;
			}
		}
	}

	void MidiMacroM8V2::loopUpdate()
	{
		if (!enabled_)
			return;

		if (!linked_)
		{
			uint32_t now = millis();
			if (now - lastIdentityMs_ >= 1000)
			{
				sendIdentity();
			}
		}

		// omxLeds normally redraws itself on every blink transition (blinkAutoRefresh),
		// but we don't want to rely on that staying true for whichever mode currently
		// owns the strip. Force a redraw ourselves, at most once per transition, and only
		// when something we're mirroring is actually flash/pulse.
		bool blink = omxLeds.getBlinkState();
		bool slowBlink = omxLeds.getSlowBlinkState();
		if (blink != lastBlinkState_ || slowBlink != lastSlowBlinkState_)
		{
			lastBlinkState_ = blink;
			lastSlowBlinkState_ = slowBlink;

			bool anyBlinking = (padMode_ == PAD_MUTE && muteLatch_) || (padMode_ == PAD_SOLO && soloLatch_);
			for (uint8_t i = 0; i < kNumLppNotes && !anyBlinking; i++)
			{
				if (ledMode_[i] != 0 && ledColor_[i] != 0)
					anyBlinking = true;
			}

			if (anyBlinking)
				omxLeds.setDirty();
		}
	}

	void MidiMacroM8V2::onPotChanged(int potIndex, int prevValue, int newValue, int analogDelta)
	{
		(void)prevValue;
		(void)newValue;
		(void)analogDelta;
		omxUtil.sendPots(potIndex, midiMacroConfig.midiMacroChan);
	}

	// ---------------------------------------------------------------- MIDI in

	bool MidiMacroM8V2::inMidiNoteOn(byte channel, byte note, byte velocity)
	{
		// LPP LED feedback: ch 1 static, ch 2 flash, ch 3 pulse.
		if (channel < 1 || channel > 3)
			return false;

		if (note < kNumLppNotes)
		{
			ledColor_[note] = velocity; // vel 0 == note off
			ledMode_[note] = channel - 1;
		}

		if (!linked_)
		{
			linked_ = true;
			if (enabled_)
				omxDisp.setDirty();
		}

		// Only the macro's own drawLEDs() reads the cache, so don't force the host mode
		// to redraw its strip on every LED note when the macro isn't entered.
		if (enabled_)
			omxLeds.setDirty();
		return true;
	}

	bool MidiMacroM8V2::inMidiNoteOff(byte channel, byte note, byte velocity)
	{
		(void)velocity;

		if (channel < 1 || channel > 3)
			return false;

		if (note < kNumLppNotes)
		{
			ledColor_[note] = 0;
			ledMode_[note] = channel - 1;
		}

		if (!linked_)
		{
			linked_ = true;
			if (enabled_)
				omxDisp.setDirty();
		}

		if (enabled_)
			omxLeds.setDirty();
		return true;
	}

	void MidiMacroM8V2::inMidiControlChange(byte channel, byte control, byte value)
	{
		// Some hosts send the ring LEDs as CCs with the same number as the LPP note.
		if (channel < 1 || channel > 3)
			return;

		if (control < kNumLppNotes)
		{
			ledColor_[control] = value;
			ledMode_[control] = channel - 1;

			if (!linked_)
			{
				linked_ = true;
				if (enabled_)
					omxDisp.setDirty();
			}

			if (enabled_)
				omxLeds.setDirty();
		}
	}

	// ------------------------------------------------------------------- Keys

	uint8_t MidiMacroM8V2::lppNoteForKey(uint8_t key)
	{
		// LPP MK3 programmer numbering: note = row * 10 + col, row 1 = bottom.

		if (view_ == VIEW_NOTE)
		{
			// All 16 white keys are pads: 11-18 = row_, 19-26 = row_+1.
			if (key >= 11 && key <= 18)
				return (uint8_t)(row_ * 10 + (key - 10));

			if (key >= 19 && key <= 26)
			{
				uint8_t row2 = row_ + 1;
				if (row2 > 8)
					row2 = 8;
				return (uint8_t)(row2 * 10 + (key - 18));
			}

			// Black keys carry the whole nav cluster in Note view (Session-only pad
			// mode keys 3/4/5 are dark/inactive here).
			switch (key)
			{
			case 6:
				return 90; // Shift (Note view only)
			case 7:
				return 20; // Play (Note view only)
			case 8:
				return 80; // Up
			case 10:
				return 10; // Edit / Rec
			default:
				return 0; // 0/1/2 (local), 3/4/5 (inactive), 9 (Option) send nothing
			}
		}

		// Session view. With Mute/Solo held, the grid becomes track select (101-108).
		if (key >= 11 && key <= 18)
		{
			if (padMode_ != PAD_CLIP)
				return (uint8_t)(101 + (key - 11)); // track buttons T1-T8

			return (uint8_t)(row_ * 10 + (key - 10)); // grid cols 1-8 of row_
		}

		switch (key)
		{
		case 19:
			return (uint8_t)(row_ * 10 + 9); // row launch / scene
		case 8:
			return 80; // Up
		case 22:
			return 70; // Down
		case 21:
			return 91; // Track <
		case 23:
			return 92; // Track >
		case 10:
			return 10; // Edit / Rec
		case 24:
			return 90; // Shift
		case 26:
			return 20; // Play
		default:
			return 0; // key 9 (Option) and everything else send nothing
		}
	}

	void MidiMacroM8V2::scrollRow(int8_t dir)
	{
		// Note view shows row_ and row_+1, so it can't scroll past 7.
		uint8_t maxRow = (view_ == VIEW_NOTE) ? 7 : 8;

		int8_t newRow = (int8_t)row_ + dir;
		if (newRow < 1)
			newRow = 1;
		if (newRow > (int8_t)maxRow)
			newRow = (int8_t)maxRow;

		if ((uint8_t)newRow != row_)
		{
			row_ = (uint8_t)newRow;
			omxLeds.setDirty();
			omxDisp.setDirty();
		}
	}

	// Pressing the active MUTE/SOLO key again toggles latch vs momentary; pressing a
	// different mode key switches modes, releasing/reacquiring the M8-side modifier.
	//
	// NOTE (plan section 4b, unverified on hardware): the actual M8/LPP Mute-Solo
	// semantics (a held modifier + track button vs a toggled mode) are not confirmed.
	// What we implement here is intentionally simple: entering MUTE/SOLO holds down
	// Note 2/3 for as long as that mode is active and keys 11-18 send track buttons
	// 101-108 while held; the latch flag doesn't change that OMX-side behaviour at all
	// (it's the same Note On/Off either way) - it's only meant to be read on the M8 side,
	// where latch vs momentary is expected to change how the M8 treats the mute/solo
	// press. If real hardware disagrees, this is the place to change it.
	void MidiMacroM8V2::setPadMode(PadMode newMode)
	{
		if (newMode == padMode_)
		{
			if (padMode_ == PAD_MUTE)
			{
				muteLatch_ = !muteLatch_;
				omxDisp.displayMessageTimed(muteLatch_ ? "Mute: Latch" : "Mute: Moment", 5);
			}
			else if (padMode_ == PAD_SOLO)
			{
				soloLatch_ = !soloLatch_;
				omxDisp.displayMessageTimed(soloLatch_ ? "Solo: Latch" : "Solo: Moment", 5);
			}

			omxLeds.setDirty();
			return;
		}

		// Leaving MUTE/SOLO releases its modifier, whether latch or momentary.
		if (padMode_ == PAD_MUTE)
			MM::sendNoteOff(2, 0, 1);
		else if (padMode_ == PAD_SOLO)
			MM::sendNoteOff(3, 0, 1);

		padMode_ = newMode;

		if (padMode_ == PAD_MUTE)
			MM::sendNoteOn(2, 127, 1);
		else if (padMode_ == PAD_SOLO)
			MM::sendNoteOn(3, 127, 1);

		omxLeds.setDirty();
		omxDisp.setDirty();
	}

	void MidiMacroM8V2::onKeyUpdate(OMXKeypadEvent e)
	{
		uint8_t thisKey = (uint8_t)e.key();

		// Ignore held repeats, same as the other macros.
		if (e.held())
			return;

		if (thisKey >= kNumKeys)
			return;

		// Key up: always release what we actually sent, never a recomputed note.
		// Runs before the AUX guard so a pad held when AUX goes down can't stick.
		if (!e.down())
		{
			if (thisKey == 0)
			{
				auxHeld_ = false;
				omxLeds.setDirty();
				return;
			}

			if (keyNoteSent_[thisKey] != 0)
			{
				MM::sendNoteOff(keyNoteSent_[thisKey], 0, 1);
				keyNoteSent_[thisKey] = 0;
				omxLeds.setDirty();
			}
			return;
		}

		// Key down
		if (thisKey == 0)
		{
			auxHeld_ = true;
			omxLeds.setDirty();
			return;
		}

		// AUX shortcut layer: keys pressed while AUX is held never send pads.
		if (auxHeld_)
		{
			switch (thisKey)
			{
			case 1:
				// Down (tap)
				MM::sendNoteOn(70, 127, 1);
				MM::sendNoteOff(70, 0, 1);
				return;
			case 2:
				// Up (tap)
				MM::sendNoteOn(80, 127, 1);
				MM::sendNoteOff(80, 0, 1);
				return;
			case 3:
				MM::sendNoteOn(93, 127, 1);
				MM::sendNoteOff(93, 0, 1);
				view_ = VIEW_SESSION;
				omxDisp.displayMessageTimed("SESSION", 5);
				omxLeds.setDirty();
				omxDisp.setDirty();
				return;
			case 4:
				MM::sendNoteOn(94, 127, 1);
				MM::sendNoteOff(94, 0, 1);
				view_ = VIEW_NOTE;
				if (row_ > 7)
					row_ = 7; // Note view shows row_ and row_+1
				omxDisp.displayMessageTimed("NOTE", 5);
				omxLeds.setDirty();
				omxDisp.setDirty();
				return;
			case 5:
				// Seq view - the M8 switches, the OMX stays in its current layout.
				MM::sendNoteOn(97, 127, 1);
				MM::sendNoteOff(97, 0, 1);
				omxDisp.displayMessageTimed("SEQ", 5);
				return;
			default:
				return; // consumed, no-op
			}
		}

		switch (thisKey)
		{
		case 1:
			scrollRow(-1);
			return;
		case 2:
			scrollRow(1);
			return;
		case 3:
			if (view_ == VIEW_SESSION)
				setPadMode(PAD_CLIP);
			return;
		case 4:
			if (view_ == VIEW_SESSION)
				setPadMode(PAD_MUTE);
			return;
		case 5:
			if (view_ == VIEW_SESSION)
				setPadMode(PAD_SOLO);
			return;
		case 9:
			// Option: no LPP equivalent, lit placeholder that sends nothing.
			return;
		default:
			break;
		}

		uint8_t note = lppNoteForKey(thisKey);
		if (note != 0)
		{
			MM::sendNoteOn(note, 127, 1);
			keyNoteSent_[thisKey] = note;
			omxLeds.setDirty();
		}
	}

	// ------------------------------------------------------------------- LEDs

	void MidiMacroM8V2::drawPaletteKey(uint8_t key, uint8_t note, uint32_t offColor)
	{
		if (note >= kNumLppNotes)
		{
			strip.setPixelColor(key, offColor);
			return;
		}

		uint8_t idx = ledColor_[note];
		if (idx == 0)
		{
			strip.setPixelColor(key, offColor);
			return;
		}

		uint32_t color = lppPaletteColor(idx);
		uint8_t mode = ledMode_[note];

		if (mode == 1) // flash
		{
			strip.setPixelColor(key, omxLeds.getBlinkState() ? color : LEDOFF);
		}
		else if (mode == 2) // pulse
		{
			strip.setPixelColor(key, omxLeds.getSlowBlinkState() ? color : halfColor(color));
		}
		else // static
		{
			strip.setPixelColor(key, color);
		}
	}

	void MidiMacroM8V2::drawLEDs()
	{
		if (omxLeds.isDirty() == false)
			return;

		omxLeds.setAllLEDS(0, 0, 0);

		strip.setPixelColor(0, PURPLE); // AUX, always

		if (auxHeld_)
		{
			// AUX shortcut overlay - everything else stays dark.
			strip.setPixelColor(3, view_ == VIEW_SESSION ? CYAN : DKCYAN);
			strip.setPixelColor(4, view_ == VIEW_NOTE ? LTCYAN : DKCYAN);
			strip.setPixelColor(5, DKCYAN); // Seq view - OMX has no "current view" state for it
			strip.setPixelColor(1, ORANGE);
			strip.setPixelColor(2, ORANGE);
			return;
		}

		// Row scroll - Note view can't scroll past 7 (it shows row_ and row_+1).
		uint8_t maxRow = (view_ == VIEW_NOTE) ? 7 : 8;
		strip.setPixelColor(1, row_ > 1 ? INDIGO : LOWWHITE);
		strip.setPixelColor(2, row_ < maxRow ? INDIGO : LOWWHITE);

		if (view_ == VIEW_SESSION)
		{
			// Pad mode keys: active bright, inactive dim; MUTE/SOLO blink when latched.
			bool muteDark = (padMode_ == PAD_MUTE && muteLatch_ && !omxLeds.getSlowBlinkState());
			bool soloDark = (padMode_ == PAD_SOLO && soloLatch_ && !omxLeds.getSlowBlinkState());

			strip.setPixelColor(3, padMode_ == PAD_CLIP ? MAGENTA : DKMAGENTA);
			strip.setPixelColor(4, (padMode_ == PAD_MUTE && !muteDark) ? RED : DKRED);
			strip.setPixelColor(5, (padMode_ == PAD_SOLO && !soloDark) ? YELLOW : DKYELLOW);

			// Nav cluster
			strip.setPixelColor(8, INDIGO);	 // Up
			strip.setPixelColor(21, INDIGO); // Track <
			strip.setPixelColor(22, INDIGO); // Down
			strip.setPixelColor(23, INDIGO); // Track >

			strip.setPixelColor(9, WHITE);	// Option placeholder
			strip.setPixelColor(10, RBLUE); // Edit
			strip.setPixelColor(24, GREEN); // Shift
			strip.setPixelColor(26, ledColor_[20] != 0 ? GREEN : WHITE); // Play

			drawPaletteKey(19, (uint8_t)(row_ * 10 + 9), LOWWHITE); // Row launch / scene

			for (uint8_t k = 11; k <= 18; k++)
			{
				// Must mirror lppNoteForKey(): in MUTE/SOLO these keys are the track
				// buttons 101-108, not the grid row.
				uint8_t note = (padMode_ != PAD_CLIP)
								   ? (uint8_t)(101 + (k - 11))
								   : (uint8_t)(row_ * 10 + (k - 10));
				drawPaletteKey(k, note, LEDOFF);
			}
		}
		else // VIEW_NOTE
		{
			strip.setPixelColor(3, LEDOFF);
			strip.setPixelColor(4, LEDOFF);
			strip.setPixelColor(5, LEDOFF);

			strip.setPixelColor(6, GREEN);	 // Shift (Note view only)
			strip.setPixelColor(7, WHITE);	 // Play (Note view only)
			strip.setPixelColor(8, INDIGO); // Up
			strip.setPixelColor(9, WHITE);	 // Option placeholder
			strip.setPixelColor(10, RBLUE); // Edit

			uint8_t row2 = row_ + 1;
			if (row2 > 8)
				row2 = 8;

			for (uint8_t k = 11; k <= 18; k++)
			{
				uint8_t note = (uint8_t)(row_ * 10 + (k - 10));
				drawPaletteKey(k, note, LEDOFF);
			}
			for (uint8_t k = 19; k <= 26; k++)
			{
				uint8_t note = (uint8_t)(row2 * 10 + (k - 18));
				drawPaletteKey(k, note, LEDOFF);
			}
		}
	}

	// ---------------------------------------------------------------- Display

	void MidiMacroM8V2::onEncoderChangedEditParam(Encoder::Update enc)
	{
		int8_t page = params_.getSelPage();
		int8_t param = params_.getSelParam();

		if (page != M8V2PAGE_LATCH)
		{
			// Main page: turning the encoder scrolls the row, no button press needed.
			scrollRow(enc.dir());
			return;
		}

		// Latch page: any turn flips the latch flag for the selected param. See the
		// comment on setPadMode() - this is the same flag, just editable two ways.
		if (enc.dir() != 0)
		{
			if (param == 0)
			{
				muteLatch_ = !muteLatch_;
				omxDisp.displayMessageTimed(muteLatch_ ? "Mute: Latch" : "Mute: Moment", 5);
			}
			else if (param == 1)
			{
				soloLatch_ = !soloLatch_;
				omxDisp.displayMessageTimed(soloLatch_ ? "Solo: Latch" : "Solo: Moment", 5);
			}
			omxLeds.setDirty();
		}

		omxDisp.setDirty();
	}

	void MidiMacroM8V2::onDisplayUpdate()
	{
		omxDisp.clearLegends();

		int8_t page = params_.getSelPage();

		if (page == M8V2PAGE_LATCH)
		{
			omxDisp.setLegend(0, "MUTE", !muteLatch_, muteLatch_ ? "LATCH" : "MOMENT");
			omxDisp.setLegend(1, "SOLO", !soloLatch_, soloLatch_ ? "LATCH" : "MOMENT");
			omxDisp.dispGenericMode2(params_.getNumPages(), params_.getSelPage(), params_.getSelParam(), encoderSelect_);
			return;
		}

		const char *modeName = "CLIP";
		if (padMode_ == PAD_MUTE)
			modeName = "MUTE";
		else if (padMode_ == PAD_SOLO)
			modeName = "SOLO";

		const char *viewName = (view_ == VIEW_NOTE) ? "NOTE" : "SESS";

		if (view_ == VIEW_NOTE)
			snprintf(dispLabel_, sizeof(dispLabel_), "M8 %s R%d", viewName, (int)row_);
		else
			snprintf(dispLabel_, sizeof(dispLabel_), "M8 %s R%d %s", viewName, (int)row_, modeName);

		snprintf(dispStatus_, sizeof(dispStatus_), "LPP %s", linked_ ? "LINK" : "WAIT");

		omxDisp.dispGenericModeLabelDoubleLine(dispLabel_, dispStatus_, params_.getNumPages(), params_.getSelPage());
	}

#endif // !OMX_M8_MACRO_LEGACY
}
