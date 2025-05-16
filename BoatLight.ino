#include <Adafruit_NeoPixel.h>
#include <EEPROM.h>
#include <avr/sleep.h>
#include <avr/wdt.h>

#define LED_PIN       0   // PB0
#define BUTTON_PIN    1   // PB1
#define VOLTAGE_PIN   A1  // PB2
#define NUM_LEDS      16
#define NUM_MODES     9
#define VOLTAGE_THRESHOLD 2.35
#define SLEEP_CYCLES 72   // Each Cycle is eight seconds

enum class SosState { IDLE, FADING_IN, ON, FADING_OUT, OFF };


Adafruit_NeoPixel strip(NUM_LEDS, LED_PIN, NEO_RGBW + NEO_KHZ800);
SosState sosState = SosState::IDLE;

const uint16_t sosPattern[] PROGMEM = {
  200,200,200,  // ...
  600,600,600,  // ---
  200,200,200   // ...
};

const uint8_t sosPause = 200;
const uint16_t sosGap = 1000;

uint8_t currentMode = 0;
uint8_t sosIndex = 0;
unsigned long sosLastTime = 0;
bool sosRunning = false;
uint8_t fadeBrightness = 0;

void setup() {

  strip.begin();
  strip.show(); // Turn off all LEDs initially

  pinMode(BUTTON_PIN, INPUT_PULLUP);

  currentMode = EEPROM.read(0);
  if (currentMode >= NUM_MODES) currentMode = 0;

  strip.fill(strip.Color(10,0,0,0));
  strip.show();
  delay(100);
  strip.clear();
  strip.show();
}

void loop() {
  float voltage = readVoltage();  

  if (voltage < VOLTAGE_THRESHOLD) {
    runMode(currentMode);    
  } else {
    strip.clear();
    strip.show();

    for (int j=0; j < SLEEP_CYCLES; j++) { 
      shutDown_with_WD (0b100001);  // 8 seconds
      delay(50);
    }
  }

  if (digitalRead(BUTTON_PIN) == LOW) {
    delay(200); // debounce
    currentMode = (currentMode + 1) % NUM_MODES;
    EEPROM.update(0, currentMode);
    while(digitalRead(BUTTON_PIN) == LOW); // wait for release
  }

  delay (100);
}

float readVoltage() {

  // 10-bit ADC (0-1023), Vcc assumed 5V
  int raw = analogRead(VOLTAGE_PIN);
  float voltage = raw * (5.0 / 1023);  // adjust if using 3.3V reference
  return voltage;
  
}

void runMode(uint8_t mode) {
  switch (mode) {
    case 0: // LED 6–10 green
      setRangeColor(5, 9, 0, 255, 0, 0); break;
    case 1: // LED 1–5 red
      setRangeColor(0, 4, 255, 0, 0, 0); break;
    case 2:  // LED 1–5 red, 6–10 green
      setRangeColor(0, 4, 255, 0, 0, 0);
      setRangeColor(5, 9, 0, 255, 0, 0); break;
    case 3: // LED 1–10 white
      setRangeColor(0, 9, 0, 0, 0, 255); break;
    case 4: // LED 11–16 white
      setRangeColor(10, 15, 0, 0, 0, 255); break;
    case 5: // 1–5 red, 6–10 green, 11–16 white
      setRangeColor(0, 4, 255, 0, 0, 0);
      setRangeColor(5, 9, 0, 255, 0, 0);
      setRangeColor(10, 15, 0, 0, 0, 255);
      break;
    case 6: // All white
      setRangeColor(0, 15, 0, 0, 0, 255); break;
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
    updateSos();
  } else {
    strip.show();
  }
}

void setRangeColor(uint8_t start, uint8_t end, uint8_t r, uint8_t g, uint8_t b, uint8_t w) {
  for (int i = start; i <= end; i++) {
    strip.setPixelColor(i, strip.Color(r, g, b, w));
  }
}

void setAllWhite(uint8_t brightness) {
  strip.setBrightness(brightness);
  for (int i = 0; i < NUM_LEDS; i++) {
    strip.setPixelColor(i, strip.Color(0, 0, 0, 255));
  }
  strip.show();
}

void updateSos() {
  static unsigned long lastStepTime = 0;
  static bool paused = false;
  static unsigned long pauseStart = 0;

  if (!sosRunning) return;

  unsigned long now = millis();
  int duration = pgm_read_word(&sosPattern[sosIndex]);

  switch (sosState) {
    case SosState::FADING_IN:
      if (now - lastStepTime >= 10) {
        lastStepTime = now;
        if (fadeBrightness < 255) {
          fadeBrightness += 5;
          strip.setBrightness(fadeBrightness);
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
          fadeBrightness -= 5;
          strip.setBrightness(fadeBrightness);
          setAllWhite(fadeBrightness);
        } else {
          strip.clear();
          strip.show();
          sosState = SosState::OFF;
          sosLastTime = now;
        }
      }
      break;

    case SosState::OFF:
      if (now - sosLastTime >= sosPause) {
        sosIndex++;
        if (sosIndex >= sizeof(sosPattern) / sizeof(sosPattern[0])) {
          sosIndex = 0;
          sosLastTime = now;
          sosState = SosState::OFF;

          // Use pause logic non-blockingly          
          if (!paused) {
            paused = true;
            pauseStart = now;
          } else if (now - pauseStart >= sosGap) {
            paused = false;
            sosState = SosState::FADING_IN;
            fadeBrightness = 0;
          }
        } else {
          sosState = SosState::FADING_IN;
          fadeBrightness = 0;
        }
      }
      break;

    default:
      break;
  }
}

//This function is called when the watchdog timer expires. 
ISR(WDT_vect) {
  wdt_disable();  // disable watchdog    
}

// This function puts the ATTINY in sleep mode.
// To save power, the ADCs are explicitly shutdown.
void shutDown_with_WD(const byte time_len) {
  noInterrupts();
  wdt_reset();
   
  MCUSR = 0;                      // Reset flags
  WDTCR |= 0b00011000;            // Set WDCE, WDE
  WDTCR =  0b01000110 | time_len; // Set WDIE and delay
 
  ADCSRA &= ~_BV(ADEN);           // Stop the adc

  set_sleep_mode (SLEEP_MODE_PWR_DOWN);
  sleep_bod_disable();
  interrupts();
  ADCSRA |= _BV(ADEN);            // ADC on
  sleep_mode();     
 
}