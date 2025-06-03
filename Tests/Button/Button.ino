#include <Bounce2.h>

#define LED_PIN 0
#define BOUNCE_PIN 1

Bounce bounce = Bounce();

int ledState = LOW;

void setup() {
  bounce.attach( BOUNCE_PIN ,  INPUT_PULLUP );   
  bounce.interval(25); 
  
  pinMode(LED_PIN, OUTPUT);
  digitalWrite(LED_PIN, ledState);
}

void loop() {
  bounce.update();
  
  if ( bounce.changed() ) {    
    int deboucedInput = bounce.read();

    if ( deboucedInput == LOW ) {
      ledState = !ledState; 
      digitalWrite(LED_PIN,ledState); 
    }
  }
}
