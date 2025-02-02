#include <Arduino.h>
#include "FastLED.h"
#include "SdFat.h"
#include "bmp.hpp"
#include "config.hpp"
#include "gui.hpp"
#include "sdios.h"
#include "timing.hpp"
#include "util.hpp"
#include <SPI.h>
#include "PhotoStick.hpp"

#include <limits.h>
#include <string.h>

// TODO Change code to display images correctly that have a width < 288 pixel
// TODO Maybe sort file alphabetically
// TODO Add "Status Bar". Prints error of file, shows that LED is running


namespace
{
CRGB leds[NUM_LEDS];

enum class StickState
{
  GUI,
  IMAGE,
  CREATIVE,
  PAUSE,
};

struct
{
  StickState    state;   // Holds current state
  BMP::BMPFile  bmpFile; // In IMAGE mode: Holds info on BMP file
  uint16_t      step;    // Counts activations of loop()
  uint16_t      maxStep; // After maxStep loop() activations: transition state
  unsigned long startMs; // In PAUSE mode: used determine current pause length
  unsigned long durationMs;     // In PAUSE Mode: bounds pause length
  StickState    nextState;      // State to transition to after PAUSE
  CRGB          animationColor; // In CREATIVE mode: color for animation
  Animation     animation;      // In CREATIVE mode: animation type
  uint8_t       repetitions;    // In IMAGE/CREATIVE: number of repetitions
  uint16_t      delayMs;        // In IMAGE/CREATIVE: delay if speed < 100%
  SdFat         sd;
} stick;

/* Definitions for Software SPI */
// Chip select may be constant or RAM variable.
const uint8_t SD_CS_PIN = 10;
//
// Pin numbers in templates must be constants.
const uint8_t SOFT_MISO_PIN = 12;
const uint8_t SOFT_MOSI_PIN = 11;
const uint8_t SOFT_SCK_PIN  = 13;

SoftSpiDriver<SOFT_MISO_PIN, SOFT_MOSI_PIN, SOFT_SCK_PIN> softSpi;
#define SD_CONFIG SdSpiConfig(SD_CS_PIN, SHARED_SPI, SD_SCK_MHZ(0), &softSpi)
/* End Definitions for Software SPI */

#ifdef ENABLE_TIMING
Timing::Stats statLoad;
Timing::Stats statShow;
#endif
}

// See:
// https://learn.adafruit.com/adafruit-2-8-tft-touch-shield-v2/backlight-touch-irq
void setBacklight(bool on)
{
  digitalWrite(BACKLIGHT_PIN, on ? HIGH : LOW);
}

void initSdCard()
{
  Serial.println(F("Initializing SD card... "));
  if (!stick.sd.begin(SD_CONFIG) ) {
    stick.sd.printSdError(&Serial);
    panic(F("SD Init ... failed!"));
  }
  Serial.println(F("OK!"));
}

void setup()
{
  Serial.begin(9600);
  Serial.println(F("Photostick\n"));

  initSdCard();

  Gui::init(stick.sd);

  /* Enable backlight */
  pinMode(BACKLIGHT_PIN, OUTPUT);
  setBacklight(true);

  stick.state = StickState::GUI;

  FastLED.setCorrection(TypicalLEDStrip);
  FastLED.setBrightness(25);
  FastLED.addLeds<WS2812B, DATA_PIN, GRB>(leds, NUM_LEDS);
}

void animate()
{
  switch (stick.animation) {
  case ANIM_LIGHT:
    FastLED.showColor(stick.animationColor);
    delay(1000);
    break;

  case ANIM_BLINK:
    if (stick.step % 2 == 0) {
      FastLED.showColor(stick.animationColor);
      delay(500);
    } else {
      FastLED.clear(true);
      delay(500);
    }
    break;

  case ANIM_MARQUEE:
    FastLED.clear();
    leds[stick.step] = stick.animationColor;
    FastLED.show();
    break;
  }
}

uint16_t getAnimationSteps(Animation anim)
{
  switch (anim) {
  case ANIM_LIGHT:
    return 1;
  case ANIM_BLINK:
    return 2;
  case ANIM_MARQUEE:
    return NUM_LEDS;
  default:
    return 0;
  }
}

void loop()
{
  // Update logic
  switch (stick.state) {
  case StickState::GUI: {
#ifdef BATTERY_VOLTAGE_PIN
    const int  scaledBatteryVoltage = analogRead(BATTERY_VOLTAGE_PIN);
    const bool isBatteryVoltageLow =
      (scaledBatteryVoltage > 0 &&
       scaledBatteryVoltage <= BATTERY_VOLTAGE_THRESHOLD);
#else
    const bool isBatteryVoltageLow = false;
#endif

    Gui::update(isBatteryVoltageLow, false);
    break;
  }
  case StickState::IMAGE:
    setBacklight(false);
    TIME(&statLoad, BMP::loadRow(stick.bmpFile, stick.step, &leds[0]));
    TIME(&statShow, FastLED.show());
    ++stick.step;
    delay(stick.delayMs);
    break;

  case StickState::CREATIVE:
    setBacklight(false);
    animate();
    ++stick.step;
    delay(stick.delayMs);
    break;

  case StickState::PAUSE:
    setBacklight(false);
    break;
  }

  // Transition criteria
  switch (stick.state) {
  case StickState::GUI: {
    StickConfig cfg;
    if (Gui::readyToGo(cfg)) {
      if (cfg.fileToLoad != nullptr) {
        stick.nextState = StickState::IMAGE;
        BMP::open(stick.sd, stick.bmpFile, cfg.fileToLoad);
        stick.maxStep = stick.bmpFile.height;
      } else {
        stick.nextState      = StickState::CREATIVE;
        stick.animation      = cfg.animation;
        stick.animationColor = cfg.animationColor;
        stick.maxStep        = getAnimationSteps(stick.animation);
      }

      stick.delayMs     = MAX_DELAY_MS - cfg.speed * (MAX_DELAY_MS / 10);
      stick.step        = 0;
      stick.state       = StickState::PAUSE;
      stick.startMs     = millis();
      stick.durationMs  = cfg.countdown * 1000;
      stick.repetitions = cfg.repetitions;
      FastLED.setBrightness(cfg.brightness * 10);
    }
    break;
  }

  case StickState::IMAGE:
  case StickState::CREATIVE:
    Gui::update(false, true);
    if (stick.step == stick.maxStep) {
      --stick.repetitions;
      stick.step = 0;
    }

    if (stick.repetitions == 0) {
      FastLED.clear(true);
      stick.state     = StickState::PAUSE;
      stick.nextState = StickState::GUI;
      stick.startMs   = millis();

      stick.bmpFile.file.close();

#ifdef ENABLE_TIMING
      Serial.print(F("Show: "));
      statShow.println();
      statShow = Timing::Stats();
      Serial.print(F("Load: "));
      statLoad.println();
      statLoad = Timing::Stats();
#endif
    Gui::update(false, false);
    }
    break;

  case StickState::PAUSE:
    if (millis() - stick.startMs >= stick.durationMs) {
      stick.startMs = millis();
      stick.state   = stick.nextState;
      setBacklight(true);
    }
    break;
  }
}

// vim: et ts=2
