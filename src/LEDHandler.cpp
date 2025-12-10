#include "LEDHandler.h"
#include <FastLED.h>

// LED array
CRGB leds[NUM_LEDS];

// Current state
static LEDState currentState = LED_OFF;
static unsigned long lastUpdate = 0;
static bool pulseDirection = true;
static uint8_t pulseValue = 0;
static uint8_t currentBrightness = LED_BRIGHTNESS;

void initLED()
{
    FastLED.addLeds<WS2812B, LED_PIN, GRB>(leds, NUM_LEDS);
    FastLED.setBrightness(currentBrightness);
    leds[0] = CRGB::Black;
    FastLED.show();

    // Quick startup flash
    flashLED(0, 255, 0, 100); // Green flash on startup
}

void setLEDState(LEDState state)
{
    currentState = state;

    switch (state)
    {
    case LED_OFF:
        leds[0] = CRGB::Black;
        break;
    case LED_IDLE:
        leds[0] = CRGB(0, 30, 0); // Dim green
        break;
    case LED_READING:
        leds[0] = CRGB(0, 0, 255); // Blue
        break;
    case LED_WIFI_ACTIVE:
        leds[0] = CRGB(0, 255, 255); // Cyan
        break;
    case LED_BLE_ACTIVE:
        leds[0] = CRGB(128, 0, 255); // Purple
        break;
    case LED_RECORDING:
        // Will be animated in updateLED()
        leds[0] = CRGB(255, 0, 0); // Red
        break;
    case LED_LOW_BATTERY:
        leds[0] = CRGB(255, 100, 0); // Orange
        break;
    case LED_ERROR:
        leds[0] = CRGB(255, 0, 0); // Red
        break;
    case LED_SUCCESS:
        leds[0] = CRGB(0, 255, 0); // Green
        break;
    }
    FastLED.show();
}

void setLEDColor(uint8_t r, uint8_t g, uint8_t b)
{
    currentState = LED_OFF; // Custom color, no auto-animation
    leds[0] = CRGB(r, g, b);
    FastLED.show();
}

void setLEDBrightness(uint8_t brightness)
{
    currentBrightness = brightness;
    FastLED.setBrightness(brightness);
    FastLED.show();
}

void turnOffLED()
{
    currentState = LED_OFF;
    leds[0] = CRGB::Black;
    FastLED.show();
}

void updateLED()
{
    unsigned long now = millis();

    // Handle animated states
    switch (currentState)
    {
    case LED_RECORDING:
        // Pulsing red effect
        if (now - lastUpdate > 30)
        {
            lastUpdate = now;
            if (pulseDirection)
            {
                pulseValue += 5;
                if (pulseValue >= 255)
                    pulseDirection = false;
            }
            else
            {
                pulseValue -= 5;
                if (pulseValue <= 30)
                    pulseDirection = true;
            }
            leds[0] = CRGB(pulseValue, 0, 0);
            FastLED.show();
        }
        break;

    case LED_LOW_BATTERY:
        // Blinking orange
        if (now - lastUpdate > 500)
        {
            lastUpdate = now;
            if (leds[0].r > 0)
            {
                leds[0] = CRGB::Black;
            }
            else
            {
                leds[0] = CRGB(255, 100, 0);
            }
            FastLED.show();
        }
        break;

    default:
        // Non-animated states, nothing to do
        break;
    }
}

void flashLED(uint8_t r, uint8_t g, uint8_t b, int duration_ms)
{
    LEDState previousState = currentState;
    CRGB previousColor = leds[0];

    leds[0] = CRGB(r, g, b);
    FastLED.show();
    delay(duration_ms);

    // Restore previous state
    leds[0] = previousColor;
    currentState = previousState;
    FastLED.show();
}
