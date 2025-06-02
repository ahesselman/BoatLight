#include <Adafruit_NeoPixel.h>
#include <Bounce2.h>
#include <EEPROM.h>
#include <avr/sleep.h>
#include <avr/wdt.h>

#define DEBUG
#define WOKWI   // Uncomment this when testing on Wokwi simulator

#define LED_PIN               0   // PB0
#define BUTTON_PIN            1   // PB1
#define VOLTAGE_PIN           A1  // PB2
#define LED_POWER_SWITCH_PIN  3   // PB3

#define NUM_LEDS      16
#define NUM_MODES     9
#define SLEEP_CYCLES  7   // Sleep duration when idle (7 x 8 sec = ~ 56 sec)
#define VOLTAGE_LOWER_THRESHOLD 2.35  // Threshold LEDs on
#define VOLTAGE_UPPER_THRESHOLD 2.45  // Threshold LEDs off

// State machine for SOS fading behavior
enum class SosState { IDLE, FADING_IN, ON, FADING_OUT, OFF };

// Available light modes
enum LightMode {
  GREEN = 0,
  RED,
  GREEN_RED,
  WHITE_1_10,
  WHITE_11_16,
  FULL_COMBO,
  ALL_WHITE,
  SOS,
  OFF_MODE
};

#ifndef WOKWI
  Adafruit_NeoPixel strip(NUM_LEDS, LED_PIN, NEO_GRBW + NEO_KHZ800);
#else
  Adafruit_NeoPixel strip(NUM_LEDS, LED_PIN, NEO_GRB + NEO_KHZ800);
#endif
Bounce2::Button modeButton = Bounce2::Button();
SosState sosState = SosState::IDLE;

// --- SOS Pattern ---
// Timing for SOS Morse pattern (in milliseconds)
const uint16_t sosPattern[] PROGMEM = {
  250,250,250,  // ...
  750,750,750,  // ---
  250,250,250   // ...
};

#define SOS_PATTERN_LENGTH (sizeof(sosPattern) / sizeof(sosPattern[0]))

// Constants
const uint8_t fadeStep = 15;
const uint8_t sosPause = 250;
const uint16_t sosGap = 1500;
const unsigned long saveDelay = 2000; // EEPROM write delay, in ms
const float r1 = 10000.0;             // Voltage divider resistor 1 value, in Ohm
const float r2 = 10000.0;             // Voltage divider resistor 1 value, in Ohm
constexpr float referenceVoltage = 5.0;
constexpr float dividerFactor = referenceVoltage / 1023.0;  // ADC scale factor

// Globals
bool outputState = false;                   // Whether LEDs are powered
bool sosRunning = false;                    // Whether SOS is active
bool cycleEnd = false;                      // End of SOS cycle
uint8_t lastSavedMode = 255;                // Previously saved mode
uint8_t currentMode = LightMode::OFF_MODE;  
uint8_t sosIndex = 0;                       // Current SOS pattern step
uint8_t fadeBrightness = 0;
unsigned long sosLastTime = 0;
unsigned long lastModeChangeTime = 0;

// ----- Arduino setup -----
void setup() {
  initiateStrip();
  initiateModeButton();
  readAndSanitizeCurrentMode();
}

// ----- Arduino loop -----
void loop() {
  handleModeButtonPress();
  storeCurrentMode();

  #ifndef DEBUG
    performVoltageRead();
  #else
    applyCurrentMode(currentMode); // Skip voltage check when in debug mode
  #endif
}

// ----- Initialization functions -----
void initiateStrip() {
  strip.begin();
  strip.clear();
  strip.show();

  pinMode(LED_POWER_SWITCH_PIN, OUTPUT);
  digitalWrite(LED_POWER_SWITCH_PIN, LOW);
}

void initiateModeButton() {
  modeButton.attach(BUTTON_PIN, INPUT_PULLUP);
  modeButton.interval(25);
  modeButton.setPressedState(LOW); 
}

void readAndSanitizeCurrentMode() {
  currentMode = EEPROM.read(0);
  if (currentMode >= NUM_MODES) currentMode = LightMode::GREEN;
}

// ----- Button and EEPROM Handling -----
void handleModeButtonPress() {
  modeButton.update();

  if (modeButton.fell()) {
    currentMode = (currentMode + 1) % NUM_MODES;
    lastModeChangeTime  = millis();
  }
}

void storeCurrentMode() {
  if (currentMode != lastSavedMode && millis() - lastModeChangeTime > saveDelay) {    
    EEPROM.update(0, currentMode);
    lastSavedMode = currentMode;
  }
}

// ----- Voltage Monitoring and Power Control -----
void performVoltageRead() {
  float solarVoltage = readSolarVoltage();

  if (solarVoltage < VOLTAGE_LOWER_THRESHOLD) {
    if (!outputState)
      outputState = true;
    digitalWrite(LED_POWER_SWITCH_PIN, HIGH);
    applyCurrentMode(currentMode);
  } else if (outputState && solarVoltage > VOLTAGE_UPPER_THRESHOLD) {
    outputState = false;

    strip.clear();
    strip.show(); 

    digitalWrite(LED_POWER_SWITCH_PIN, LOW);

    for (int j=0; j < SLEEP_CYCLES; j++) { 
      shutDownWithWD (0b100001);  // 8 seconds
    }
  }
}

float readSolarVoltage() {
  int rawVoltage = analogRead(VOLTAGE_PIN);
  
  float voltageAtPin = rawVoltage * dividerFactor;
  float solarVoltage = voltageAtPin * ((r1 + r2) / r2);

  return solarVoltage;  
}

// ----- LED Mode control -----
void applyCurrentMode(uint8_t mode) {
    if (mode != LightMode::OFF_MODE) {
      digitalWrite(LED_POWER_SWITCH_PIN, HIGH);
    } else {
      digitalWrite(LED_POWER_SWITCH_PIN, LOW);
    }

  switch (mode) {
    case LightMode::GREEN: // LED 6–10 green
      #ifndef WOKWI
        colorLedsInRange(5, 9, 0, 255, 0, 0, true);
      #else
        colorLedsInRange(5, 9, 0, 255, 0, true);
      #endif
      break;
    case LightMode::RED: // LED 1–5 red
      #ifndef WOKWI
        colorLedsInRange(0, 4, 255, 0, 0, 0, true);
      #else
        colorLedsInRange(0, 4, 255, 0, 0, true);
      #endif      
      break;
    case LightMode::GREEN_RED:  // LED 1–5 red, 6–10 green
      #ifndef WOKWI
        colorLedsInRange(0, 4, 255, 0, 0, 0, true);
        colorLedsInRange(5, 9, 0, 255, 0, 0, false);
      #else
        colorLedsInRange(0, 4, 255, 0, 0, true);
        colorLedsInRange(5, 9, 0, 255, 0, false);
      #endif
      break;
    case LightMode::WHITE_1_10: // LED 1–10 white
      #ifndef WOKWI
        colorLedsInRange(0, 9, 0, 0, 0, 255, true);
      #else
        colorLedsInRange(0, 9, 255, 255, 255, true);
      #endif
      break;
    case LightMode::WHITE_11_16: // LED 11–16 white
      #ifndef WOKWI
        colorLedsInRange(10, 15, 0, 0, 0, 255, true);
      #else
        colorLedsInRange(10, 15, 255, 255, 255, true);
      #endif
      break;
    case LightMode::FULL_COMBO: // 1–5 red, 6–10 green, 11–16 white
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
    case LightMode::ALL_WHITE: // All white
      #ifndef WOKWI
        colorLedsInRange(0, 15, 0, 0, 0, 255, true);
      #else
        colorLedsInRange(0, 15, 255, 255, 255, true);
      #endif
      break;
    case LightMode::SOS: // SOS
      if (!sosRunning) {
        sosIndex = 0;
        sosState = SosState::FADING_IN;
        sosLastTime = millis();
        fadeBrightness = 0;
        sosRunning = true;
      }
      handleSosAnimation();
      break;
    case LightMode::OFF_MODE: 
    default: // All off
      strip.clear();
      sosRunning = false;
      break;
  }

  if (mode != LightMode::SOS) {
    strip.show();
  }
}

// ----- LED color helper -----
#ifndef WOKWI
void colorLedsInRange(uint8_t start, uint8_t end, uint8_t r, uint8_t g, uint8_t b, uint8_t w, bool reset) {
#else
void colorLedsInRange(uint8_t start, uint8_t end, uint8_t r, uint8_t g, uint8_t b, bool reset) {
#endif
  if (reset) {
    strip.clear();
    strip.setBrightness(255);
  }
  for (int i = start; i <= end; i++) {
    #ifndef WOKWI
      strip.setPixelColor(i, strip.Color(r, g, b, w));
    #else
      strip.setPixelColor(i, strip.Color(r, g, b));
    #endif
  }
}

// ----- All-white Helper for SOS -----
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

// ----- SOS Blinking Animation Handler -----
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
          fadeBrightness += fadeStep;          
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
          fadeBrightness -= fadeStep;          
          setAllWhite(fadeBrightness);
        } else {
          strip.clear();
          strip.show();
          sosState = SosState::OFF;
          sosLastTime = now;

          sosIndex++;
          if (sosIndex >= SOS_PATTERN_LENGTH) {
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

// ----- Watchdog Timer ISR -----
// Wakes the device up from sleep when WDT expires
ISR(WDT_vect) {
  wdt_disable();  // Disable WDT after wake-up  
}

// ----- Sleep Handler -----
// Puts device to deep sleep for power saving
void shutDownWithWD(const byte time_len) {
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