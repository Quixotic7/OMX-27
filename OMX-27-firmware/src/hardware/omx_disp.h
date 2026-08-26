#pragma once
#include "../config.h"
#include <elapsedMillis.h>

// MESSAGE DISPLAY
const int MESSAGE_TIMEOUT_US = 500000;

class OmxDisp
{
public:
	// Should make into function

	const char *legends[4] = {"", "", "", ""};
	int legendVals[4] = {0, 0, 0, 0};
	int dispPage = 0;
	const char *legendText[4] = {"", "", "", ""};
	bool useLegendString[4] = {false, false, false, false};
	String legendString[4] = {"12345", "12345", "12345", "12345"};

	OmxDisp();
	void setup();
	void clearDisplay();
	void drawStartupScreen();
	void displayMessage(String msg);
	void displayMessage(const char *msg);
	void displayMessagef(const char *fmt, ...);
	void displayMessageTimed(String msg, uint8_t secs);
	void displaySpecialMessage(uint8_t msgType, String msg, uint8_t secs);

	bool isMessageActive();
	bool isDispLocked();

	void dispGridBoxes();
	void invertColor(bool flip);
	void dispValBox(int v, int16_t n, bool inv);
	void dispSymbBox(const char *v, int16_t n, bool inv);
	void dispGenericMode(int selected);

	void dispGenericMode2(uint8_t numPages, int8_t selectedPage, int8_t selectedParam, bool encSelActive);

	// Displays a label and page numbers
	void dispGenericModeLabel(const char *label, uint8_t numPages, int8_t selectedPage);
	void dispGenericModeLabelDoubleLine(const char *label1, const char *label2, uint8_t numPages, int8_t selectedPage);
	// Held-modifier split view: top label + a row of topCount boxes (top keys) over a row
	// of bottomCount boxes (bottom keys) + bottom label. Boxes are filled to show state.
	// Pass nullptr for a fill array to draw empty outlines; pass count 0 to omit that row
	// (single-row layout).
	void dispKeyFunctionSplit(const char *topLabel, const bool *topFill, uint8_t topCount,
							  const char *bottomLabel, const bool *bottomFill, uint8_t bottomCount);
	// Held-track status (Mix): "TRACK n" with M / S cells (filled when active) and a
	// play-direction icon (playModeIndex 0-4 = fwd/rev/fwd-pong/rev-pong/random).
	void dispTrackHold(uint8_t trackNum, bool muted, bool soloed, uint8_t playModeIndex);
	// Step menu param page: 4 cells (label + value). A locked cell's label is inverted. The
	// selected cell is boxed when navigating; when `editing` (holding a step) it is fully
	// inverted to show the encoder is locked onto it.
	void dispStepParams(const char *labels[4], const char *values[4], const bool locked[4], uint8_t sel, bool editing);
	// Step view Note hold: a compact piano keyboard (chord = notesAsKeys[6]) on top, with the
	// 16 step-marker cells beneath (filled = has content; `focus` step gets a tick). No text.
	void dispStepNoteKeyboard(int8_t notesAsKeys[6], const bool *filled, int8_t focus);
	// Step view overview: mode name on top, a row of `count` step cells on the bottom (filled
	// = has content). The playhead step (0-based, -1 = none) gets a tick underneath.
	void dispStepOverview(const char *modeName, const bool *filled, uint8_t count, int8_t playhead);
	// Mix F3 (LEN | RATE): rate label on top, track length as a 16-cell bar on the bottom.
	// activeCount cells (0-16) are full boxes (steps within the length); the rest are dashes.
	// Every 4th active cell gets a left notch to make groups of 4 easier to count.
	void dispTrackLength(const char *rateStr, uint8_t activeCount);
	void dispGenericModeLabelSmallText(const char *label, uint8_t numPages, int8_t selectedPage);

	// Displays a header and options below
	// Good for something like a yes/no box
	void dispOptionCombo(const char * header, const char *options[], uint8_t optionCount, uint8_t selected, bool encSelActive);

	void dispChar16(const char *charArray[], uint8_t charCount, uint8_t selected, uint8_t numPages, int8_t selectedPage, bool encSelActive, bool showLabels, const char *labels[], uint8_t labelCount);

	// Renders values as bars
	void dispValues16(int8_t valueArray[], uint8_t valueCount, int8_t minValue, int8_t maxValue, bool centered, uint8_t selected, uint8_t numPages, int8_t selectedPage, bool encSelActive, bool showLabels, const char *labels[], uint8_t labelCount);

	void dispParamBar(int8_t potValue, int8_t targetValue, int8_t minValue, int8_t maxValue, bool pickedUp, bool centered, const char *bankName, const char *paramName);

	void dispPickupBarLabelTimed(const char *label, int8_t potValue, int8_t targetValue, int8_t minValue, int8_t maxValue, bool pickedUp, bool centered);
	void dispPickupBarValueTimed(int8_t dispValue, int8_t potValue, int8_t targetValue, int8_t minValue, int8_t maxValue, bool pickedUp, bool centered, const char *bankName, const char *paramName);

	void drawPotPickupBar(int8_t potValue, int8_t targetValue, int8_t minValue, int8_t maxValue, bool pickedUp, bool centered);

	// Displays slots for midifx or something else in future
	void dispSlots(const char *slotNames[], uint8_t slotCount, uint8_t selected, uint8_t animPos, bool encSelActive, bool showLabels, const char *labels[], uint8_t labelCount);

	// Displays multiple slots up to slotCount all centered
	void dispCenteredSlots(const char *slotNames[], uint8_t slotCount, uint8_t selected, bool encoderSelect, bool showLabels, bool centerLabels, const char *labels[], uint8_t labelCount);
	void dispCenteredSlots(const uint8_t *slotFont, const char *slotNames[], uint8_t slotCount, uint8_t selected, bool encoderSelect, bool showLabels, bool centerLabels, const char *labels[], uint8_t labelCount);


	// noteNumbers should be array of 6
	void dispSeqKeyboard(int8_t notesAsKeys[], bool showLabels, const char *labels[], uint8_t labelCount);
	void dispKeyboard(int rootNote, int noteNumbers[], bool showLabels, const char *labels[], uint8_t labelCount);
	void drawKeyboard(bool blackNotes[10], bool whiteNotes[16]);

	void dispChordBasicPage(uint8_t selected, bool encoderSelect, const char *noteName, const char *octaveName, const char *chordType, int8_t balArray[], float velArray[]);
	void chordBalanceMsg(int8_t balArray[], float velArray[], uint8_t secs);

	void dispLabelParams(int8_t selected, bool encSelActive, const char *labels[], uint8_t labelCount, bool centered);

	void dispPageIndicators(int page, bool selected);
	void dispPageIndicators2(uint8_t numPages, int8_t selected);
	void dispMode();

	void testdrawrect();
	void drawLoading();

	void setDirty();
	bool isDirty() { return dirtyDisplay; }

	bool canShowDisplay();
	void showDisplay();
	void forceShowDisplay();

	void bumpDisplayTimer();

	void clearLegends();
	void setLegend(uint8_t index, const char* label, int value);
	void setLegend(uint8_t index, const char* label, bool isOff, int value);
	void setLegend(uint8_t index, const char* label, const char* text);
	void setLegend(uint8_t index, const char* label, bool isOff, const char* text);
	void setLegend(uint8_t index, const char* label, String text);
	void setLegend(uint8_t index, const char* label, bool isOff, String text);
	void setLegend(uint8_t index, const char* label, bool value);


	void setSubmode(int submode);

	void UpdateMessageTextTimer();

	void drawEuclidPattern(bool singleView, bool *pattern, uint8_t steps, uint8_t yPos, bool selected, bool isPlaying, uint8_t seqPos);

private:
	int hline = 8;
	int messageTextTimer = 0;
	int dispLockedTimer = 0;

	bool dirtyDisplay = false;

	uint8_t animPos = 0;
	int animTimer = 0;

	String currentMsg;
	uint8_t specialMsgType_ = 0;

	int8_t chordBalArray_[4];
	float chordVelArray_[4];

	elapsedMillis dirtyDisplayTimer = 0;
	unsigned long displayRefreshRate = 60;

	void dispParamLabel(uint8_t x, uint8_t y, uint8_t width, uint8_t height, bool selected, uint8_t selectionType, bool encSelActive, bool showLabel, const char *label, const uint8_t *font, int8_t labelYOffset, bool centered);

	void u8g2centerText(const char *s, int16_t x, int16_t y, uint16_t w, uint16_t h);
	void u8g2leftText(const char *s, int16_t x, int16_t y, uint16_t w, uint16_t h);
	void u8g2centerNumber(int n, uint16_t x, uint16_t y, uint16_t w, uint16_t h);
	void renderMessage();

	void dispChordBalance();

	bool validateLegendIndex(uint8_t index);
};

extern OmxDisp omxDisp;
