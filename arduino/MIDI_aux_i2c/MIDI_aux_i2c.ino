#include <Arduino.h>
#include <EasyTransferI2C.h>
// #include <STM32ADC.h>
#include <Wire.h>
#include <neotimer.h>

#include "MultiMap.h"
#include "lgtx8p.h"

// AXILLARY (secondary) board firmware
const String firmwareVersion = "v1.1.0-i2c_a-dev";

// Number of potentiometers or faders
const uint8_t NUM_SLIDERS = 8;      // Faders connected to primary board
const uint8_t NUM_AUX_SLIDERS = 1;  //  Faders on secondary I2C board
const uint8_t NUM_TOTAL_SLIDERS =
    NUM_AUX_SLIDERS + NUM_SLIDERS;  //  Total faders count

// Analog pins assignment
const uint8_t analogInputs[NUM_AUX_SLIDERS] = {0};

const byte mainboard_address = 2;

//Neotimer mytimer = Neotimer(3);

EasyTransferI2C ET;  // EasyTransfer object
//  https://github.com/eugene-prout/Arduino-EasyTransfer/
struct SEND_DATA_STRUCTURE {
  uint16_t auxVal[NUM_AUX_SLIDERS] = {0};
};
SEND_DATA_STRUCTURE mydata;

// STM32ADC myADC(ADC1);

void updateSliderValues();

void setup() {
  for (int i = 0; i < NUM_SLIDERS; i++) {
    pinMode(analogInputs[i], INPUT);
  }
  pinMode(13, OUTPUT);

  // myADC.calibrate();

  Wire.begin();
  ET.begin(details(mydata), &Wire);

  Serial.begin(9600);

  delay(100);
}

void loop() {
//  if (mytimer.repeat()) {
    updateSliderValues();
//  }
}

// Called every 10ms
void updateSliderValues() {
  // Aux faders (local)
  for (int i = 0; i < NUM_AUX_SLIDERS; i++) {
    // Read ADC fader value
    mydata.auxVal[i] = analogRead(analogInputs[i]);
    Serial.println(mydata.auxVal[i]);
  }
  // send data
  ET.sendData(mainboard_address);
  delay(10);
}
