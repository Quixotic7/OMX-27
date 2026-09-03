#pragma once

#include <Arduino.h>

// ---------------------------------------------------------------------------
// NornsLink - OMX-27 <-> monome norns bridge over USB MIDI SysEx.
//
// Phase 1: screen mirror. The 128x32 OLED framebuffer (512 bytes) is sent in
// 32-byte CHUNKS (46-byte SysEx ~= 64 bytes of the 128-byte TinyUSB TX FIFO, so
// a message always fits with headroom for clock traffic). Only CHANGED chunks
// are sent (delta), paced so norns' per-packet MIDI RX keeps up. Every chunk
// carries a frame id + checksum and each pass ends with FRAME_END, so norns can
// present a frame atomically and REQUEST resends of anything it missed.
//
// Wire: F0 7D 00 00 <cmd> <payload...> F7  (F0/F7 added by sendSysEx*, 7-bit data)
// 16-chunk masks are sent as 3 bytes: [bits0-6][bits7-13][bits14-15].
// ---------------------------------------------------------------------------

// OMX -> norns
static const uint8_t NL_CMD_FRAME = 0x50;     // chunk: [chunk(0-15)] [fid7] [cksum7] [7bit-encoded 32B]
static const uint8_t NL_CMD_INPUT = 0x51;     // NT input event (phase 2)
static const uint8_t NL_CMD_STATUS = 0x52;    // status: [0x01]=user input activity
static const uint8_t NL_CMD_FRAME_END = 0x53; // pass complete: [fid7] [mask x3] -> norns presents atomically
static const uint8_t NL_CMD_LED_STATE = 0x54; // reply to a host LED-state query: [part 0/1] [r g b ... 7-bit per LED]

// norns -> OMX
static const uint8_t NL_CMD_MIRROR_EN = 0x58; // [0/1] enable screen-mirror streaming
static const uint8_t NL_CMD_LED = 0x59;       // NT LED set (phase 2)
static const uint8_t NL_CMD_LED_BATCH = 0x5A; // NT LED batch (phase 2)
static const uint8_t NL_CMD_LED_SHOW = 0x5B;  // NT LED latch (phase 2)
static const uint8_t NL_CMD_DRAW = 0x5C;      // NT screen draw batch (phase 2)
static const uint8_t NL_CMD_DRAW_UPD = 0x5D;  // NT screen flush (phase 2)
static const uint8_t NL_CMD_REQ = 0x5E;       // [mask x3] resend these chunks; all-zero = resend last FRAME_END
static const uint8_t NL_CMD_PACE = 0x5F;      // [ms 1-127] gap between chunk messages (runtime tunable from norns)

class NornsLink
{
public:
	void setMirrorEnabled(bool enabled);
	bool mirrorEnabled() const { return mirrorOn_; }

	// Capture the current display and queue changed chunks. Called from
	// OmxDisp::showDisplay() right after the buffer is pushed to the OLED.
	void streamFrame();

	// Send at most one queued chunk. Call every main-loop iteration.
	void pump();

	// norns asked for these chunks again (it missed/rejected them); mask 0 =
	// it missed the FRAME_END, resend that.
	void requestChunks(uint16_t mask);

	// Reply to a host LED-state query (NL_CMD_LED_STATE 0x54): dump all 27 keypad LEDs'
	// current RGB (each channel 7-bit) in two SysEx parts (LEDs 0-12, then 13-26). Lets a
	// host verify the RGB LED state the OLED mirror can't show.
	void sendLedState();

	// Signal user input to norns (for the "auto" mirror mode). Rate-limited.
	void markActivity();

	// Gap between chunk messages, in ms (norns tunes this at runtime).
	void setPaceMs(uint8_t ms) { chunkIntervalMicros_ = (uint32_t)(ms < 1 ? 1 : ms) * 1000UL; }

private:
	static const uint8_t kChunkCount = 16;						  // 512-byte frame / 32
	static const uint16_t kChunkBytes = 32;						  // 46-byte SysEx: fits the 128B FIFO with room
	static const uint16_t kFrameBytes = kChunkCount * kChunkBytes; // 512

	bool mirrorOn_ = false;
	uint32_t chunkIntervalMicros_ = 15000;
	uint32_t lastSendMicros_ = 0;
	uint32_t lastChunkMicros_ = 0;
	uint32_t lastActivitySendMicros_ = 0;

	uint8_t snapshot_[kFrameBytes]; // frame captured for this send pass
	uint8_t lastSent_[kFrameBytes]; // last chunk data actually sent (for delta)
	bool haveLastSent_ = false;
	uint16_t pendingMask_ = 0; // bit c set => chunk c queued to send
	uint16_t passMask_ = 0;    // all chunks in the current pass (for FRAME_END)
	uint16_t forceMask_ = 0;   // bit c set => norns requested chunk c (resend)
	bool resendEnd_ = false;   // norns asked for the last FRAME_END again
	uint8_t fid_ = 0;          // frame id of the current/last pass
	uint16_t lastEndMask_ = 0; // mask reported in the last FRAME_END

	void sendChunk(uint8_t chunk, const uint8_t *data);
	void sendFrameEnd(uint8_t fid, uint16_t mask);
};

extern NornsLink nornsLink;
