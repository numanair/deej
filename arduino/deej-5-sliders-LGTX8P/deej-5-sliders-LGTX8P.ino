#include <avr/power.h>
#include "lgtx8p.h"
#include "MultiMap.h"

const int NUM_SLIDERS = 5;
const int analogInputs[NUM_SLIDERS] = {A0, A1, A2, A3, A4};
const int ADC_bits = 12; // 10 or 12-bit

//float input[] =  {0,  8,  33,  99, 219, 350, 925, 994, 1016}; //10-bit ADC, not as good :)

// Measured values ever 5mm of throw
//float input[] =  {0, 32,    145,   353,    909, 1493,   2016,  2549,  3106, 3679,   3960, 4064};
//float output[] = {0, 85.25, 170.5, 255.75, 341, 426.25, 511.5, 596.75, 682, 767.25, 852.5, 1023};

// Adjusted multiMap for compromise on precision and linearity
float input[] =  {0, 32,    145,   353,    909,  4064};
float output[] = {0, 85.25, 170.5, 255.75, 341,  1023};

int analogSliderValues[NUM_SLIDERS];

void setup() { 
  analogReference(DEFAULT);
  analogReadResolution(ADC_bits);
  
  for (int i = 0; i < NUM_SLIDERS; i++) {
    pinMode(analogInputs[i], INPUT);
  }
  Serial.begin(9600);
}

void loop() {
  updateSliderValues();
  sendSliderValues(); // Actually send data (all the time)
  // printSliderValues(); // For debug
  delay(10);
}

void updateSliderValues() {
  for (int i = 0; i < NUM_SLIDERS; i++) {
      analogSliderValues[i] = multiMap<float>(analogRead(analogInputs[i]), input, output, 6);
      //analogSliderValues[i] = analogRead(analogInputs[i]); //unfiltered output
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
