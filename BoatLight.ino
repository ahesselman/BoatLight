// 22-01-2026
#include <Adafruit_NeoPixel.h>
#include <Bounce2.h>
#include <EEPROM.h>
#include <avr/sleep.h>
#include <avr/wdt.h>

#ifdef __AVR__
#include <avr/power.h>
#endif

// ------------------------------------------------------------
// Pin definitions (ATtiny85 internal vs. external pins)
// ------------------------------------------------------------
//
#define ADC_INPUT A0        // RST    - pin 1 (d on remote)
#define BUTTON_SOS_PIN 3    // PB3    - pin 2 (c on remote)
#define BUTTON_ON_OFF_PIN 4 // PB4    - pin 3 (a on remote) \
                            // GND    - Pin 4
#define LED_PIN 0           // PB0    - pin 5
#define BUTTON_MODE_PIN 1   // PB1    - pin 6 (b on remote)
#define VOLTAGE_PIN A1      // PB2    - pin 7 \
                            // VCC    - pin 8

#define SOS_LETTERS 3

// ------------------------------------------------------------
// Enums
// ------------------------------------------------------------
enum LightMode
{
  // Basic modes
  GREEN = 0,
  RED,
  GREEN_RED,
  WHITE_FRONT,
  WHITE_BACK,
  FULL_COMBO,
  WHITE_ALL,
  WHITE_FLASH,
  // Extra modes (only when enabled)
  RED_ALL,
  RED_FLASH,
  GREEN_ALL,
  GREEN_FLASH,
  BLUE_ALL,
  BLUE_FLASH,
  YELLOW_ALL,
  YELLOW_FLASH,
  MAGENTA_ALL,
  MAGENTA_FLASH,
  CYAN_ALL,
  CYAN_FLASH,
  // Special modes
  SOS,       // <-- always third to last, will only be activated when pressing sos button
  OFF_MODE,  // <-- always second to last, will only be activated when pressing on/off button
  MODE_COUNT // <-- always last — auto-updates
};

enum LedStripWhiteSectorMode
{
  FRONT = 0,
  BACK
};

// ------------------------------------------------------------
// Constants
// ------------------------------------------------------------
const uint8_t numberOfLeds = 17;
const uint8_t numberOfModes = LightMode::MODE_COUNT;
const byte sleepTime = 0b100001; // 8 seconds
const uint8_t sleepCycles = 1;   // 1 cycles * 8 seconds = 8 seconds total
const unsigned long saveDelayDuration = 3000;
const uint8_t maxBrightness = 255;
const uint8_t maxBrightnessWhite = 127;
const float colorDegrees = 112.5;
const float voltageLowerThreshold = 0.75; // was 1.75
const float voltageUpperThreshold = 1.50; // was 3.25
const float resistor1Value = 10000.0;
const float resistor2Value = 10000.0;
constexpr float referenceVoltage = 5.0;
constexpr float dividerFactor = referenceVoltage / 1023.0;

/* -- Morse timing constants --
    1. The length of a dot is 1 time unit.
    2. A dash is 3 time units.
    3. The space between symbols (dots and dashes) of the same letter is 1 time unit.
    4. The space between letters is 3 time units.
    5. The space between words is 7 time units.
*/
const uint16_t morseTimeUnit = 250;
const uint16_t symbolPauseDuration = 1 * morseTimeUnit;
const uint16_t letterPauseDuration = 3 * morseTimeUnit;
const uint16_t wordPauseDuration = 7 * morseTimeUnit;

const uint8_t sosPattern[][4] PROGMEM = {
    {1, 1, 1, 0}, // S = dot dot dot
    {3, 3, 3, 0}, // O = dash dash dash
    {1, 1, 1, 0}  // S = dot dot dot
};

// ------------------------------------------------------------
// Data structures
// ------------------------------------------------------------
struct LedStripSectors
{
  uint8_t red[numberOfLeds];
  uint8_t green[numberOfLeds];
  uint8_t whiteBack1[numberOfLeds];
  uint8_t whiteBack2[numberOfLeds];
  uint8_t whiteFront[numberOfLeds];

  uint8_t redCount;
  uint8_t greenCount;
  uint8_t whiteBackCount1;
  uint8_t whiteBackCount2;
  uint8_t whiteFrontCount;
};

// ------------------------------------------------------------
// Globals
// ------------------------------------------------------------
bool shouldResetStrip = false;
bool sosInitialized = false;
bool extraModesEnabled = false;
uint8_t currentMode = LightMode::OFF_MODE;
uint8_t lastActiveMode = LightMode::GREEN;
unsigned long lastModeChangeTime = 0;
unsigned long pressStartTime = 0;
uint8_t sosPatternStepIndex = 0;
unsigned long sosLastTime = 0;
uint8_t ledCount = 0;
float voltageADCReference = 0;

LedStripSectors ledStripSectors;

Adafruit_NeoPixel ledStrip(numberOfLeds, LED_PIN, NEO_GRBW + NEO_KHZ800);

Bounce2::Button modeButton = Bounce2::Button();
Bounce2::Button onOffButton = Bounce2::Button();
Bounce2::Button sosButton = Bounce2::Button();

// ------------------------------------------------------------
// Function Declarations
// ------------------------------------------------------------
void initializeAndTestLedStrip();
void initializeLedStripSectors();
void initializeButton(Bounce2::Button &button, uint8_t pin, uint16_t interval = 25);
void initializeExtraModesButton(uint8_t pin);
void readAndSanitizeStoredMode();

void handleOnOffButtonPress();
void handleModeButtonPress();
void handleSosButtonPress();
void handleExtraModesButtonPress();
void storeCurrentMode();

float readSolarVoltage();
void performAndHandleVoltageRead();
void handleVoltageState(float voltage);
void handleLowVoltage();
void handleHighVoltage();
void handleRunMode(uint8_t r, uint8_t g, uint8_t b, uint8_t w);

void applyCurrentMode(uint8_t mode);
void showRed();
void showGreen();
void showWhite(LedStripWhiteSectorMode mode);
void colorLedsInRange(uint8_t start, uint8_t end, uint8_t r, uint8_t g, uint8_t b, uint8_t w, bool reset);
void handleSosAnimation();
void handleFlashMode(uint8_t r, uint8_t g, uint8_t b, uint8_t w, uint8_t brightness);
void setAllPerColor(uint8_t r, uint8_t g, uint8_t b, uint8_t w, uint8_t brightness, bool doShow = true, bool setBrightnessFlag = true);
void handleOffMode();
void stopExtraModes();
void shutDownWithWD(uint8_t wdt_period);

// ------------------------------------------------------------
// Setup
// ------------------------------------------------------------
void setup()
{
#if defined(__AVR_ATtiny85__) && (F_CPU == 16000000)
  clock_prescale_set(clock_div_1);
#endif
  initializeAndTestLedStrip();
  initializeLedStripSectors();

  initializeButton(modeButton, BUTTON_MODE_PIN);
  initializeButton(onOffButton, BUTTON_ON_OFF_PIN);
  initializeButton(sosButton, BUTTON_SOS_PIN);
  initializeExtraModesButton(ADC_INPUT);

  readAndSanitizeStoredMode();
}

// ------------------------------------------------------------
// Loop
// ------------------------------------------------------------
void loop()
{
  handleExtraModesButtonPress(); // knob D
  handleOnOffButtonPress();      // knob A
  handleModeButtonPress();       // knob B
  handleSosButtonPress();        // knob C
  storeCurrentMode();
  performAndHandleVoltageRead();
}

// ------------------------------------------------------------
// LED Strip Setup and Test
// ------------------------------------------------------------
void initializeAndTestLedStrip()
{
  ledStrip.begin();
  ledStrip.clear();
  ledStrip.show();
  ledStrip.setBrightness(maxBrightness);

  ledStrip.clear();
  while (ledCount < numberOfLeds)
    handleRunMode(0, 0, 0, maxBrightness);

  ledStrip.clear();
  ledStrip.show();
  ledStrip.setBrightness(maxBrightness);
}

// ------------------------------------------------------------
// LED Sector Setup
// ------------------------------------------------------------
void initializeLedStripSectors()
{
  ledStripSectors.redCount = 0;
  ledStripSectors.greenCount = 0;
  ledStripSectors.whiteFrontCount = 0;
  ledStripSectors.whiteBackCount1 = 0;
  ledStripSectors.whiteBackCount2 = 0;

  float degreesPerLed = 360.0 / numberOfLeds;
  uint8_t numberColoredLeds = round(colorDegrees / degreesPerLed);
  uint8_t numberWhiteLeds = numberOfLeds - (2 * numberColoredLeds);
  uint8_t numberWhiteLedsBack1 = numberWhiteLeds / 2;
  uint8_t numberWhiteLedsBack2 = numberWhiteLeds - numberWhiteLedsBack1;

  for (uint8_t i = 0; i < numberWhiteLedsBack1; i++)
  {
    ledStripSectors.whiteBack1[ledStripSectors.whiteBackCount1++] = i;
  }

  for (uint8_t i = 0; i < numberColoredLeds; i++)
  {
    ledStripSectors.green[ledStripSectors.greenCount++] = numberWhiteLedsBack1 + i;
  }

  for (uint8_t i = 0; i < numberColoredLeds; i++)
  {
    ledStripSectors.red[ledStripSectors.redCount++] = numberColoredLeds + numberWhiteLedsBack1 + i;
  }

  for (uint8_t i = 0; i < numberWhiteLedsBack2; i++)
  {
    ledStripSectors.whiteBack2[ledStripSectors.whiteBackCount2++] = (2 * numberColoredLeds) + numberWhiteLedsBack1 + i;
  }

  for (uint8_t i = 0; i < numberColoredLeds * 2; i++)
  {
    ledStripSectors.whiteFront[ledStripSectors.whiteFrontCount++] = numberWhiteLedsBack1 + i;
  }
}

// ------------------------------------------------------------
// Button Setup
// ------------------------------------------------------------
void initializeButton(Bounce2::Button &button, uint8_t pin, uint16_t interval)
{
  button.attach(pin, INPUT);
  button.interval(interval);
  button.setPressedState(HIGH);
}

void initializeExtraModesButton(uint8_t pin)
{
  pinMode(pin, INPUT);
  voltageADCReference = analogRead(pin);
}

// ------------------------------------------------------------
// Fetch Stored Mode
// ------------------------------------------------------------
void readAndSanitizeStoredMode()
{
  uint8_t stored = EEPROM.read(0);
  if (stored >= numberOfModes)
    stored = LightMode::GREEN;
  currentMode = stored;
  lastActiveMode = stored;
  extraModesEnabled = (lastActiveMode > LightMode::WHITE_FLASH);
}

// ------------------------------------------------------------
// Button Handling
// ------------------------------------------------------------

void handleExtraModesButtonPress()
{
  static bool pressed = false;

  float adc = analogRead(ADC_INPUT);
  bool isHigh = adc > (voltageADCReference + 100);

  if (isHigh && !pressed)
  {
    extraModesEnabled = !extraModesEnabled;
    pressed = true;
  }

  if (!isHigh && pressed)
  {
    voltageADCReference = analogRead(ADC_INPUT);
    pressed = false;
  }
}

void handleOnOffButtonPress()
{
  onOffButton.update();
  if (!onOffButton.rose())
    return;

  if (currentMode != LightMode::OFF_MODE)
  {
    lastActiveMode = currentMode;
    currentMode = LightMode::OFF_MODE;
  }
  else
  {
    currentMode = lastActiveMode;
  }
  shouldResetStrip = true;
}

void handleModeButtonPress()
{
  modeButton.update();
  if (!modeButton.rose())
    return;

  uint8_t nextMode = lastActiveMode;

  do
  {
    nextMode = (nextMode + 1) % (numberOfModes - 1); // last mode is OFF_MODE
  } while (nextMode == LightMode::SOS);

  currentMode = nextMode;
  lastActiveMode = currentMode;
  shouldResetStrip = true;
  lastModeChangeTime = millis();
}

void handleSosButtonPress()
{
  sosButton.update();
  if (!sosButton.rose())
    return;

  if (currentMode != LightMode::SOS)
  {
    lastActiveMode = currentMode; // store current mode to return to
    currentMode = LightMode::SOS;
  }
  else
  {
    currentMode = lastActiveMode; // toggle off SOS
  }

  shouldResetStrip = true;
}

// ------------------------------------------------------------
// EEPROM Mode Saving
// ------------------------------------------------------------
void storeCurrentMode()
{
  if (currentMode != LightMode::OFF_MODE && millis() - lastModeChangeTime > saveDelayDuration && EEPROM.read(0) != currentMode)
  {
    EEPROM.update(0, currentMode);
  }
}

// ------------------------------------------------------------
// Voltage Reading & Power Control
// ------------------------------------------------------------
void performAndHandleVoltageRead()
{
  float solarVoltage = readSolarVoltage();
  handleVoltageState(solarVoltage);
}

float readSolarVoltage()
{
  long sum = 0;
  for (int i = 0; i < 10; i++)
  {
    sum += analogRead(VOLTAGE_PIN);
    delay(2);
  }
  float avg = sum / 10.0;
  float voltageAtPin = avg * dividerFactor;
  return voltageAtPin * ((resistor1Value + resistor2Value) / resistor2Value); // voltage correction due to voltage divider 10K | 10K
}

void handleVoltageState(float voltage)
{
  if (voltage < voltageLowerThreshold)
  {
    handleLowVoltage();
  }
  else if (voltage > voltageUpperThreshold)
  {
    handleHighVoltage();
  }
}

void handleLowVoltage()
{
  applyCurrentMode(currentMode);
}

void handleHighVoltage()
{
  ledStrip.clear();
  ledStrip.show();

  for (int j = 0; j < sleepCycles; j++)
  {
    shutDownWithWD(sleepTime);
  }
}

// ------------------------------------------------------------
// LED Modes
// ------------------------------------------------------------
void applyCurrentMode(uint8_t mode)
{
  switch (mode)
  {
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
    showWhite(FRONT);
    break;
  case LightMode::WHITE_BACK:
    showWhite(BACK);
    break;
  case LightMode::FULL_COMBO:
    showGreen();
    showRed();
    showWhite(BACK);
    break;
  case LightMode::WHITE_ALL:
    setAllPerColor(0, 0, 0, maxBrightnessWhite, maxBrightness, true, true);
    break;
  case LightMode::WHITE_FLASH:
    handleFlashMode(0, 0, 0, maxBrightness, maxBrightness);
    break;
  case LightMode::RED_ALL:
    if (extraModesEnabled)
      setAllPerColor(maxBrightness, 0, 0, 0, maxBrightness);
    else
      stopExtraModes();
    break;
  case LightMode::RED_FLASH:
    if (extraModesEnabled)
      handleFlashMode(maxBrightness, 0, 0, 0, maxBrightness);
    else
      stopExtraModes();
    break;
  case LightMode::GREEN_ALL:
    if (extraModesEnabled)
      setAllPerColor(0, maxBrightness, 0, 0, maxBrightness);
    else
      stopExtraModes();
    break;
  case LightMode::GREEN_FLASH:
    if (extraModesEnabled)
      handleFlashMode(0, maxBrightness, 0, 0, maxBrightness);
    else
      stopExtraModes();
    break;
  case LightMode::BLUE_ALL:
    if (extraModesEnabled)
      setAllPerColor(0, 0, maxBrightness, 0, maxBrightness);
    else
      stopExtraModes();
    break;
  case LightMode::BLUE_FLASH:
    if (extraModesEnabled)
      handleFlashMode(0, 0, maxBrightness, 0, maxBrightness);
    else
      stopExtraModes();
    break;
  case LightMode::YELLOW_ALL:
    if (extraModesEnabled)
      setAllPerColor(maxBrightness, maxBrightness, 0, 0, maxBrightness);
    else
      stopExtraModes();
    break;
  case LightMode::YELLOW_FLASH:
    if (extraModesEnabled)
      handleFlashMode(maxBrightness, maxBrightness, 0, 0, maxBrightness);
    else
      stopExtraModes();
    break;
  case LightMode::MAGENTA_ALL:
    if (extraModesEnabled)
      setAllPerColor(maxBrightness, 0, maxBrightness, 0, maxBrightness);
    else
      stopExtraModes();
    break;
  case LightMode::MAGENTA_FLASH:
    if (extraModesEnabled)
      handleFlashMode(maxBrightness, 0, maxBrightness, 0, maxBrightness);
    else
      stopExtraModes();
    break;
  case LightMode::CYAN_ALL:
    if (extraModesEnabled)
      setAllPerColor(0, maxBrightness, maxBrightness, 0, maxBrightness);
    else
      stopExtraModes();
    break;
  case LightMode::CYAN_FLASH:
    if (extraModesEnabled)
      handleFlashMode(0, maxBrightness, maxBrightness, 0, maxBrightness);
    else
      stopExtraModes();
    break;
  case LightMode::SOS:
    handleSosAnimation();
    break;
  case LightMode::OFF_MODE:
  default:
    handleOffMode();
    break;
  }

  if (mode != LightMode::SOS && sosInitialized)
    sosInitialized = false;

  if (mode != LightMode::SOS && mode != LightMode::WHITE_FLASH)
    ledStrip.show();
}

void stopExtraModes()
{
  currentMode = LightMode::GREEN;
  lastActiveMode = currentMode;
}

// ------------------------------------------------------------
// SOS Mode
// ------------------------------------------------------------
void handleSosAnimation()
{
  static unsigned long lastActionTime = 0;
  static uint8_t letterIndex = 0;
  static uint8_t symbolIndex = 0;
  static bool ledOn = false;
  static uint16_t currentDelay = 0;

  if (currentMode != LightMode::SOS)
  {
    sosInitialized = false;
    return;
  }

  unsigned long now = millis();

  if (!sosInitialized)
  {
    sosInitialized = true;
    letterIndex = 0;
    symbolIndex = 0;
    ledOn = false;
    lastActionTime = now;
    currentDelay = 0;

    ledStrip.setBrightness(maxBrightness);
    ledStrip.clear();
    ledStrip.show();
    return;
  }

  if (now - lastActionTime < currentDelay)
  {
    return;
  }

  if (!ledOn)
  {
    uint8_t unit = pgm_read_byte(&sosPattern[letterIndex][symbolIndex]);

    if (unit == 0)
    {
      letterIndex++;
      symbolIndex = 0;

      if (letterIndex >= SOS_LETTERS)
      {
        // end of word; start over after word gap
        letterIndex = 0;
        lastActionTime = now;
        currentDelay = wordPauseDuration;
        ledStrip.clear();
        ledStrip.show();
        return;
      }
      else
      {
        // letter gap
        lastActionTime = now;
        currentDelay = letterPauseDuration;
        ledStrip.clear();
        ledStrip.show();
        return;
      }
    }

    setAllPerColor(0, 0, 0, maxBrightness, maxBrightness, true, false);
    ledOn = true;
    lastActionTime = now;
    currentDelay = (uint16_t)unit * morseTimeUnit;
    symbolIndex++;
    return;
  }

  if (ledOn)
  {
    ledStrip.clear();
    ledStrip.show();
    ledOn = false;
    lastActionTime = now;
    currentDelay = symbolPauseDuration;
  }
}

// ------------------------------------------------------------
// FLASH Mode
// ------------------------------------------------------------
void handleFlashMode(uint8_t r, uint8_t g, uint8_t b, uint8_t w, uint8_t maxbrightness)
{
  static unsigned long lastFlashTime = 0;
  static bool flashOn = false;
  unsigned long now = millis();
  if (now - lastFlashTime >= 500)
  {
    lastFlashTime = now;
    flashOn = !flashOn;
    if (flashOn)
      setAllPerColor(r, g, b, w, maxBrightness);
    else
    {
      ledStrip.clear();
      ledStrip.show();
    }
  }
}

// ------------------------------------------------------------
// Helpers
// ------------------------------------------------------------

void setAllPerColor(uint8_t r, uint8_t g, uint8_t b, uint8_t w, uint8_t brightness, bool doShow, bool setBrightnessFlag)
{
  if (setBrightnessFlag)
    ledStrip.setBrightness(brightness);
  for (int i = 0; i < numberOfLeds; i++)
    ledStrip.setPixelColor(i, ledStrip.Color(r, g, b, w));
  if (doShow)
    ledStrip.show();
}

void handleRunMode(uint8_t r, uint8_t g, uint8_t b, uint8_t w)
{
  static unsigned long lastRunTime = 0;
  unsigned long now = millis();
  if (now - lastRunTime >= 59)
  {
    lastRunTime = now;
    ledStrip.clear();
    ledStrip.setPixelColor(ledCount, ledStrip.Color(r, g, b, w));
    ledStrip.show();

    ledCount++;
    if (ledCount >= numberOfLeds)
    {
      ledCount = 0;
      lastRunTime = 0;
    }
  }
}

void showRed()
{
  if (ledStripSectors.redCount == 0)
    return;
  uint8_t first = ledStripSectors.red[0];
  uint8_t last = ledStripSectors.red[ledStripSectors.redCount - 1];
  if (first > last || last >= numberOfLeds)
    return;
  colorLedsInRange(first, last, maxBrightness, 0, 0, 0, shouldResetStrip);
  shouldResetStrip = false;
}

void showGreen()
{
  if (ledStripSectors.greenCount == 0)
    return;
  uint8_t first = ledStripSectors.green[0];
  uint8_t last = ledStripSectors.green[ledStripSectors.greenCount - 1];
  if (first > last || last >= numberOfLeds)
    return;
  colorLedsInRange(first, last, 0, maxBrightness, 0, 0, shouldResetStrip);
  shouldResetStrip = false;
}

void showWhite(LedStripWhiteSectorMode mode)
{
  uint8_t first = 0, last = 0;
  if (mode == BACK)
  {
    first = ledStripSectors.whiteBack1[0];
    last = ledStripSectors.whiteBack1[ledStripSectors.whiteBackCount1 - 1];
    colorLedsInRange(first, last, 0, 0, 0, maxBrightnessWhite, shouldResetStrip);
    first = ledStripSectors.whiteBack2[0];
    last = ledStripSectors.whiteBack2[ledStripSectors.whiteBackCount2 - 1];
    colorLedsInRange(first, last, 0, 0, 0, maxBrightnessWhite, shouldResetStrip);
  }
  else
  {
    first = ledStripSectors.whiteFront[0];
    last = ledStripSectors.whiteFront[ledStripSectors.whiteFrontCount - 1];
    colorLedsInRange(first, last, 0, 0, 0, maxBrightnessWhite, shouldResetStrip);
  }

  shouldResetStrip = false;
}

void colorLedsInRange(uint8_t start, uint8_t end, uint8_t r, uint8_t g, uint8_t b, uint8_t w, bool reset)
{
  if (start >= numberOfLeds || end >= numberOfLeds)
    return;
  if (start > end)
    return;

  if (reset)
  {
    ledStrip.clear();
    ledStrip.setBrightness(maxBrightness);
  }
  for (int i = start; i <= end; i++)
  {
    ledStrip.setPixelColor(i, ledStrip.Color(r, g, b, w));
  }
}

void handleOffMode()
{
  ledStrip.clear();
  ledStrip.show();
}

// ------------------------------------------------------------
// Watchdog Sleep
// ------------------------------------------------------------
ISR(WDT_vect)
{
  wdt_disable();
}

void shutDownWithWD(uint8_t wdt_period)
{
  noInterrupts();
  wdt_reset();

  MCUSR = 0;
#if defined(WDTCSR)
  WDTCSR = (1 << WDCE) | (1 << WDE); // Old vs new cores compatible
  WDTCSR = (1 << WDIE) | wdt_period;
#elif defined(WDTCR)
  WDTCR = (1 << WDCE) | (1 << WDE);
  WDTCR = (1 << WDIE) | wdt_period;
#else
#error "Watchdog Timer register not defined for this MCU!"
#endif

  // Disable ADC and peripherals
  ADCSRA &= ~(1 << ADEN);
  power_all_disable();

  set_sleep_mode(SLEEP_MODE_PWR_DOWN);
  sleep_bod_disable();

  interrupts();
  sleep_mode(); // sleep until WDT interrupt

  // wakes here
  power_all_enable();
  _delay_ms(10);
  ADCSRA |= (1 << ADEN);
}
