#pragma once
#include <Arduino.h>

// LED Pin Configuration
#define LED_PIN 48
#define NUM_LEDS 1
#define LED_BRIGHTNESS 50 // 0-255

// LED States/Colors
enum LEDState
{
    LED_OFF,
    LED_IDLE,        // Dim green - normal operation
    LED_READING,     // Blue flash - sensor reading
    LED_WIFI_ACTIVE, // Cyan - WiFi connected
    LED_BLE_ACTIVE,  // Purple - BLE connected
    LED_RECORDING,   // Red pulse - recording active
    LED_LOW_BATTERY, // Orange blink - low battery warning
    LED_ERROR,       // Red - error state
    LED_SUCCESS      // Green flash - operation successful
};

// Initialize the LED
void initLED();

// Set LED to a specific state
void setLEDState(LEDState state);

// Set LED to a custom color (RGB values 0-255)
void setLEDColor(uint8_t r, uint8_t g, uint8_t b);

// Set LED brightness (0-255)
void setLEDBrightness(uint8_t brightness);

// Turn off the LED
void turnOffLED();

// Update LED animations (call in loop for animated states)
void updateLED();

// Quick flash for feedback
void flashLED(uint8_t r, uint8_t g, uint8_t b, int duration_ms = 100);
