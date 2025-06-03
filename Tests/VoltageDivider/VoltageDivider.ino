#include <avr/sleep.h>
#include <avr/wdt.h>

#define VOLTAGE_PIN             A1  // PB2
#define LED_POWER_SWITCH_PIN    3   // PB3

#define VOLTAGE_LOWER_THRESHOLD 3.50  // Threshold LEDs on
#define VOLTAGE_UPPER_THRESHOLD 4.50  // Threshold LEDs off

const float r1 = 10000.0;             // Voltage divider resistor 1 value, in Ohm
const float r2 = 10000.0;             // Voltage divider resistor 1 value, in Ohm
constexpr float referenceVoltage = 5.0;
constexpr float dividerFactor = referenceVoltage / 1023.0;  // ADC scale factor

bool outputState = false;  // Track current output state

void setup() {
  pinMode(LED_POWER_SWITCH_PIN, OUTPUT);
  digitalWrite(LED_POWER_SWITCH_PIN, LOW);
}

void loop()
{
  performVoltageRead();
  delay(500);
}

void performVoltageRead() {
  float solarVoltage = readSolarVoltage();

  if (solarVoltage < VOLTAGE_LOWER_THRESHOLD) {
    if (!outputState) {
      outputState = true;
      digitalWrite(LED_POWER_SWITCH_PIN, HIGH);
    }
  } else if (outputState && solarVoltage > VOLTAGE_UPPER_THRESHOLD) {
    outputState = false;
    digitalWrite(LED_POWER_SWITCH_PIN, LOW);
  }

}

float readSolarVoltage() {
  long sum = 0;
  for (int i = 0; i < 10; i++)
  {
    sum += analogRead(VOLTAGE_PIN);
    delay(2);    
  }
  
  float averagRaw = sum / 10.0;  
  float voltageAtPin = averagRaw * dividerFactor;
  float solarVoltage = voltageAtPin * ((r1 + r2) / r2);

  return solarVoltage;  
}