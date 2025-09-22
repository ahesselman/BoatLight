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

// Pins
#define LED_PIN                 0     // PB0 - 5
#define BUTTON_MODE_PIN         1     // PB1 - 6 (b on remote)
#define VOLTAGE_PIN             A1    // PB2 - 7
#define LED_POWER_SWITCH_PIN    3     // PB3 - 2
#define BUTTON_ON_OFF_PIN       4     // PB4 - 3 (a on remote)

#define SOS_PATTERN_LENGTH (sizeof(sosPattern) / sizeof(sosPattern[0]))

// Constants
const uint8_t numberOfLeds = 17;      // Number of LEDs on the ledStrip. 
                                      // When this number is changed, 
                                      // the code will calculate how to divide the leds
const uint8_t numberOfModes = 9;      // Number of modes
const byte sleepTime = 0b100001;      // Sleep duration per cycle, 8 seconds
const uint8_t sleepCycles = 7;        // Number of sleep cycles when idle (7 x 8 sec = ~ 56 sec)
const uint8_t fadeStepDuration = 15;  // Step size to increase/decrease the brightness of the leds
const uint8_t sosPauseDuration = 250;
const uint8_t dotDuration = 250;
const uint16_t dashDuration = 750;
const uint16_t sosGapDuration = 1500;
const unsigned long saveDelayDuration = 3000; // EEPROM write delay, in ms
const uint8_t maxBrightness = 255;
const float colorDegrees = 112.5;           // Amount of degress for the red and green LEDs
const float voltageLowerThreshold = 3.50;   // Threshold LEDs on
const float voltageUpperThreshold = 4.50;   // Threshold LEDs off
const float resistor1Value = 10000.0;       // Voltage divider resistor 1 value, in Ohm
const float resistor2Value = 10000.0;       // Voltage divider resistor 1 value, in Ohm
constexpr float referenceVoltage = 5.0;
constexpr float dividerFactor = referenceVoltage / 1023.0;  // ADC scale factor

// SOS Pattern, timing for SOS Morse pattern (in milliseconds)
const uint16_t sosPattern[] PROGMEM = {
  dotDuration,dotDuration,dotDuration,  // ...
  dashDuration,dashDuration,dashDuration,  // ---
  dotDuration,dotDuration,dotDuration   // ...
};

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

enum LedStripWhiteSectorMode {
  FRONT = 0,
  BACK
};

struct LedStripSectors {
  uint8_t red[numberOfLeds];
  uint8_t green[numberOfLeds];
  uint8_t whiteBack[numberOfLeds];
  uint8_t whiteFront[numberOfLeds];

  uint8_t redCount;
  uint8_t greenCount;
  uint8_t whiteBackCount;
  uint8_t whiteFrontCount;
};

// Globals
bool ledsPowered = false;                   // Whether LEDs are powered
bool sosRunning = false;                    // Whether SOS is active
bool sosCycleEnd = false;                   // End of SOS cycle
bool shouldResetStrip = false;
uint8_t lastSavedMode = 255;                // Previously saved mode
uint8_t currentMode = LightMode::OFF_MODE;  // Current mode
uint8_t sosPatternStepIndex = 0;            // Current SOS pattern step
uint8_t fadeBrightness = 0;
unsigned long sosLastTime = 0;
unsigned long lastModeChangeTime = 0;
unsigned long pressStartTime = 0;
LedStripSectors ledStripSectors;
#ifndef WOKWI
  Adafruit_NeoPixel ledStrip(numberOfLeds, LED_PIN, NEO_GRBW + NEO_KHZ800);
#else
  Adafruit_NeoPixel ledStrip(numberOfLeds, LED_PIN, NEO_GRB + NEO_KHZ800);
#endif
Bounce2::Button modeButton = Bounce2::Button();
Bounce2::Button onOffButton = Bounce2::Button();
SosState sosState = SosState::IDLE;

// ----- Arduino setup -----
void setup() {
  // These lines are specifically to support the Adafruit Trinket 5V 16 MHz.
  // Any other board, you can remove this part (but no harm leaving it):
#if defined(__AVR_ATtiny85__) && (F_CPU == 16000000)
  clock_prescale_set(clock_div_1);
#endif

  initializeLedStripSectors();
  initiateLedStrip();
  initializeModeButton();
  initializeOnOffButton();
  readAndSanitizeCurrentMode();
}

// ----- Arduino loop -----
void loop() {
  handleOnOffButtonPress();
  handleModeButtonPress();
  storeCurrentMode();
  performAndHandleVoltageRead();
}

// ----- Setup functions -----

void initializeLedStripSectors() {
  // Define the led identifiers for the various color
  
  ledStripSectors.redCount = 0;
  ledStripSectors.greenCount = 0;
  ledStripSectors.whiteFrontCount = 0;
  ledStripSectors.whiteBackCount = 0;

  float degreesPerLed = 360.0 / numberOfLeds;
  uint8_t numberColoredLeds = round(colorDegrees / degreesPerLed);

  // Red LEDs
  for (uint8_t i = 0; i < numberColoredLeds; i++) {
    ledStripSectors.red[ledStripSectors.redCount++] = i + 1;
  }

  // Green LEDs
  for (uint8_t i = 0; i < numberColoredLeds; i++) {
    ledStripSectors.green[ledStripSectors.greenCount++] = numberColoredLeds + i + 1;
  }

  // White LEDs
  uint8_t numberWhiteLeds = numberOfLeds - (2 * numberColoredLeds);
  uint8_t startIdOfWhiteLed = 2 * numberColoredLeds;

  for (uint8_t i = 0; i < numberWhiteLeds; i++) {
    ledStripSectors.whiteBack[ledStripSectors.whiteBackCount++] = startIdOfWhiteLed + i + 1;
  }

  for (uint8_t i = 0; i < numberColoredLeds * 2; i++) {
    ledStripSectors.whiteFront[ledStripSectors.whiteFrontCount++] = i + 1; 
  }
}

void initiateLedStrip() {
  ledStrip.begin();
  ledStrip.clear();
  ledStrip.show();

  // Configure the pin that switches the power for the ledstrip
  // Initially turned off
  pinMode(LED_POWER_SWITCH_PIN, OUTPUT);
  digitalWrite(LED_POWER_SWITCH_PIN, LOW);
}

void initializeModeButton() {
  modeButton.attach(BUTTON_MODE_PIN, INPUT);
  modeButton.interval(25);
  modeButton.setPressedState(HIGH); 
}

void initializeOnOffButton() {
  onOffButton.attach(BUTTON_ON_OFF_PIN, INPUT);
  onOffButton.interval(25);
  onOffButton.setPressedState(HIGH);
}

void readAndSanitizeCurrentMode() {
  currentMode = EEPROM.read(0);
  if (currentMode >= numberOfModes) currentMode = LightMode::GREEN;
  lastSavedMode = currentMode;
}

// ----- Button and EEPROM Handling -----

void handleOnOffButtonPress() {
  onOffButton.update();
  
  // From LOW to HIGH
  if (onOffButton.rose()) {
    if (currentMode != LightMode::OFF_MODE) {
      currentMode = LightMode::OFF_MODE;
    } else {
      readAndSanitizeCurrentMode();
    }
  }
}

void handleModeButtonPress() {
  modeButton.update();

  // From LOW to HIGH
  if (modeButton.rose()) {
    currentMode = (currentMode + 1) % numberOfModes;
  
    if (currentMode != lastSavedMode) {
      shouldResetStrip = true;
      lastModeChangeTime = millis(); // update timer for EEPROM saving
    }
  }
}

void storeCurrentMode() {
  // The current mode will only be stored if it is not in mode off, 
  // changed and set for saveDelayDuration time
  if (currentMode != LightMode::OFF_MODE && 
      currentMode != lastSavedMode && 
      millis() - lastModeChangeTime > saveDelayDuration) {    
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
  
  float averageRaw = sum / 10.0;

  float voltageAtPin = averageRaw * dividerFactor;
  return voltageAtPin * ((resistor1Value + resistor2Value) / resistor2Value); // a voltage divider is used to prevent to voltage go over 5v   
}

void handleVoltageState(float voltage) {
  if (voltage < voltageLowerThreshold) {
    handleLowVoltage(); // system will turn on
  } else if (ledsPowered && voltage > voltageUpperThreshold) {
    handleHighVoltage(); // system will shut down
  }
}

void handleLowVoltage() {
  if (!ledsPowered) {
    ledsPowered = true;
    digitalWrite(LED_POWER_SWITCH_PIN, HIGH);
  }

  applyCurrentMode(currentMode);
}

void handleHighVoltage() {
  ledsPowered = false;

  ledStrip.clear();
  ledStrip.show(); 

  digitalWrite(LED_POWER_SWITCH_PIN, LOW);

  for (int j=0; j < sleepCycles; j++) { 
    shutDownWithWD(sleepTime); 
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
      showWhite(LedStripWhiteSectorMode::FRONT);
      break;
    case LightMode::WHITE_BACK: 
      showWhite(LedStripWhiteSectorMode::BACK);
      break;
    case LightMode::FULL_COMBO: 
      showGreen();
      showRed();
      showWhite(LedStripWhiteSectorMode::BACK);
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
    ledStrip.show();
  }
}

void showRed() {
  uint8_t firstLed = ledStripSectors.red[0];
  uint8_t lastLed = ledStripSectors.red[ledStripSectors.redCount - 1];

  #ifndef WOKWI
    colorLedsInRange(firstLed, lastLed, 255, 0, 0, 0, shouldResetStrip);
  #else
    colorLedsInRange(firstLed, lastLed, 255, 0, 0, shouldResetStrip);
  #endif

  shouldResetStrip = false;
}

void showGreen() {
  uint8_t firstLed = ledStripSectors.green[0];
  uint8_t lastLed = ledStripSectors.green[ledStripSectors.greenCount - 1];

  #ifndef WOKWI
    colorLedsInRange(firstLed, lastLed, 0, 255, 0, 0, shouldResetStrip);
  #else
    colorLedsInRange(firstLed, lastLed, 0, 255, 0, shouldResetStrip);
  #endif

  shouldResetStrip = false;
}

void showWhite(LedStripWhiteSectorMode whiteMode) {
  uint8_t firstLed = 0;
  uint8_t lastLed = 0;

  switch (whiteMode) {
    case LedStripWhiteSectorMode::BACK:
      firstLed = ledStripSectors.whiteBack[0];
      lastLed = ledStripSectors.whiteBack[ledStripSectors.whiteBackCount - 1];
      break;
    case LedStripWhiteSectorMode::FRONT:
      firstLed = ledStripSectors.whiteFront[0];
      lastLed = ledStripSectors.whiteFront[ledStripSectors.whiteFrontCount - 1];
      break;
    default:
      break;
  }

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
    ledStrip.clear();
    ledStrip.setBrightness(maxBrightness);
  }

  for (int i = start; i <= end; i++) {
    #ifndef WOKWI
      ledStrip.setPixelColor(i, ledStrip.Color(r, g, b, w));
    #else
      ledStrip.setPixelColor(i, ledStrip.Color(r, g, b));
    #endif
  }
}

void initiateSOS() {
  sosPatternStepIndex = 0;
  sosState = SosState::FADING_IN;
  sosLastTime = millis();
  fadeBrightness = 0;
  sosRunning = true;
}

// ----- All-white Helper for SOS -----
void setAllWhite(uint8_t brightness) {
  ledStrip.setBrightness(brightness);

  for (int i = 0; i < numberOfLeds; i++) {
      #ifndef WOKWI
        ledStrip.setPixelColor(i, ledStrip.Color(0, 0, 0, 255));
      #else
        ledStrip.setPixelColor(i, ledStrip.Color(255, 255, 255));
      #endif
  }

  ledStrip.show();
}

// ----- SOS Blinking Animation Handler -----
void handleSosAnimation() {
  static unsigned long lastStepTime = 0;
  static bool paused = false;

  if (!sosRunning) return;

  unsigned long now = millis();
  int duration = pgm_read_word(&sosPattern[sosPatternStepIndex]);

  switch (sosState) {
    case SosState::FADING_IN:
      if (now - lastStepTime >= 10) {
        lastStepTime = now;
        if (fadeBrightness < maxBrightness) {
          fadeBrightness += fadeStepDuration;          
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
          ledStrip.clear();
          ledStrip.show();
          sosState = SosState::OFF;
          sosLastTime = now;

          sosPatternStepIndex++;
          if (sosPatternStepIndex >= SOS_PATTERN_LENGTH) {
            sosPatternStepIndex = 0;
            sosCycleEnd = true;
          }          
        }
      }
      break;

    case SosState::OFF:
      if (now - sosLastTime >= (sosCycleEnd ? sosGapDuration : sosPauseDuration)) {
        lastStepTime = now;          
        if (!paused) {
          paused = true;
        } else {
          paused = false;
          sosCycleEnd = false;
          sosState = SosState::FADING_IN;
          fadeBrightness = 0;          
        }
      }
      break;
  }
}

void handleOffMode() {
  ledStrip.clear();
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
  noInterrupts();                 // Disable interrupts temporarily
  wdt_reset();                    // Reset the watchdog
   
  MCUSR = 0;                      // Clear reset flags
  WDTCR |= (1 << WDCE) | (1 << WDE);  // Set WDCE, WDE
  WDTCR = (1 << WDIE) | time_len; // Enable watchdog interrupt, set timeout
 
  // Disable ADC and peripherals before sleep
  ADCSRA &= ~(1 << ADEN);
  power_adc_disable();
#ifdef power_spi_disable
  power_spi_disable();
#endif
  power_timer0_disable();
  power_timer1_disable();
#ifdef power_usart0_disable
  power_usart0_disable();
#endif

  // Set sleep mode
  set_sleep_mode(SLEEP_MODE_PWR_DOWN);
  sleep_bod_disable();            // Disable brown-out detection during sleep

  interrupts();                   // Re-enable interrupts before sleeping
  sleep_mode();                   // Enter sleep mode (MCU actually sleeps here)

  // ---- MCU wakes here after WDT interrupt ----
  // Re-enable peripherals and ADC
  power_all_enable();
  ADCSRA |= (1 << ADEN);
}