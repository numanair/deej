#include <Arduino.h>
#include <STM32ADC.h>  // STM32
#include <neotimer.h>

// #include "MultiMap.h"

// AXILLARY (secondary) board firmware
const String firmwareVersion = "v1.1.0-tx-dev";

// Number of potentiometers or faders
const uint8_t NUM_SLIDERS = 8;      // Faders connected to primary board
const uint8_t NUM_AUX_SLIDERS = 1;  //  Faders on secondary I2C board
const uint8_t NUM_TOTAL_SLIDERS =
    NUM_AUX_SLIDERS + NUM_SLIDERS;  //  Total faders count

// Analog pins assignment
const uint8_t analogInputs[NUM_AUX_SLIDERS] = {PA0};

Neotimer mytimer = Neotimer(50);

uint16_t auxVal[NUM_AUX_SLIDERS] = {0};

STM32ADC myADC(ADC1);  // STM32

void updateSliderValues();

void setup() {
  for (int i = 0; i < NUM_SLIDERS; i++) {
    pinMode(analogInputs[i], INPUT);
  }
  pinMode(PC13, OUTPUT);

  myADC.calibrate();

  Serial.begin(9600);   // usb serial
  Serial1.begin(9600);  // UART port 1

  delay(100);
}

void loop() {
  int adcRead[NUM_AUX_SLIDERS];
  for (int i = 0; i < NUM_AUX_SLIDERS; i++) {
    auxVal[i] = analogRead(analogInputs[i]);
    Serial.print(auxVal[i]);
    Serial.print(' ');
  }
  Serial.print('\n');
  delay(3);
  // send data
  if (mytimer.repeat()) {
    Serial.println("sending");
    Serial1.write(auxVal, NUM_AUX_SLIDERS);
    Serial.println("done");
  }
}
