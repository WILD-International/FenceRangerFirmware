#include <Arduino.h>
#include <RadioLib.h>

// automatically detect which board is being used
#define RADIO_BOARD_AUTO

// now include RadioBoards
// this must be included AFTER RadioLib!
#include <RadioBoards.h>

// create a new RadioLib module
// "Radio" will be set to the correct module type,
// and "RadioModule" will have the correct pins
Radio radio = new RadioModule();

#define Boot_Key 0

#define Led 35
void setup() {
  // put your setup code here, to run once:
  Serial.begin(115200);
  pinMode(Boot_Key, INPUT_PULLUP);
  pinMode(Led, OUTPUT);
}

void loop() {
  if (digitalRead(Boot_Key) == 0) {
    digitalWrite(Led, HIGH);
    Serial.print("hello world !!! \n");
    delay(1000);
    digitalWrite(Led, LOW);
  }
  delay(100);
}
