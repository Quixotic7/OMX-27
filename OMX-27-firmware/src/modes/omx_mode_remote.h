#pragma once

#include "omx_mode_interface.h"

// ---------------------------------------------------------------------------
// REMOTE mode - the OMX-27 as a dumb-terminal controller for a host script
// (monome norns lua, or the PC test tool), the way a monome grid serves norns.
//
// While active:
//   - Every key event is sent to the host:      INPUT 0x00 [key, z]
//   - Raw encoder deltas are sent:              INPUT 0x01 [dir(0=CCW,2=CW), amt]
//   - Encoder button up/down is sent:           INPUT 0x02 [z]
//   - Pot changes are sent:                     INPUT 0x03 [pot, v7, hi7, lo7]
//   - The host owns all 27 LEDs (staged writes latched by LED_SHOW) and the
//     whole 128x32 screen (raw framebuffer chunks latched by DRAW_UPD).
//
// The normal encoder-long-press mode-select is blocked so a script can use
// button holds; hold AUX (key 0) and click the encoder to open mode select.
// Until the first host frame arrives the screen shows a "REMOTE" splash, and
// whatever the host last drew/lit simply stays put (no heartbeat/timeout).
// ---------------------------------------------------------------------------
class OmxModeRemote : public OmxModeInterface
{
public:
	OmxModeRemote() {}
	~OmxModeRemote() {}

	void onModeActivated() override;
	void onModeDeactivated() override;

	void onPotChanged(int potIndex, int prevValue, int newValue, int analogDelta) override;
	void updateLEDs() override;
	void onEncoderChanged(Encoder::Update enc) override;
	void onEncoderButtonDown() override;
	void onEncoderButtonUp() override;
	void onEncoderButtonUpLong() override;
	bool shouldBlockEncEdit() override { return true; }
	void onEncoderButtonDownLong() override;
	void onKeyUpdate(OMXKeypadEvent e) override;
	void onDisplayUpdate() override;

	// Incoming host SysEx (routed from sysex.cpp when this mode is active).
	// d points at the full message starting at 0xF0; d[4] is the command byte.
	void onSysex(const uint8_t *d, unsigned len);

private:
	static const uint8_t kNumLeds = 27;
	static const uint8_t kChunkCount = 16;
	static const uint16_t kChunkBytes = 32;
	static const uint16_t kFrameBytes = kChunkCount * kChunkBytes; // 512

	bool auxHeld_ = false;
	bool ebtnDown_ = false; // dedupe Down vs DownLong

	// Pot jitter suppression
	int16_t lastPotV_[5] = {-1, -1, -1, -1, -1};
	uint16_t lastPotHiRes_[5] = {0};

	// LEDs: hosts stage writes, LED_SHOW latches them (grid-style refresh)
	uint32_t ledStage_[kNumLeds] = {0};
	uint32_t ledLive_[kNumLeds] = {0};

	// Screen: DRAW chunks stage into frameStage_, DRAW_UPD latches
	uint8_t frameStage_[kFrameBytes] = {0};
	uint8_t frameLive_[kFrameBytes] = {0};
	bool frameReceived_ = false;

	void enterModeSelect();
	void sendInput2(uint8_t sub, uint8_t a, uint8_t b);
	void sendInput(uint8_t sub, const uint8_t *args, uint8_t argLen);
	void sendStatus(uint8_t status);
};
