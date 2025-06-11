#include <Adafruit_NeoPixel.h>
#include <Bounce2.h>

#ifdef __AVR__
 #include <avr/power.h> // Required for 16 MHz Adafruit Trinket
#endif

#define LED_PIN     0
#define BUTTON_PIN  1

#define NUM_LEDS    16
#define NUM_MODES   9

enum class SosState { IDLE, FADING_IN, ON, FADING_OUT, OFF };

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

Adafruit_NeoPixel strip(NUM_LEDS, LED_PIN, NEO_GRBW + NEO_KHZ800);
Bounce2::Button modeButton = Bounce2::Button();

SosState sosState = SosState::IDLE;

const uint8_t fadeStep = 15;
const uint8_t sosPause = 250;
const uint16_t sosGap = 1500;

const uint16_t sosPattern[] PROGMEM = {
  250,250,250,  // ...
  750,750,750,  // ---
  250,250,250   // ...
};

#define SOS_PATTERN_LENGTH (sizeof(sosPattern) / sizeof(sosPattern[0]))

// Globals
bool sosRunning = false;
bool cycleEnd = false;
bool shouldResetStrip = false;

uint8_t lastSavedMode = 255;
uint8_t currentMode = LightMode::OFF_MODE;  
uint8_t sosIndex = 0;
uint8_t fadeBrightness = 0;

unsigned long sosLastTime = 0;
unsigned long lastModeChangeTime = 0;
unsigned long pressStartTime = 0;

// ----- Arduino setup -----
void setup() {
#if defined(__AVR_ATtiny85__) && (F_CPU == 16000000)
  clock_prescale_set(clock_div_1);
#endif

  initiateStrip();
  initiateModeButton();
}

void initiateStrip() {
  strip.begin();
  strip.clear();
  strip.show();
}

void initiateModeButton() {
  modeButton.attach(BUTTON_PIN, INPUT_PULLUP);
  modeButton.interval(25);
  modeButton.setPressedState(LOW); 
}

void loop() { 
  handleButtonPress();
  applyCurrentMode();
}

handleButtonPress() {
  modeButton.update();

  if (modeButton.fell()) {
    pressStartTime = millis();
  }

  if (modeButton.rose()) {
    unsigned long pressDuration = millis() - pressStartTime;

    if (pressDuration >= 1000) {
      currentMode = LightMode::OFF_MODE;
    } else {
      currentMode = (currentMode + 1) % NUM_MODES;
    }

    if (currentMode != lastSavedMode) {
        shouldResetStrip = true;
    }
  }
}

void applyCurrentMode(uint8_t mode) {
  switch (mode) {
    case LightMode::GREEN:
      showGreen();
      break;

    case LightMode::RED:
      showRed();
      break;
    
    case LightMode::GREEN_RED:
      showRed();
      showGreen();
      break;

    case LightMode::WHITE_1_10:
      showWhite(0, 9);
      break;

    case LightMode::WHITE_11_16:
      showWhite(10, 15);
      break;

    case LightMode::FULL_COMBO:
      showRed();
      showGreen();
      showWhite(10, 15);
      break;

    case LightMode::ALL_WHITE:
      showWhite(0, 15);
      break;
    
    case LightMode::SOS:
      if (!sosRunning) 
        initiateSOS();
      handleSosAnimation();
      break;
    
    case LightMode::OFF_MODE:
    default:
      strip.clear();
      sosRunning = false;
    break;
  }

  if (mode != LightMode::SOS) {
    strip.show();
  }
}

void showRed() {
  colorLedsInRange(0, 4, 255, 0, 0, 0, shouldResetStrip);
  shouldResetStrip = false;
}

void showGreen() {
  colorLedsInRange(5, 9, 0, 255, 0, 0, shouldResetStrip);
  shouldResetStrip = false;
}

void showWhite(uint8_t start, uint8_t end) {
  colorLedsInRange(start, end, 0, 0, 0, 255, shouldResetStrip);
  shouldResetStrip = false;
}

void initiateSOS() {
  sosIndex = 0;
  sosState = SosState::FADING_IN;
  sosLastTime = millis();
  fadeBrightness = 0;
  sosRunning = true;
}

void colorLedsInRange(uint8_t start, uint8_t end, uint8_t r, uint8_t g, uint8_t b, uint8_t w, bool reset) {
  if (reset) {
    strip.clear();
    strip.setBrightness(255);
  }

  for (uint8_t i = start; i <= end; i++) {
    strip.setPixelColor(i, strip.Color(r, g, b, w));
  }    
} 

void handleSosAnimation() {
  static unsigned long lastStepTime = 0;
  static bool paused = false;

  if (!sosRunning) return;

  unsigned long now = millis();
  uint8_t duration = pgm_read_word(&sosPattern[sosIndex]);

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
          sosState = SosState::OFF;
          sosLastTime = now;
          sosIndex++;

          if (sosIndex >= SOS_PATTERN_LENGTH) {
            sosIndex = 0;
            cycleEnd = true;
          }

          strip.clear();
          strip.show();
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
  }
}

void setAllWhite(uint8_t brightness) {
    strip.setBrightness(brightness);
    colorLedsInRange(0, 15, 0, 0, 0, 255, false);
    strip.show();
}

