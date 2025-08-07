#include <Adafruit_NeoPixel.h>
#include <Bounce2.h>
#include <EEPROM.h>
#include <avr/sleep.h>
#include <avr/wdt.h>

#ifdef __AVR__
 #include <avr/power.h> // Required for 16 MHz Adafruit Trinket
#endif

//#define DEBUG
//#define WOKWI   // Comment this when testing on Wokwi simulator

#define LED_PIN                 0     // PB0
#define BUTTON_PIN              1     // PB1
#define VOLTAGE_PIN             A1    // PB2
#define LED_POWER_SWITCH_PIN    3     // PB3

#define NUM_LEDS                17    // Number of LEDs on the strip
#define COLOR_DEGREES           112.5 // Amount of degress for the red and green LEDs
#define NUM_MODES               9     // Number of modes
#define SLEEP_CYCLES            7     // Sleep duration when idle (7 x 8 sec = ~ 56 sec)
#define VOLTAGE_LOWER_THRESHOLD 3.50  // Threshold LEDs on
#define VOLTAGE_UPPER_THRESHOLD 4.50  // Threshold LEDs off

// State machine for SOS fading behavior
enum class SosState { IDLE, FADING_IN, ON, FADING_OUT, OFF };

// Available light modes
enum LightMode {
  GREEN = 0,
  RED,
  GREEN_RED,
  WHITE_FRONT,
  WHITE_BACK,
  FULL_COMBO,
  WHITE_ALL,
  SOS,
  OFF_MODE
};

struct LightSectors {
  uint8_t red[NUM_LEDS];
  uint8_t green[NUM_LEDS];
  uint8_t whiteBack[NUM_LEDS];
  uint8_t whiteFront[NUM_LEDS];

  uint8_t redCount;
  uint8_t greenCount;
  uint8_t whiteBackCount;
  uint8_t whiteFrontCount;
};

enum LightSectorWhiteMode {
  FRONT = 0,
  BACK
};

#ifndef WOKWI
  Adafruit_NeoPixel strip(NUM_LEDS, LED_PIN, NEO_GRBW + NEO_KHZ800);
#else
  Adafruit_NeoPixel strip(NUM_LEDS, LED_PIN, NEO_GRB + NEO_KHZ800);
#endif
Bounce2::Button modeButton = Bounce2::Button();
SosState sosState = SosState::IDLE;

#define SOS_PATTERN_LENGTH (sizeof(sosPattern) / sizeof(sosPattern[0]))

// Constants
const uint8_t fadeStepDuration = 15;
const uint8_t sosPauseDuration = 250;
const uint8_t dotDuration = 250;
const uint16_t dashDuration = 750;
const uint16_t sosGapDuration = 1500;
const unsigned long saveDelayDuration = 2000;       // EEPROM write delay, in ms
const uint8_t maxBrightness = 255;
const float r1 = 10000.0;                   // Voltage divider resistor 1 value, in Ohm
const float r2 = 10000.0;                   // Voltage divider resistor 1 value, in Ohm
constexpr float referenceVoltage = 5.0;
constexpr float dividerFactor = referenceVoltage / 1023.0;  // ADC scale factor

// SOS Pattern, timing for SOS Morse pattern (in milliseconds)
const uint16_t sosPattern[] PROGMEM = {
  dotDuration,dotDuration,dotDuration,  // ...
  dashDuration,dashDuration,dashDuration,  // ---
  dotDuration,dotDuration,dotDuration   // ...
};

// Globals
bool outputState = false;                   // Whether LEDs are powered
bool sosRunning = false;                    // Whether SOS is active
bool cycleEnd = false;                      // End of SOS cycle
bool shouldResetStrip = false;
uint8_t lastSavedMode = 255;                // Previously saved mode
uint8_t currentMode = LightMode::OFF_MODE;  
uint8_t sosIndex = 0;                       // Current SOS pattern step
uint8_t fadeBrightness = 0;
unsigned long sosLastTime = 0;
unsigned long lastModeChangeTime = 0;
unsigned long pressStartTime = 0;
LightSectors lightSectors;

// ----- Arduino setup -----
void setup() {
  // These lines are specifically to support the Adafruit Trinket 5V 16 MHz.
  // Any other board, you can remove this part (but no harm leaving it):
#if defined(__AVR_ATtiny85__) && (F_CPU == 16000000)
  clock_prescale_set(clock_div_1);
#endif

  initiateStrip();
  initiateModeButton();
  lightSectors = calculateNavLightLEDs();
  readAndSanitizeCurrentMode();
}

// ----- Arduino loop -----
void loop() {
  handleModeButtonPress();
  storeCurrentMode();
  performAndHandleVoltageRead();
}

// ----- Initialization functions -----
void initiateStrip() {
  strip.begin();
  strip.clear();
  strip.show();

  // Configure the pin that switches the power for the ledstrip
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

// Define the ids for the leds for the various color 
LightSectors calculateNavLightLEDs() {
  LightSectors result;
  result.redCount = 0;
  result.greenCount = 0;
  result.whiteFrontCount = 0;
  result.whiteBackCount = 0;

  float degrees_per_led = 360.0 / NUM_LEDS;
  uint8_t numberColoredLeds = round(COLOR_DEGREES / degrees_per_led);

  // Red LEDs
  for (uint8_t i = 0; i < numberColoredLeds; i++) {
    result.red[result.redCount++] = i + 1;
  }

  // Green LEDs
  for (uint8_t i = 0; i < numberColoredLeds; i++) {
    result.green[result.greenCount++] = numberColoredLeds + i + 1;
  }

  // White LEDs
  uint8_t numberWhiteLeds = NUM_LEDS - (2 * numberColoredLeds);
  uint8_t startIdOfWhiteLed = 2 * numberColoredLeds;

  for (uint8_t i = 0; i < numberWhiteLeds; i++) {
    result.whiteBack[result.whiteBackCount++] = startIdOfWhiteLed + i + 1;
  }

  for (uint8_t i = 0; i < numberColoredLeds * 2; i++) {
    result.whiteFront[result.whiteFrontCount++] = i + 1; 
  }
  
  return result;
}

// ----- Button and EEPROM Handling -----
void handleModeButtonPress() {
  modeButton.update();

  if (modeButton.fell()) {
    pressStartTime = millis();  // record press time
  }

  if (modeButton.rose()) {
    unsigned long pressDuration = millis() - pressStartTime;
    
    if (pressDuration >= 1000) {
      // Long press -> jump to OFF mode
      currentMode = LightMode::OFF_MODE;
    } else {
      // Short press -> next mode
      currentMode = (currentMode + 1) % NUM_MODES;
    }

    if (currentMode != lastSavedMode) {
      shouldResetStrip = true;
    }

    lastModeChangeTime = millis(); // update timer for EEPROM saving
  }
}

void storeCurrentMode() {
  if (currentMode != lastSavedMode && millis() - lastModeChangeTime > saveDelayDuration) {    
    EEPROM.update(0, currentMode);
    lastSavedMode = currentMode;
  }
}

// ----- Voltage Monitoring and Power Control -----
void performAndHandleVoltageRead() {
  float solarVoltage = readSolarVoltage();
  handleVoltageState(solarVoltage);
}

float readSolarVoltage() {
  long sum = 0;
  // for noise reduction perform 10 reads and calculate average 
  for (int i = 0; i < 10; i++)
  {
    sum += analogRead(VOLTAGE_PIN);
    delay(2);    
  }
  
  float averagRaw = sum / 10.0;

  float voltageAtPin = averagRaw * dividerFactor;
  return voltageAtPin * ((r1 + r2) / r2); // a voltage divider is used to prevent to voltage go over 5v   
}

void handleVoltageState(float voltage) {
  if (voltage < VOLTAGE_LOWER_THRESHOLD) {
    handleLowVoltage();
  } else if (outputState && voltage > VOLTAGE_UPPER_THRESHOLD) {
    handleHighVoltage();
  }
}

// system will turn on
void handleLowVoltage() {
  if (!outputState) {
    outputState = true;
    digitalWrite(LED_POWER_SWITCH_PIN, HIGH);
  }

  applyCurrentMode(currentMode);
}

// system will shut down
void handleHighVoltage() {
  outputState = false;

  strip.clear();
  strip.show(); 

  digitalWrite(LED_POWER_SWITCH_PIN, LOW);

  for (int j=0; j < SLEEP_CYCLES; j++) { 
    shutDownWithWD (0b100001);  // 8 seconds
  }  
}

// ----- LED Mode control -----
void applyCurrentMode(uint8_t mode) {
  if (mode != LightMode::OFF_MODE) {
    digitalWrite(LED_POWER_SWITCH_PIN, HIGH);
  }

  switch (mode) {
    case LightMode::GREEN: 
      showGreen();
      break;
    case LightMode::RED: 
      showRed();     
      break;
    case LightMode::GREEN_RED:  
      showGreen();
      showRed();
      break;
    case LightMode::WHITE_FRONT: 
      showWhite(LightSectorWhiteMode::FRONT);
      break;
    case LightMode::WHITE_BACK: 
      showWhite(LightSectorWhiteMode::BACK);
      break;
    case LightMode::FULL_COMBO: 
      showGreen();
      showRed();
      showWhite(LightSectorWhiteMode::BACK);
      break;
    case LightMode::WHITE_ALL: 
      setAllWhite(maxBrightness);
      break;
    case LightMode::SOS: 
      if (!sosRunning) {
        initiateSOS();
      }
      handleSosAnimation();
      break;
    case LightMode::OFF_MODE: 
    default: // All off
      handleOffMode();
      break;
  }

  if (mode != LightMode::SOS) {
    strip.show();
  }
}


void showRed() {
  uint8_t firstLed = lightSectors.red[0];
  uint8_t lastLed = lightSectors.red[lightSectors.redCount - 1];

  #ifndef WOKWI
    colorLedsInRange(firstLed, lastLed, 255, 0, 0, 0, shouldResetStrip);
  #else
    colorLedsInRange(firstLed, lastLed, 255, 0, 0, shouldResetStrip);
  #endif

  shouldResetStrip = false;
}

void showGreen() {
  uint8_t firstLed = lightSectors.green[0];
  uint8_t lastLed = lightSectors.green[lightSectors.greenCount - 1];

  #ifndef WOKWI
    colorLedsInRange(firstLed, lastLed, 0, 255, 0, 0, shouldResetStrip);
  #else
    colorLedsInRange(firstLed, lastLed, 0, 255, 0, shouldResetStrip);
  #endif

  shouldResetStrip = false;
}

void showWhite(LightSectorWhiteMode whiteMode) {
  uint8_t firstLed = 0;
  uint8_t lastLed = 0;

  switch (whiteMode) {
  case LightSectorWhiteMode::BACK:
    firstLed = lightSectors.whiteBack[0];
    lastLed = lightSectors.whiteBack[lightSectors.whiteBackCount - 1];
    break;
  case LightSectorWhiteMode::FRONT:
    firstLed = lightSectors.whiteFront[0];
    lastLed = lightSectors.whiteFront[lightSectors.whiteFrontCount - 1];
    break;
  default:
    break;

  #ifndef WOKWI
    colorLedsInRange(firstLed, lastLed, 0, 0, 0, 255, shouldResetStrip);
  #else
    colorLedsInRange(firstLed, lastLed, 255, 255, 255, shouldResetStrip);
  #endif

  shouldResetStrip = false;
}

// ----- LED color helper -----
#ifndef WOKWI
void colorLedsInRange(uint8_t start, uint8_t end, uint8_t r, uint8_t g, uint8_t b, uint8_t w, bool reset) {
#else
void colorLedsInRange(uint8_t start, uint8_t end, uint8_t r, uint8_t g, uint8_t b, bool reset) {
#endif
  if (reset) {
    strip.clear();
    strip.setBrightness(maxBrightness);
  }

  for (int i = start; i <= end; i++) {
    #ifndef WOKWI
      strip.setPixelColor(i, strip.Color(r, g, b, w));
    #else
      strip.setPixelColor(i, strip.Color(r, g, b));
    #endif
  }
}

void initiateSOS() {
  sosIndex = 0;
  sosState = SosState::FADING_IN;
  sosLastTime = millis();
  fadeBrightness = 0;
  sosRunning = true;
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
        if (fadeBrightness < maxBrightness) {
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
          fadeBrightness -= fadeStepDuration;          
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
      if (now - sosLastTime >= (cycleEnd ? sosGapDuration : sosPauseDuration)) {
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
  }
}

void handleOffMode() {
  strip.clear();
  sosRunning = false;
  digitalWrite(LED_POWER_SWITCH_PIN, LOW);
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
  WDTCR |= (1 << WDCE) | (1 << WDE);  // Set WDCE, WDE
  WDTCR = (1 << WDIE) | time_len; // Set WDIE and delay
 
  ADCSRA &= ~(1 << ADEN);         // Stop the adc
  
  // Power off peripherals (guard if not defined)
  power_adc_disable();
#ifdef power_spi_disable
  power_spi_disable();
#endif
  power_timer0_disable();
  power_timer1_disable();
#ifdef power_usart0_disable
  power_usart0_disable();
#endif

  set_sleep_mode (SLEEP_MODE_PWR_DOWN);
  sleep_bod_disable();            // Disables brown-out detection during sleep
  interrupts();                   // Re-enable interrupts before sleeping
  ADCSRA |= _BV(ADEN);            // Re-enable ADC after wake-up
  sleep_mode();                   // Enter sleep mode (actually sleeps)

  // MCU wakes here
  // Re-enable peripherals
  power_all_enable();
  ADCSRA |= (1 << ADEN);
}