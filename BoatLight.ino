#include <Adafruit_NeoPixel.h>
#include <Bounce2.h>
#include <EEPROM.h>
#include <avr/sleep.h>
#include <avr/wdt.h>

#define DEBUG
#define WOKWI

#define LED_PIN       0   // PB0
#define BUTTON_PIN    1   // PB1
#define VOLTAGE_PIN   A1  // PB2
#define NUM_LEDS      16
#define NUM_MODES     9
#define SLEEP_CYCLES  7   // Each Cycle is eight seconds, ~ 56 seconds
#define VOLTAGE_LOWER_THRESHOLD 2.35
#define VOLTAGE_UPPER_THRESHOLD 2.45

enum class SosState { IDLE, FADING_IN, ON, FADING_OUT, OFF };

#ifndef WOKWI
  Adafruit_NeoPixel strip(NUM_LEDS, LED_PIN, NEO_GRBW + NEO_KHZ800);
#else
  Adafruit_NeoPixel strip(NUM_LEDS, LED_PIN, NEO_GRB + NEO_KHZ800);
#endif
Bounce2::Button modeButton = Bounce2::Button();
SosState sosState = SosState::IDLE;

// SOS pattern in ms
const uint16_t sosPattern[] PROGMEM = {
  250,250,250,  // ...
  750,750,750,  // ---
  250,250,250   // ...
};

// Constants
const uint8_t sosPause = 250;
const uint16_t sosGap = 1500;
const float r1 = 10000.0;
const float r2 = 10000.0;
const float referenceVoltage = 5.0;

// Globals
bool outputState = false;
bool sosRunning = false;
bool cycleEnd = false;
uint8_t currentMode = 0;
uint8_t sosIndex = 0;
uint8_t fadeBrightness = 0;
unsigned long sosLastTime = 0;

void setup() {
  initiateStrip();
  initiateModeButton();
  readAndSanitizeCurrentMode();
}

void loop() {
  handleModeButtonPress();  
  #ifndef DEBUG
    performVoltageRead();
  #else
    applyCurrentMode(currentMode);
  #endif
}

void initiateStrip() {
  strip.begin();
  strip.clear();
  strip.show();
}

void initiateModeButton() {
  modeButton.attach(BUTTON_PIN, INPUT_PULLUP);
  modeButton.interval(5);
  modeButton.setPressedState(LOW); 
}

void readAndSanitizeCurrentMode() {
  currentMode = EEPROM.read(0);

  if (currentMode >= NUM_MODES) currentMode = 0;
}

void handleModeButtonPress() {
  modeButton.update();

  if (modeButton.pressed()) {
    currentMode = (currentMode + 1) % NUM_MODES;
    EEPROM.update(0, currentMode);
  }
}

void performVoltageRead() {
  float solarVoltage = readSolarVoltage();

  if (!outputState && solarVoltage < VOLTAGE_LOWER_THRESHOLD) {
    applyCurrentMode(currentMode);
    outputState = true;
  } else if (outputState && solarVoltage > VOLTAGE_UPPER_THRESHOLD) {
    strip.clear();
    strip.show();
    for (int j=0; j < SLEEP_CYCLES; j++) { 
      shutDown_with_WD (0b100001);  // 8 seconds
      delay(50);
    }
  }
}

float readSolarVoltage() {
  int rawVoltage = analogRead(VOLTAGE_PIN);
  
  float voltageAtPin = rawVoltage * (referenceVoltage / 1023.0);
  float solarVoltage = voltageAtPin * ((r1 + r2) / r2);

  return solarVoltage;  
}

void applyCurrentMode(uint8_t mode) {
  switch (mode) {
    case 0: // LED 6–10 green
      #ifndef WOKWI
        colorLedsInRange(5, 9, 0, 255, 0, 0, true); break;
      #else
        colorLedsInRange(5, 9, 0, 255, 0, true); break;
      #endif
    case 1: // LED 1–5 red
      #ifndef WOKWI
        colorLedsInRange(0, 4, 255, 0, 0, 0, true); break;
      #else
        colorLedsInRange(0, 4, 255, 0, 0, true); break;
      #endif      
    case 2:  // LED 1–5 red, 6–10 green
      #ifndef WOKWI
        colorLedsInRange(0, 4, 255, 0, 0, 0, true);
        colorLedsInRange(5, 9, 0, 255, 0, 0, false); break;
      #else
        colorLedsInRange(0, 4, 255, 0, 0, true);
        colorLedsInRange(5, 9, 0, 255, 0, false); break;
      #endif
    case 3: // LED 1–10 white
      #ifndef WOKWI
        colorLedsInRange(0, 9, 0, 0, 0, 255, true); break;
      #else
        colorLedsInRange(0, 9, 255, 255, 255, true); break;
      #endif      
    case 4: // LED 11–16 white
      #ifndef WOKWI
        colorLedsInRange(10, 15, 0, 0, 0, 255, true); break;
      #else
        colorLedsInRange(10, 15, 255, 255, 255, true); break;
      #endif      
    case 5: // 1–5 red, 6–10 green, 11–16 white
      #ifndef WOKWI
        colorLedsInRange(0, 4, 255, 0, 0, 0, true);
        colorLedsInRange(5, 9, 0, 255, 0, 0, false);
        colorLedsInRange(10, 15, 0, 0, 0, 255, false);
      #else
        colorLedsInRange(0, 4, 255, 0, 0, true);
        colorLedsInRange(5, 9, 0, 255, 0, false);
        colorLedsInRange(10, 15, 255, 255, 255, false);        
      #endif
      break;
    case 6: // All white
      #ifndef WOKWI
        colorLedsInRange(0, 15, 0, 0, 0, 255, true); break;
      #else
        colorLedsInRange(0, 15, 255, 255, 255, true); break;
      #endif      
    case 7: // SOS
      if (!sosRunning) {
        sosIndex = 0;
        sosState = SosState::FADING_IN;
        sosLastTime = millis();
        fadeBrightness = 0;
        sosRunning = true;
      }
      break;
    case 8: 
    default: // All off
      strip.clear();  strip.show();
      sosRunning = false;
      break;
  }

  if (currentMode == 7) {
    handleSosAnimation();
  } else {
    strip.show();
  }
}

#ifndef WOKWI
  void colorLedsInRange(uint8_t start, uint8_t end, uint8_t r, uint8_t g, uint8_t b, uint8_t w, bool reset) {
    if (reset) {
      strip.clear();
      strip.setBrightness(255);
    } 
    
    for (int i = start; i <= end; i++) {
      strip.setPixelColor(i, strip.Color(r, g, b, w));
    }
  }
#else
  void colorLedsInRange(uint8_t start, uint8_t end, uint8_t r, uint8_t g, uint8_t b, bool reset) {
    if (reset) {
      strip.clear();
      strip.setBrightness(255);
    } 

    for (int i = start; i <= end; i++) {
      strip.setPixelColor(i, strip.Color(r, g, b));
    }
  }
#endif

void setAllWhite(uint8_t brightness) {
  strip.setBrightness(brightness);
  for (int i = 0; i < NUM_LEDS; i++) {
      #ifndef WOKWI
        strip.setPixelColor(i, strip.Color(0, 0, 0, 255));
      #else
        strip.setPixelColor(i, strip.Color(255, 255, 255));
      #endif
  }
  strip.show();
}

void handleSosAnimation() {
  static unsigned long lastStepTime = 0;
  static bool paused = false;

  if (!sosRunning) return;

  unsigned long now = millis();
  int duration = pgm_read_word(&sosPattern[sosIndex]);

  switch (sosState) {
    case SosState::FADING_IN:
      if (now - lastStepTime >= 10) {
        lastStepTime = now;
        if (fadeBrightness < 255) {
          fadeBrightness += 15;          
          setAllWhite(fadeBrightness);
        } else {
          sosState = SosState::ON;
          sosLastTime = now;
        }
      }
      break;

    case SosState::ON:
      if (now - sosLastTime >= duration) {
        sosState = SosState::FADING_OUT;
        lastStepTime = now;
      }
      break;

    case SosState::FADING_OUT:
      if (now - lastStepTime >= 10) {
        lastStepTime = now;
        if (fadeBrightness > 0) {
          fadeBrightness -= 15;          
          setAllWhite(fadeBrightness);
        } else {
          strip.clear();
          strip.show();
          sosState = SosState::OFF;
          sosLastTime = now;

          sosIndex++;
          if (sosIndex >= sizeof(sosPattern) / sizeof(sosPattern[0])) {
            sosIndex = 0;
            cycleEnd = true;
          }          
        }
      }
      break;

    case SosState::OFF:
      if (now - sosLastTime >= (cycleEnd ? sosGap : sosPause)) {
        lastStepTime = now;          
        if (!paused) {
          paused = true;
        } else {
          paused = false;
          cycleEnd = false;
          sosState = SosState::FADING_IN;
          fadeBrightness = 0;          
        }
      }
      break;

    default:
      break;
  }
}

// This function is called when the watchdog timer expires. 
ISR(WDT_vect) {
  wdt_disable();                  // Disable watchdog timer after wake-up    
}

// This function puts the ATTINY in sleep mode.
// To save power, the ADCs (Analog-to-Digital Converter) are explicitly shutdown.
void shutDown_with_WD(const byte time_len) {
  noInterrupts();                 // Disables interrupts temporarily
  wdt_reset();                    // Resets the watchdog
   
  MCUSR = 0;                      // Clears any reset flags
  WDTCR |= 0b00011000;            // Set WDCE, WDE
  WDTCR =  0b01000110 | time_len; // Set WDIE and delay
 
  ADCSRA &= ~_BV(ADEN);           // Stop the adc

  set_sleep_mode (SLEEP_MODE_PWR_DOWN);
  sleep_bod_disable();            // Disables brown-out detection during sleep
  interrupts();                   // Re-enable interrupts before sleeping
  ADCSRA |= _BV(ADEN);            // Re-enable ADC after wake-up
  sleep_mode();                   // Enter sleep mode (actually sleeps)
}