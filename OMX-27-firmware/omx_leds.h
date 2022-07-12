#pragma once
#include "config.h"

#include <Adafruit_NeoPixel.h>

// Declare NeoPixel strip object
Adafruit_NeoPixel strip(LED_COUNT, LED_PIN, NEO_GRB + NEO_KHZ800);

class OmxLeds
{
public:
    // OmxLeds() : strip(LED_COUNT, LED_PIN, NEO_GRB + NEO_KHZ800){};

    OmxLeds(){};

    void updateLeds();

    // clears dirty, transmits pixel data if dirty.
    void showLeds();

    bool getBlinkState();
    bool getSlowBlinkState();

    // void setPixelColor(uint16_t n, uint8_t r, uint8_t g, uint8_t b);

    void setAllLEDS(int R, int G, int B);

    void setDirty();

    // #### COLOR FUNCTIONS
    // Input a value 0 to 255 to get a color value.
    // The colours are a transition r - g - b - back to r.
    uint32_t Wheel(byte WheelPos);
    void colorWipe(byte red, byte green, byte blue, int SpeedDelay);
    // Theatre-style crawling lights with rainbow effect
    void theaterChaseRainbow(uint8_t wait);
    void CylonBounce(byte red, byte green, byte blue, int EyeSize, int SpeedDelay, int ReturnDelay, int start, int end);
    void RolandFill(byte red, byte green, byte blue, int start, int end, int SpeedDelay);

private:
    unsigned long blinkInterval = clockConfig.clockbpm * 2;
    bool blinkState = false;
    bool slowBlinkState = false;

    bool dirtyPixels = false;

    elapsedMillis blink_msec = 0;
    elapsedMillis slow_blink_msec = 0;

    elapsedMillis dirtyDisplayTimer = 0;
    unsigned long displayRefreshRate = 60;
};

extern OmxLeds omxLeds;