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

	// Handshake diagnostics shown on the display while unlinked: identities sent / inquiries received.
	static uint16_t s_identitySent = 0;
	static uint16_t s_inquiryRx = 0;

	static void sendIdentityReply()
	{
		s_identitySent++;
		MM::sendSysExUSB(sizeof(kIdentityReply), kIdentityReply, false); // USB only: the M8 (or iOS app) is on USB, and TRS would eat 17 bytes/s while unlinked
	}

	void onDeviceInquiry(const uint8_t *data, unsigned length)
	{
		(void)data;
		(void)length;
		s_inquiryRx++;

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
		params_.addPage(3); // Latch (MUTE, SOLO) + RING (CC/NOTE)
		encoderSelect_ = false;

		for (uint8_t i = 0; i < kNumLppNotes; i++)
		{
			ledColor_[i] = 0;
			ledMode_[i] = 0;
		}
		for (uint8_t i = 0; i < kNumKeys; i++)
		{
			keyNoteSent_[i] = 0;
			ctrlSent_[i] = -1;
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
		sendLpp(2, false);
		sendLpp(3, false);
		padMode_ = PAD_CLIP;
		view_ = VIEW_SESSION;
		row_ = 8; // Session pins the left keys to Launchpad row 8; keys 1/2 move the box.
		latchedTracks_ = 0;
		momentaryTracks_ = 0;
		trackHeld_ = false;
		for (uint8_t i = 0; i < kNumKeys; i++)
			ctrlSent_[i] = -1;

		sendIdentity();

		// Ask the M8 for Session view (LPP Session button = note 93).
		sendLppTap(93);

		omxLeds.setDirty();
		omxDisp.setDirty();
	}

	void MidiMacroM8V2::onDisabled()
	{
		releaseAllKeys();
		releaseLatchedTracks();
		momentaryTracks_ = 0;
		for (uint8_t i = 0; i < kNumKeys; i++)
		{
			if (ctrlSent_[i] >= 0)
			{
				MM::sendNoteOff((uint8_t)ctrlSent_[i], 0, midiMacroConfig.midiMacroChan);
				ctrlSent_[i] = -1;
			}
		}

		// Release the Mute/Solo modifiers regardless of which pad mode was active -
		// harmless if they weren't held.
		sendLpp(2, false);
		sendLpp(3, false);

		for (uint8_t i = 0; i < kNumLppNotes; i++)
		{
			ledColor_[i] = 0;
			ledMode_[i] = 0;
		}

		auxHeld_ = false;
		trackHeld_ = false;
		if (recLatched_) { recLatched_ = false; sendLpp(10, false); }
		omxLeds.setDirty();
	}

	void MidiMacroM8V2::releaseAllKeys()
	{
		for (uint8_t i = 0; i < kNumKeys; i++)
		{
			if (keyNoteSent_[i] != 0)
			{
				sendLpp(keyNoteSent_[i], false);
				keyNoteSent_[i] = 0;
			}
		}
	}

	bool MidiMacroM8V2::isRingButton(uint8_t n)
	{
		if (n >= 11 && n <= 88)
		{
			uint8_t col = n % 10;
			return (col == 0 || col == 9);
		}
		return true; // 1-8, 10/20/..90, 91-99, 101-108
	}

	int8_t MidiMacroM8V2::controlMapNote(uint8_t key)
	{
		switch (key)
		{
		case 8:  return 6; // Up
		case 21: return 4; // Left
		case 22: return 7; // Down
		case 23: return 5; // Right
		case 9:  return 3; // Option
		case 10: return 2; // Edit
		case 24: return 1; // Shift
		case 26: return 0; // Play
		default: return -1;
		}
	}

	void MidiMacroM8V2::sendLpp(uint8_t n, bool on)
	{
		if (ringAsCC_ && isRingButton(n))
			MM::sendControlChange(n, on ? 127 : 0, 1);
		else if (on)
			MM::sendNoteOn(n, 127, 1);
		else
			MM::sendNoteOff(n, 0, 1);
	}

	void MidiMacroM8V2::releaseLatchedTracks()
	{
		for (uint8_t i = 0; i < 8; i++)
		{
			if (latchedTracks_ & (1 << i))
				sendLpp((uint8_t)(101 + i), false);
		}
		latchedTracks_ = 0;
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
			// Notes layout: keys 1/2 scroll the M8 keyboard (LPP Down/Up), key 3 is a held
			// Track modifier (handled in onKeyUpdate), 9 = Edit/Rec, 10 = Play. White keys
			// 11-26 are the left 4x4 of the keyboard (rows 5-8, cols 1-4), or the track
			// buttons 101-108 on keys 11-18 while Track is held.
			switch (key)
			{
			case 1: return 70;  // Scroll Down
			case 2: return 80;  // Scroll Up
			case 9: return 10;  // Edit / Rec
			case 10: return 20; // Play
			default: break;
			}
			if (trackHeld_ && key >= 11 && key <= 18)
				return (uint8_t)(101 + (key - 11));
			return seqSlotNote(key); // 0 for keys 3-8
		}

		// Session view. Keys 1/2 nudge the M8 session box (LPP Up 80 / Down 70). The left
		// keys are pinned to Launchpad row 8 (row_ == 8 in Session).
		if (key == 1)
			return 70; // Scroll Down (box)
		if (key == 2)
			return 80; // Scroll Up (box)

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

		// Leaving MUTE/SOLO releases its modifier and any OMX-side latched tracks.
		releaseLatchedTracks();
		momentaryTracks_ = 0;
		if (padMode_ == PAD_MUTE)
			sendLpp(2, false);
		else if (padMode_ == PAD_SOLO)
			sendLpp(3, false);

		padMode_ = newMode;

		if (padMode_ == PAD_MUTE)
			sendLpp(2, true);
		else if (padMode_ == PAD_SOLO)
			sendLpp(3, true);

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
			if (thisKey == 3 && trackHeld_)
			{
				trackHeld_ = false;
				omxLeds.setDirty();
				omxDisp.setDirty();
				return;
			}

			// Control Map cluster (macro channel).
			if (ctrlSent_[thisKey] >= 0)
			{
				MM::sendNoteOff((uint8_t)ctrlSent_[thisKey], 0, midiMacroConfig.midiMacroChan);
				ctrlSent_[thisKey] = -1;
				omxLeds.setDirty();
				return;
			}

			// Momentary MUTE/SOLO track: re-tap to undo the toggle the press made.
			if (thisKey >= 11 && thisKey <= 18)
			{
				uint8_t bit = (uint8_t)(1 << (thisKey - 11));
				if (momentaryTracks_ & bit)
				{
					sendLppTap((uint8_t)(101 + (thisKey - 11)));
					momentaryTracks_ &= (uint8_t)~bit;
					omxLeds.setDirty();
					return;
				}
			}

			if (keyNoteSent_[thisKey] != 0)
			{
				sendLpp(keyNoteSent_[thisKey], false);
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
			// SEQ: AUX + a white key selects the pattern (right 4x4). Sent as a grid note and
			// tracked in keyNoteSent_, so the (un-guarded) key-up releases it.
			if (view_ == VIEW_SEQ && thisKey >= 11 && thisKey <= 26)
			{
				uint8_t pnote = seqPatternNote(thisKey);
				sendLpp(pnote, true);
				keyNoteSent_[thisKey] = pnote;
				omxLeds.setDirty();
				return;
			}

			// SEQ transport on the AUX layer so all eight keypads stay free:
			// AUX+9 = Edit/Rec tap (toggle the M8 editing submode), AUX+10 = Play tap,
			// AUX+8 = latch Record held/released (M8 "hold Rec + keypad" / "hold Rec + Play").
			if (view_ == VIEW_SEQ && (thisKey == 8 || thisKey == 9 || thisKey == 10))
			{
				if (thisKey == 8)
				{
					recLatched_ = !recLatched_;
					sendLpp(10, recLatched_);
					omxDisp.displayMessageTimed(recLatched_ ? "REC HELD" : "REC OFF", 5);
				}
				else if (thisKey == 9)
				{
					if (!recLatched_)
						sendLppTap(10);
				}
				else
					sendLppTap(20);
				omxLeds.setDirty();
				omxDisp.setDirty();
				return;
			}

			switch (thisKey)
			{
			case 1:
				// Down (tap)
				sendLppTap(70);
				return;
			case 2:
				// Up (tap)
				sendLppTap(80);
				return;
			case 3:
				sendLppTap(93);
				if (recLatched_) { recLatched_ = false; sendLpp(10, false); }
				view_ = VIEW_SESSION;
				row_ = 8;
				omxDisp.displayMessageTimed("SESSION", 5);
				omxLeds.setDirty();
				omxDisp.setDirty();
				return;
			case 4:
				sendLppTap(94);
				if (recLatched_) { recLatched_ = false; sendLpp(10, false); }
				view_ = VIEW_NOTE;
				trackHeld_ = false;
				omxDisp.displayMessageTimed("NOTE", 5);
				omxLeds.setDirty();
				omxDisp.setDirty();
				return;
			case 5:
				sendLppTap(97);
				view_ = VIEW_SEQ;
				omxDisp.displayMessageTimed("SEQ", 5);
				omxLeds.setDirty();
				omxDisp.setDirty();
				return;
			default:
				return; // consumed, no-op
			}
		}

		if (view_ == VIEW_SEQ)
		{
			uint8_t note = (thisKey <= 10) ? seqNotePadNote(thisKey) : seqSlotNote(thisKey);
			if (note != 0)
			{
				sendLpp(note, true);
				keyNoteSent_[thisKey] = note;
				omxLeds.setDirty();
			}
			return;
		}

		if (view_ == VIEW_SESSION)
		{
			switch (thisKey)
			{
			case 3: setPadMode(PAD_CLIP); return;
			case 4: setPadMode(PAD_MUTE); return;
			case 5: setPadMode(PAD_SOLO); return;
			default: break;
			}

			// Right-hand cluster drives the M8 Control Map on the macro channel (M-CH),
			// like the pre-Launchpad M8 macro. Fixed per key, so key-up releases cleanly.
			int8_t cm = controlMapNote(thisKey);
			if (cm >= 0)
			{
				MM::sendNoteOn((uint8_t)cm, 1, midiMacroConfig.midiMacroChan);
				ctrlSent_[thisKey] = cm;
				omxLeds.setDirty();
				return;
			}
			// Keys 1/2 (box up/down) and 11-19 (row-8 pads / tracks) fall through.
		}
		else // VIEW_NOTE
		{
			if (thisKey == 3)
			{
				trackHeld_ = true; // hold: keys 11-18 become track select
				omxLeds.setDirty();
				omxDisp.setDirty();
				return;
			}
		}

		uint8_t note = lppNoteForKey(thisKey);

		// MUTE/SOLO: keys 11-18 are the M8 track buttons. A tap toggles that track's
		// mute/solo (the M8 acts on the press and ignores the release). Momentary re-taps
		// on key-up to undo it; latch leaves it toggled until pressed again.
		if (view_ == VIEW_SESSION && padMode_ != PAD_CLIP && thisKey >= 11 && thisKey <= 18)
		{
			uint8_t bit = (uint8_t)(1 << (thisKey - 11));
			bool latch = (padMode_ == PAD_MUTE && muteLatch_) || (padMode_ == PAD_SOLO && soloLatch_);
			sendLppTap(note);
			if (latch)
				latchedTracks_ ^= bit; // LED memory only; the M8 holds the real state
			else
				momentaryTracks_ |= bit;
			omxLeds.setDirty();
			return;
		}

		if (note != 0)
		{
			sendLpp(note, true);
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

	uint8_t MidiMacroM8V2::seqNotePadNote(uint8_t key)
	{
		if (key == 1) return 70;                   // Scroll Down (keypads)
		if (key == 2) return 80;                   // Scroll Up
		if (key >= 3 && key <= 10) return key + 8; // LPP row 1 C1-C8 -> 11-18 (keypads)
		return 0;                                  // Rec/Play live on the AUX layer in Seq
	}

	uint8_t MidiMacroM8V2::seqSlotNote(uint8_t key)
	{
		if (key < 11 || key > 26) return 0;
		uint8_t idx = key - 11;
		return (uint8_t)((8 - idx / 4) * 10 + (idx % 4) + 1); // cols 1-4
	}

	uint8_t MidiMacroM8V2::seqPatternNote(uint8_t key)
	{
		if (key < 11 || key > 26) return 0;
		uint8_t idx = key - 11;
		return (uint8_t)((8 - idx / 4) * 10 + (idx % 4) + 5); // cols 5-8
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
			strip.setPixelColor(5, view_ == VIEW_SEQ ? LTCYAN : DKCYAN);
			if (view_ == VIEW_SEQ)
			{
				for (uint8_t k = 11; k <= 26; k++)
					drawPaletteKey(k, seqPatternNote(k), DKPURPLE);
				strip.setPixelColor(8, recLatched_ ? RED : DKRED);          // latch Record
				strip.setPixelColor(9, ledColor_[10] != 0 ? RED : DKRED);   // Edit/Rec tap
				strip.setPixelColor(10, ledColor_[20] != 0 ? GREEN : WHITE); // Play tap
			}
			else
			{
				strip.setPixelColor(1, ORANGE);
				strip.setPixelColor(2, ORANGE);
			}
			return;
		}

		// Keys 1/2 = LPP Scroll Down/Up in every view (Session recolours them below).
		strip.setPixelColor(1, INDIGO);
		strip.setPixelColor(2, INDIGO);

		if (view_ == VIEW_SESSION)
		{
			// Keys 1/2 move the M8 session box up/down (not row scroll in Session).
			strip.setPixelColor(1, RBLUE);
			strip.setPixelColor(2, RBLUE);

			// Pad mode keys: active bright, inactive dim; MUTE/SOLO blink when latched.
			bool muteDark = (padMode_ == PAD_MUTE && muteLatch_ && !omxLeds.getSlowBlinkState());
			bool soloDark = (padMode_ == PAD_SOLO && soloLatch_ && !omxLeds.getSlowBlinkState());

			strip.setPixelColor(3, padMode_ == PAD_CLIP ? MAGENTA : DKMAGENTA);
			strip.setPixelColor(4, (padMode_ == PAD_MUTE && !muteDark) ? RED : DKRED);
			strip.setPixelColor(5, (padMode_ == PAD_SOLO && !soloDark) ? YELLOW : DKYELLOW);

			// Nav cluster
			strip.setPixelColor(8, INDIGO);	 // Up (Control Map)
			strip.setPixelColor(21, INDIGO); // Left (Control Map)
			strip.setPixelColor(22, INDIGO); // Down (Control Map)
			strip.setPixelColor(23, INDIGO); // Right (Control Map)

			strip.setPixelColor(9, ORANGE);	// Option (Control Map, now functional)
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
				if (padMode_ != PAD_CLIP && ledColor_[note] == 0 && (latchedTracks_ & (1 << (k - 11))))
					strip.setPixelColor(k, padMode_ == PAD_MUTE ? RED : YELLOW); // latched, no colour from the M8
			}
		}
		else if (view_ == VIEW_SEQ)
		{
			// Black keys 3-10 = keypads (LPP row 1); white keys 11-26 = left-4x4 note slots.
			for (uint8_t k = 3; k <= 10; k++)
				drawPaletteKey(k, seqNotePadNote(k), DKCYAN);
			if (recLatched_)
				strip.setPixelColor(0, omxLeds.getSlowBlinkState() ? RED : PURPLE); // AUX blinks red while Record is latched
			for (uint8_t k = 11; k <= 26; k++)
				drawPaletteKey(k, seqSlotNote(k), LOWWHITE);
		}
		else // VIEW_NOTE
		{
			strip.setPixelColor(3, trackHeld_ ? WHITE : RED);       // Track (hold)
			strip.setPixelColor(9, ledColor_[10] != 0 ? RED : DKRED); // Edit / Rec
			strip.setPixelColor(10, ledColor_[20] != 0 ? GREEN : RED); // Play

			for (uint8_t k = 11; k <= 26; k++)
			{
				uint8_t note = (trackHeld_ && k <= 18) ? (uint8_t)(101 + (k - 11)) : seqSlotNote(k);
				drawPaletteKey(k, note, (trackHeld_ && k <= 18) ? DKRED : LOWWHITE);
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
			// Main page: encoder = LPP Scroll Up/Down, same as keys 2/1.
			if (enc.dir() != 0)
				sendLppTap(enc.dir() > 0 ? 80 : 70);
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
			else if (param == 2)
			{
				releaseAllKeys(); // never leave a note held while the message type changes
				releaseLatchedTracks();
				ringAsCC_ = !ringAsCC_;
				omxDisp.displayMessageTimed(ringAsCC_ ? "Ring: CC" : "Ring: Note", 5);
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
			omxDisp.setLegend(2, "RING", !ringAsCC_, ringAsCC_ ? "CC" : "NOTE");
			omxDisp.dispGenericMode2(params_.getNumPages(), params_.getSelPage(), params_.getSelParam(), encoderSelect_);
			return;
		}

		const char *modeName = "CLIP";
		if (padMode_ == PAD_MUTE)
			modeName = "MUTE";
		else if (padMode_ == PAD_SOLO)
			modeName = "SOLO";

		if (view_ == VIEW_SEQ)
			snprintf(dispLabel_, sizeof(dispLabel_), recLatched_ ? "M8 SEQ REC" : "M8 SEQ");
		else if (view_ == VIEW_NOTE)
			snprintf(dispLabel_, sizeof(dispLabel_), trackHeld_ ? "M8 NOTE TRK" : "M8 NOTE");
		else
			snprintf(dispLabel_, sizeof(dispLabel_), "M8 SESS R%d %s", (int)row_, modeName);

		if (linked_)
			snprintf(dispStatus_, sizeof(dispStatus_), "LPP LINK");
		else // sent/received counters help debug a host that never asks or never answers
			snprintf(dispStatus_, sizeof(dispStatus_), "WAIT S%u R%u", (unsigned)(s_identitySent % 100), (unsigned)(s_inquiryRx % 100));

		omxDisp.dispGenericModeLabelDoubleLine(dispLabel_, dispStatus_, params_.getNumPages(), params_.getSelPage());
	}

#endif // !OMX_M8_MACRO_LEGACY
}
