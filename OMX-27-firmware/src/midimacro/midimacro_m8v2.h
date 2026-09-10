#pragma once
#include "midimacro_interface.h"

// M8V2 - Launchpad Pro MK3 emulation for the Dirtywave M8.
// See design/m8v2/M8V2-PLAN.md. Phase 1 = skeleton + handshake.
//
// The whole class is compiled only when the legacy macro is NOT selected.
// Define OMX_M8_MACRO_LEGACY in config.h to build the old MidiMacroM8 instead.

namespace midimacro
{
	// Universal Device Inquiry hook, called from sysex.cpp BEFORE the F0 7D 00 00 gate.
	// Replies with the Launchpad Pro MK3 identity whenever the M8 macro slot (1) is
	// selected, regardless of whether the macro is currently entered or which OMX mode
	// is running. Compiled to a no-op in the legacy build.
	void onDeviceInquiry(const uint8_t *data, unsigned length);

#ifndef OMX_M8_MACRO_LEGACY

	class MidiMacroM8V2 : public MidiMacroInterface
	{
	public:
		enum View : uint8_t
		{
			VIEW_SESSION,
			VIEW_NOTE
		};

		enum PadMode : uint8_t
		{
			PAD_CLIP,
			PAD_MUTE,
			PAD_SOLO
		};

		MidiMacroM8V2();
		~MidiMacroM8V2() {}

		bool consumesPots() override { return true; }
		bool consumesDisplay() override { return true; }

		String getName() override;

		void loopUpdate() override;

		void onDisplayUpdate() override;

		void onPotChanged(int potIndex, int prevValue, int newValue, int analogDelta) override;
		void onKeyUpdate(OMXKeypadEvent e) override;
		void drawLEDs() override;

		void inMidiControlChange(byte channel, byte control, byte value) override;
		bool inMidiNoteOn(byte channel, byte note, byte velocity) override;
		bool inMidiNoteOff(byte channel, byte note, byte velocity) override;

	protected:
		void onEnabled() override;
		void onDisabled() override;

		void onEncoderChangedEditParam(Encoder::Update enc) override;

	private:
		// Highest LPP programmer note we cache (grid tops out at 98, ring at 108 but
		// the track row 101-108 is inside this range too).
		static const uint8_t kNumLppNotes = 110;
		static const uint8_t kNumKeys = 27;

		// Maps an OMX key to the Launchpad Pro MK3 programmer note it sends.
		// Depends on view_ and padMode_. Returns 0 for keys that send nothing
		// (local-only actions like row scroll/pad-mode select, or the Option placeholder).
		uint8_t lppNoteForKey(uint8_t key);

		void sendIdentity();
		void releaseAllKeys();
		void scrollRow(int8_t dir);
		void setPadMode(PadMode newMode);

		// Draws one grid/scene key from the palette cache: static/flash/pulse per ledMode_,
		// offColor when the cached index is 0 (or the note is out of range).
		void drawPaletteKey(uint8_t key, uint8_t note, uint32_t offColor);

		View view_ = VIEW_SESSION;
		PadMode padMode_ = PAD_CLIP;
		bool muteLatch_ = false;
		bool soloLatch_ = false;
		uint8_t row_ = 8; // 1..8, which grid row keys 11-18 show (11-7 in Note view)

		bool linked_ = false;
		uint32_t lastIdentityMs_ = 0;
		bool auxHeld_ = false;

		// Last seen blink phases, so loopUpdate() only calls omxLeds.setDirty() once per
		// actual blink transition (and only when something cached is actually flash/pulse).
		bool lastBlinkState_ = false;
		bool lastSlowBlinkState_ = false;

		uint8_t ledColor_[kNumLppNotes]; // LPP palette index by LPP note
		uint8_t ledMode_[kNumLppNotes];	 // 0 static, 1 flash, 2 pulse
		uint8_t keyNoteSent_[kNumKeys];	 // LPP note currently held per OMX key, 0 = none

		// Persistent display buffers - never pass String(...).c_str() of a temporary.
		char dispLabel_[24];
		char dispStatus_[16];
	};

#endif // !OMX_M8_MACRO_LEGACY
}
