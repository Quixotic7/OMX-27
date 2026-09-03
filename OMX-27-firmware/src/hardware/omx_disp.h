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
	// Step menu param page: 4 cells (label + value). A locked cell's label is inverted. The
	// selected cell is boxed when navigating; when `editing` (holding a step) it is fully
	// inverted to show the encoder is locked onto it.
	void dispStepParams(const char *labels[4], const char *values[4], const bool locked[4], uint8_t sel, bool editing);
	// Like dispStepParams but a 5-cell grid (e.g. the track-aware SCALE page: Mode/Root/
	// Scale/Lock/Group). dimmed[i] greys a cell whose param is inactive in the current mode.
	void dispParams5(const char *labels[5], const char *values[5], const bool dimmed[5], uint8_t sel, bool editing);
	// FORM Mix bar pages (LEVELS / CC): count bars; sel boxed, inverted while editing.
	// FORM Mix bar pages. locked marks P-Locked slots; bigNum >= 0 draws a large number
	// block on the right (the pot bank) selectable as cell index `count`.
	void dispMixLevels(const char *title, const char *valText, const int8_t *vals, uint8_t count, uint8_t sel, bool editing, const bool *locked = nullptr, int8_t bigNum = -1);
	// FORM Tools v2 layouts: action tools (params + buttons + steps; stepState=nullptr =
	// no-steps variant), the VEL/CHANCE bars page, and the generator page w/ live preview.
	void dispToolActionPage(const char *pLabels[], const char *pVals[], uint8_t pCount, const char *btnLabels[], uint8_t btnCount, int8_t sel, bool editing, const uint8_t *stepState, uint8_t pageLen, int8_t playhead);
	void dispToolBarsPage(uint8_t vmin, uint8_t vmax, uint8_t vRange, const int16_t bars[16], const uint8_t styles[16], int16_t barMax, int8_t sel, bool editing, int8_t playhead);
	void dispToolGenPage(const char *pLabels[], const char *pVals[], uint8_t pCount, int8_t sel, bool editing, bool *preview, uint8_t previewLen, const uint8_t *stepState, uint8_t pageLen, int8_t playhead);
	// Step F2 view: the current play-direction icon + its name on top, a bottom label below.
	// Step view Note hold: a compact piano keyboard (chord = notesAsKeys[6]) on top, with the
	// 16 step-marker cells beneath (filled = has content; `focus` step gets a tick). No text.
	void dispStepNoteKeyboard(int8_t notesAsKeys[6], const uint8_t *stepState, uint8_t pageLen, int8_t focus, bool stepStrip = true, int8_t pageNum = -1);
	// Seq page-1 track overview: 8 track-state squares (selected underlined, muted = outline)
	// top-left, left-justified track name, rate + 4 page icons + BPM on the right, and 16 step
	// boxes on the bottom (stepState: 0 empty / 1 has-notes / 2 ghost). playhead -1 = none.
	// modOverlay: 0 none · 1 F1 (box the page section) · 2 F2 (box the track name). overlayLabel
	// is the inverted bottom-box text shown while a modifier is held (nullptr when modOverlay = 0).
	// transport: 0 stopped · 1 playing · 2 recording (the active icon is filled). viewLabel is an
	// optional mode tag in the top area (e.g. "MIX"; nullptr = none); viewLabelSel boxes/inverts it
	// (the encoder's page-1 view selector is in edit mode) instead of the default underline.
	// showPagesSteps false = hide the 4 page icons + the 16-step row (e.g. the MI keyboard view).
	// showCCMeter draws the transient top-row knob meter (only while a knob is being adjusted).
	void dispSeqTrackPage(const char *trackName, const bool *trackMuted, uint8_t selTrack,
						  const char *rateStr, uint8_t playMode, uint16_t bpm, uint8_t enabledPages,
						  uint8_t activePage, const uint8_t *stepState, int8_t playhead,
						  uint8_t modOverlay, const char *overlayLabel, uint8_t pageLen, uint8_t transport,
						  const char *viewLabel, bool viewLabelSel, bool showPagesSteps = true,
						  bool showCCMeter = false, uint8_t numTracks = 8);
	// Step view overview: mode name on top, a row of `count` step cells on the bottom (filled
	// = has content). The playhead step (0-based, -1 = none) gets a tick underneath.
	void dispStepOverview(const char *modeName, const uint8_t *stepState, uint8_t pageLen, int8_t playhead, bool invertTitle = false);
	// Notes-view F1 (jump): "JUMP" on the left, the 4 page icons on the right (same position as
	// the track page), and the 16-step row below (focus = the step being edited).
	void dispNotesJump(const uint8_t *stepState, uint8_t pageLen, int8_t focus, uint8_t enabledPages, uint8_t activePage);
	// Persistent 5-segment CC meter on the top pixel row: each knob's current value (0-127) as a
	// horizontal bar (§2). Reads potSettings.analogValues. Call after clearing the buffer.
	void drawCCMeter();
	// The Seq STEPNOTES page: 6 note slots (FONT_LABELS) under a header. selected 0-5 boxes a
	// slot, 6 boxes the header (the names/numbers switch); encoderSelect draws the box.
	void dispNoteSlots(const char *slotNames[6], const char *header, uint8_t selected, bool encoderSelect);
	// Mix F3 (LEN | RATE): rate label on top, track length as a 16-cell bar on the bottom.
	// activeCount cells (0-16) are full boxes (steps within the length); the rest are dashes.
	// Every 4th active cell gets a left notch to make groups of 4 easier to count.
	void dispTrackLength(const char *rateStr, uint8_t activeCount);
	void dispGenericModeLabelSmallText(const char *label, uint8_t numPages, int8_t selectedPage);

	// FORM MI page-0 overlay: up to 4 rectangles along the bottom, one per enabled page, each width
	// proportional to that page's length (pageLens[4], 1-16). A filled cell marks the playhead —
	// playAbsStep is the absolute playing step (0-63), or -1 when stopped. Does NOT clear the buffer
	// (draws over the already-rendered keyboard view).
	void drawPageBars(const uint8_t *pageLens, uint8_t enabledMask, int8_t playAbsStep);

	// FORM Patterns view: a big "Pn" (TENFAT) on the left, the switch-style name top-right, an
	// optional tag under it (">Pq" queued / "CHn" chain), and a bottom progress bar (0-1) showing
	// how far through the loop/bar the playhead is (i.e. when a queued switch will commit).
	void dispPatternPage(uint8_t pat, const char *styleName, const char *tag, float progress);

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
