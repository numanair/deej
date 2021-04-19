#include <avr/power.h>
#include "lgtx8p.h"

#include <Arduino_Helpers.h>
#include <AH/Hardware/FilteredAnalog.hpp>
#include <AH/Timing/MillisMicrosTimer.hpp>

const int NUM_SLIDERS = 5;
//const int analogInputs[NUM_SLIDERS] = {A0, A1, A2, A3, A4};
const int ADC_bits = 10; // 10 or 12-bit
const int MaxADC = 1016; // 4064 for 12-bit or 1016 for 10-bit

int analogSliderValues[NUM_SLIDERS];
bool analogInputsUpdated = false;

FilteredAnalog<> analogInputs[NUM_SLIDERS] = {A0, A1, A2, A3, A4};

void setup() {
  analogReference(DEFAULT);
  analogReadResolution(ADC_bits);

  for (int i = 0; i < NUM_SLIDERS; i++) {
    //pinMode(analogInputs[i], INPUT);
    analogInputs[i].setupADC();
  }
  Serial.begin(9600);
  //while (!Serial); //wait for serial
  Serial.println("-----------");
}

void loop() {
  static Timer<millis> timer = 1; // ms - Blink w/o delay wrapper
  static Timer<millis> timer2 = 10; // ms - Blink w/o delay wrapper
  if (timer) {
    for (int i = 0; i < NUM_SLIDERS; i++) {
      if (analogInputs[i].update()) {
        analogInputsUpdated = true;
      }
    }
  }
  if (timer2) {
    updateSliderValues();
    sendSliderValues(); // Actually send data (all the time)
    analogInputsUpdated = false;
  }
  //delay(10);
}

void updateSliderValues() {
  for (int i = 0; i < NUM_SLIDERS; i++) {
    //analogSliderValues[i] = map(analogRead(analogInputs[i]), 0, MaxADC, 0, 1023);
    //analogSliderValues[i] = analogInputs[i].getValue();
    analogSliderValues[i] = map(analogInputs[i].getValue(), 0, MaxADC, 0, 1023);
  }
}

void sendSliderValues() {
  String builtString = String("");

  for (int i = 0; i < NUM_SLIDERS; i++) {
    builtString += String((int)analogSliderValues[i]);

    if (i < NUM_SLIDERS - 1) {
      builtString += String("|");
    }
  }

  Serial.println(builtString);
}

void printSliderValues() {
  for (int i = 0; i < NUM_SLIDERS; i++) {
    String printedString = String("Slider #") + String(i + 1) + String(": ") + String(analogSliderValues[i]) + String(" mV");
    Serial.write(printedString.c_str());

    if (i < NUM_SLIDERS - 1) {
      Serial.write(" | ");
    } else {
      Serial.write("\n");
    }
  }
}
