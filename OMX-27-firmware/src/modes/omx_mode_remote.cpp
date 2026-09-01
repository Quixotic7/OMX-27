#include "omx_mode_remote.h"
#include "../config.h"
#include "../globals.h"
#include "../consts/colors.h"
#include "../hardware/omx_disp.h"
#include "../hardware/omx_leds.h"
#include "../midi/midi.h"
#include "../midi/norns_link.h"
#include "../ClearUI/ClearUI_Display.h"
#include "omx_screensaver.h"

extern OmxScreensaver omxScreensaver;

// STATUS payloads (NL_CMD_STATUS): 0x01 = activity (NornsLink), plus:
static const uint8_t kStatusRemoteOn = 0x02;
static const uint8_t kStatusRemoteOff = 0x03;

// Decode the mirror-format 7-bit stream (hi-bits byte then up to 7 data bytes)
// into dst[dstLen]. Returns true if the source held enough data.
static bool decode7bit(const uint8_t *src, uint16_t srcLen, uint8_t *dst, uint16_t dstLen)
{
	uint16_t si = 0;
	for (uint16_t i = 0; i < dstLen; i += 7)
	{
		uint8_t n = (uint8_t)((dstLen - i) < 7 ? (dstLen - i) : 7);
		if (si + 1 + n > srcLen)
			return false;
		uint8_t hi = src[si++];
		for (uint8_t j = 0; j < n; j++)
			dst[i + j] = (uint8_t)((src[si++] & 0x7F) | (((hi >> j) & 0x01) << 7));
	}
	return true;
}

void OmxModeRemote::onModeActivated()
{
	auxHeld_ = false;
	ebtnDown_ = false;
	sendStatus(kStatusRemoteOn);
	omxDisp.setDirty();
	omxLeds.setDirty();
}

void OmxModeRemote::onModeDeactivated()
{
	sendStatus(kStatusRemoteOff);
}

// ---------------------------------------------------------------------------
// Input -> host
// ---------------------------------------------------------------------------
void OmxModeRemote::onKeyUpdate(OMXKeypadEvent e)
{
	uint8_t key = (uint8_t)e.key();
	if (key == 0)
		auxHeld_ = e.down();

	sendInput2(0x00, key, e.down() ? 1 : 0);
}

void OmxModeRemote::onEncoderChanged(Encoder::Update enc)
{
	int amt = enc.accel(1);
	if (amt == 0)
		return;
	sendInput2(0x01, amt < 0 ? 0 : 2, (uint8_t)constrain(abs(amt), 1, 127));
}

void OmxModeRemote::onEncoderButtonDown()
{
	if (auxHeld_)
	{
		enterModeSelect();
		return;
	}
	if (!ebtnDown_)
	{
		ebtnDown_ = true;
		uint8_t z = 1;
		sendInput(0x02, &z, 1);
	}
}

void OmxModeRemote::onEncoderButtonDownLong()
{
	// Down was already reported; the host does its own hold timing.
	onEncoderButtonDown();
}

void OmxModeRemote::onEncoderButtonUp()
{
	if (ebtnDown_)
	{
		ebtnDown_ = false;
		uint8_t z = 0;
		sendInput(0x02, &z, 1);
	}
}

void OmxModeRemote::onEncoderButtonUpLong()
{
	onEncoderButtonUp();
}

void OmxModeRemote::onPotChanged(int potIndex, int prevValue, int newValue, int analogDelta)
{
	if (potIndex < 0 || potIndex >= 5)
		return;
	uint16_t hiRes = (uint16_t)constrain(potSettings.hiResPotVal[potIndex], 0, 16383);

	// The analog smoother oscillates between adjacent values at rest; only
	// forward real movement (7-bit change, or a hi-res step past the jitter).
	if (newValue == lastPotV_[potIndex] &&
		(uint16_t)abs((int)hiRes - (int)lastPotHiRes_[potIndex]) < 32)
		return;
	lastPotV_[potIndex] = (int16_t)newValue;
	lastPotHiRes_[potIndex] = hiRes;
	uint8_t args[4];
	args[0] = (uint8_t)potIndex;
	args[1] = (uint8_t)constrain(newValue, 0, 127);
	args[2] = (uint8_t)((hiRes >> 7) & 0x7F);
	args[3] = (uint8_t)(hiRes & 0x7F);
	sendInput(0x03, args, 4);
}

void OmxModeRemote::enterModeSelect()
{
	encoderConfig.enc_edit = true;
	sysSettings.newmode = sysSettings.omxMode;
	omxLeds.setAllLEDS(0, 0, 0);
	omxDisp.setDirty();
	omxLeds.setDirty();
}

// ---------------------------------------------------------------------------
// Host -> OMX (LEDs + screen)
// ---------------------------------------------------------------------------
void OmxModeRemote::onSysex(const uint8_t *d, unsigned len)
{
	// d: F0 7D 00 00 <cmd> <payload...> F7
	if (len < 6)
		return;
	const uint8_t cmd = d[4];
	const uint8_t *p = &d[5];
	const unsigned plen = len - 6; // strip header + trailing F7

	switch (cmd)
	{
	case NL_CMD_LED: // [idx, r, g, b] (7-bit channels, scaled x2)
		if (plen >= 4 && p[0] < kNumLeds)
		{
			ledStage_[p[0]] = strip.Color((uint8_t)(p[1] << 1), (uint8_t)(p[2] << 1), (uint8_t)(p[3] << 1));
		}
		break;
	case NL_CMD_LED_BATCH: // [start, count, r,g,b × count]
		if (plen >= 2)
		{
			uint8_t start = p[0];
			uint8_t count = p[1];
			if (start < kNumLeds && plen >= (unsigned)(2 + count * 3))
			{
				for (uint8_t i = 0; i < count && (start + i) < kNumLeds; i++)
				{
					const uint8_t *c = &p[2 + i * 3];
					ledStage_[start + i] = strip.Color((uint8_t)(c[0] << 1), (uint8_t)(c[1] << 1), (uint8_t)(c[2] << 1));
				}
			}
		}
		break;
	case NL_CMD_LED_SHOW: // latch staged LEDs
		memcpy(ledLive_, ledStage_, sizeof(ledLive_));
		omxLeds.setDirty();
		break;
	case NL_CMD_DRAW: // [chunk(0-15)] [7bit-encoded 32B]
		if (plen >= 1 && p[0] < kChunkCount)
		{
			decode7bit(&p[1], (uint16_t)(plen - 1), &frameStage_[(uint16_t)p[0] * kChunkBytes], kChunkBytes);
		}
		break;
	case NL_CMD_DRAW_UPD: // latch staged frame
		memcpy(frameLive_, frameStage_, sizeof(frameLive_));
		frameReceived_ = true;
		omxDisp.setDirty();
		omxScreensaver.resetCounter(); // host is driving; keep the panel awake
		break;
	default:
		break;
	}
}

// ---------------------------------------------------------------------------
// Rendering
// ---------------------------------------------------------------------------
void OmxModeRemote::updateLEDs()
{
	for (uint8_t i = 0; i < kNumLeds; i++)
	{
		strip.setPixelColor(i, ledLive_[i]);
	}
}

void OmxModeRemote::onDisplayUpdate()
{
	if (omxLeds.isDirty())
	{
		updateLEDs();
	}

	if (encoderConfig.enc_edit)
		return; // mode-select menu owns the screen

	if (!omxDisp.isDirty())
		return;

	if (!frameReceived_)
	{
		omxDisp.dispGenericModeLabel("REMOTE", 0, 0);
		return;
	}

	// Host framebuffer -> OLED, bypassing the text renderer. The main loop
	// cleared the buffer before calling us; showDisplay() pushes it after.
	uint8_t *fb = display.getBuffer();
	if (fb != nullptr)
	{
		memcpy(fb, frameLive_, kFrameBytes);
	}
}

// ---------------------------------------------------------------------------
// SysEx senders (same wire format as NornsLink)
// ---------------------------------------------------------------------------
void OmxModeRemote::sendInput2(uint8_t sub, uint8_t a, uint8_t b)
{
	uint8_t args[2] = {a, b};
	sendInput(sub, args, 2);
}

void OmxModeRemote::sendInput(uint8_t sub, const uint8_t *args, uint8_t argLen)
{
	uint8_t buf[5 + 8];
	buf[0] = 0x7D;
	buf[1] = 0x00;
	buf[2] = 0x00;
	buf[3] = NL_CMD_INPUT;
	buf[4] = sub;
	for (uint8_t i = 0; i < argLen && i < 8; i++)
		buf[5 + i] = args[i] & 0x7F;
	MM::sendSysExUSB((uint32_t)(5 + argLen), buf, false);
}

void OmxModeRemote::sendStatus(uint8_t status)
{
	uint8_t buf[5];
	buf[0] = 0x7D;
	buf[1] = 0x00;
	buf[2] = 0x00;
	buf[3] = NL_CMD_STATUS;
	buf[4] = status;
	MM::sendSysExUSB(5, buf, false);
}
