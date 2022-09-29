#include <Arduino.h>
#include <Wire.h>
#include <EasyTransferI2C.h>
#include <STM32ADC.h>  // STM32
#include <neotimer.h>

#include "MultiMap.h"
// #include "lgtx8p.h" // LGT8F328P

// AXILLARY (secondary) board firmware
const String firmwareVersion = "v1.1.0-i2c_a-dev";

// Number of potentiometers or faders
const uint8_t NUM_SLIDERS = 8;      // Faders connected to primary board
const uint8_t NUM_AUX_SLIDERS = 1;  //  Faders on secondary I2C board
const uint8_t NUM_TOTAL_SLIDERS =
    NUM_AUX_SLIDERS + NUM_SLIDERS;  //  Total faders count

// Analog pins assignment
const uint8_t analogInputs[NUM_AUX_SLIDERS] = {PA0};

const byte mainboard_address = 2;

Neotimer mytimer = Neotimer(5);

EasyTransferI2C ET;  // EasyTransfer object
//  https://github.com/eugene-prout/Arduino-EasyTransfer/
struct SEND_DATA_STRUCTURE {
  uint16_t auxVal[NUM_AUX_SLIDERS] = {0};
};
SEND_DATA_STRUCTURE mydata;

STM32ADC myADC(ADC1);  // STM32

void updateSliderValues();

void setup() {
  for (int i = 0; i < NUM_SLIDERS; i++) {
    pinMode(analogInputs[i], INPUT);
  }
  pinMode(PC13, OUTPUT);

  myADC.calibrate();  // STM32

  Wire.begin();
  ET.begin(details(mydata), &Wire);

  Serial.begin(9600);

  delay(100);
}

void loop() {
  int adcRead[NUM_AUX_SLIDERS];
  for (int i = 0; i < NUM_AUX_SLIDERS; i++) {
    // adcRead[i] = 500;
    adcRead[i] = analogRead(analogInputs[i]);
  }
  // updateSliderValues();
  for (int i = 0; i < NUM_AUX_SLIDERS; i++) {
    // Read ADC fader value
    mydata.auxVal[i] = adcRead[i];
    Serial.print(mydata.auxVal[i]);
    Serial.print(' ');
  }
  Serial.print('\n');
  // send data
  if (mytimer.repeat()) {
    Serial.println("sending");
    ET.sendData(mainboard_address);
    Serial.println("done");
  }
}
