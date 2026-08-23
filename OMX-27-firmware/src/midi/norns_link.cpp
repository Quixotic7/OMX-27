#include "norns_link.h"

#include <string.h>

#include "midi.h"
#include "../ClearUI/ClearUI_Display.h" // extern Adafruit_SSD1306 display

NornsLink nornsLink;

// How often we look for display changes to send (low, since it's cheap + delta'd).
static const uint32_t kMinSendIntervalMicros = 4000;
// (gap between chunk messages is chunkIntervalMicros_, tunable via NL_CMD_PACE)

// Encode src[len] (8-bit) into dst as 7-bit-safe SysEx data (32 bytes -> 37).
static uint16_t encode7bit(const uint8_t *src, uint16_t len, uint8_t *dst)
{
	uint16_t di = 0;
	for (uint16_t i = 0; i < len; i += 7)
	{
		uint8_t n = (uint8_t)((len - i) < 7 ? (len - i) : 7);
		uint8_t hi = 0;
		for (uint8_t j = 0; j < n; j++)
			hi |= (uint8_t)(((src[i + j] >> 7) & 0x01) << j);
		dst[di++] = hi;
		for (uint8_t j = 0; j < n; j++)
			dst[di++] = (uint8_t)(src[i + j] & 0x7F);
	}
	return di;
}

// 7-bit checksum of the raw chunk bytes (norns verifies; mismatch -> reject+REQ).
static uint8_t checksum7(const uint8_t *data, uint16_t len)
{
	uint16_t sum = 0;
	for (uint16_t i = 0; i < len; i++)
		sum += data[i];
	return (uint8_t)(sum & 0x7F);
}

void NornsLink::setMirrorEnabled(bool enabled)
{
	mirrorOn_ = enabled;
	if (enabled)
	{
		haveLastSent_ = false; // force a full frame on (re)enable
		pendingMask_ = 0;
		passMask_ = 0;
		forceMask_ = 0;
		resendEnd_ = false;
	}
}

void NornsLink::requestChunks(uint16_t mask)
{
	if (mask == 0)
		resendEnd_ = true;
	else
		forceMask_ |= mask;
}

void NornsLink::sendChunk(uint8_t chunk, const uint8_t *data)
{
	// 7D 00 00 <FRAME> <chunk> <fid> <cksum> <encoded...>   (F0/F7 added by sendSysExUSB)
	uint8_t buf[7 + 48];
	buf[0] = 0x7D;
	buf[1] = 0x00;
	buf[2] = 0x00;
	buf[3] = NL_CMD_FRAME;
	buf[4] = chunk;
	buf[5] = fid_;
	buf[6] = checksum7(data, kChunkBytes);
	uint16_t encLen = encode7bit(data, kChunkBytes, &buf[7]);
	MM::sendSysExUSB((uint32_t)(7 + encLen), buf, false);
}

void NornsLink::sendFrameEnd(uint8_t fid, uint16_t mask)
{
	uint8_t buf[8];
	buf[0] = 0x7D;
	buf[1] = 0x00;
	buf[2] = 0x00;
	buf[3] = NL_CMD_FRAME_END;
	buf[4] = fid & 0x7F;
	buf[5] = (uint8_t)(mask & 0x7F);		 // chunks 0-6
	buf[6] = (uint8_t)((mask >> 7) & 0x7F);  // chunks 7-13
	buf[7] = (uint8_t)((mask >> 14) & 0x03); // chunks 14-15
	MM::sendSysExUSB(8, buf, false);
	lastEndMask_ = mask;
}

void NornsLink::streamFrame()
{
	if (!mirrorOn_)
		return;
	if (pendingMask_ != 0)
		return; // still sending the previous pass

	uint32_t now = micros();
	if ((uint32_t)(now - lastSendMicros_) < kMinSendIntervalMicros)
		return;

	const uint8_t *fb = display.getBuffer();
	if (fb == nullptr)
		return;

	uint16_t changed = 0;
	for (uint8_t c = 0; c < kChunkCount; c++)
	{
		const uint8_t *s = fb + (uint16_t)c * kChunkBytes;
		const uint8_t *l = lastSent_ + (uint16_t)c * kChunkBytes;
		if (!haveLastSent_ || memcmp(s, l, kChunkBytes) != 0)
			changed |= (uint16_t)(1u << c);
	}
	if (!haveLastSent_)
		changed = 0xFFFF;

	if (changed == 0)
	{
		lastSendMicros_ = now;
		return; // nothing new; pump() services REQ resends on its own
	}

	memcpy(snapshot_, fb, kFrameBytes);
	fid_ = (uint8_t)((fid_ + 1) & 0x7F); // new content -> new frame id
	uint16_t mask = changed | forceMask_; // merge outstanding resend requests
	forceMask_ = 0;
	pendingMask_ = mask;
	passMask_ = mask;
	lastSendMicros_ = now;
}

void NornsLink::pump()
{
	if (!mirrorOn_)
	{
		pendingMask_ = 0;
		forceMask_ = 0;
		resendEnd_ = false;
		return;
	}

	uint32_t now = micros();
	if ((uint32_t)(now - lastChunkMicros_) < chunkIntervalMicros_)
		return;

	// Resend-only pass (norns missed chunks of the last frame): same fid, from
	// the same snapshot, so norns can complete that frame atomically.
	if (pendingMask_ == 0 && forceMask_ != 0)
	{
		pendingMask_ = forceMask_;
		passMask_ = forceMask_;
		forceMask_ = 0;
	}
	if (pendingMask_ == 0 && resendEnd_)
	{
		resendEnd_ = false;
		sendFrameEnd(fid_, lastEndMask_);
		lastChunkMicros_ = now;
		return;
	}
	if (pendingMask_ == 0)
		return;

	uint8_t c = 0;
	while (c < kChunkCount && !(pendingMask_ & (uint16_t)(1u << c)))
		c++;
	if (c >= kChunkCount)
	{
		pendingMask_ = 0;
		return;
	}

	const uint8_t *chunkData = snapshot_ + (uint16_t)c * kChunkBytes;
	sendChunk(c, chunkData);
	memcpy(lastSent_ + (uint16_t)c * kChunkBytes, chunkData, kChunkBytes);
	pendingMask_ &= (uint16_t)~(1u << c);
	lastChunkMicros_ = now;

	if (pendingMask_ == 0)
	{
		haveLastSent_ = true;
		sendFrameEnd(fid_, passMask_);
	}
}

void NornsLink::markActivity()
{
	if (!mirrorOn_)
		return;
	uint32_t now = micros();
	if ((uint32_t)(now - lastActivitySendMicros_) < 100000) // rate-limit ~10/s
		return;
	lastActivitySendMicros_ = now;
	uint8_t buf[5];
	buf[0] = 0x7D;
	buf[1] = 0x00;
	buf[2] = 0x00;
	buf[3] = NL_CMD_STATUS;
	buf[4] = 0x01; // user input activity
	MM::sendSysExUSB(5, buf, false);
}
