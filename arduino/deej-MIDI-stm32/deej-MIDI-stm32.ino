#include <Arduino.h>
//#include <Arduino_Helpers.h>
//#include <AH/Timing/MillisMicrosTimer.hpp>
#include <USBComposite.h>
#include "MultiMap.h"
#include <BlockNot.h>   

// Number of potentiometers or faders
const int NUM_SLIDERS = 5;

// Potentiometer pins assignment
const uint8 analogInputs[NUM_SLIDERS] = {0, 1, 2, 3, 4};

// Adjusts linearity correction for my specific potentiometers.
// 1 = fully linear but jittery. 0.7 is about max for no jitter.
const float correctionMultiplier = 0.70;

// measured output every equal 5mm increment in 14-bit. Minimum and maximum values are not affected by correctionMultiplier.
float measuredInput[] = {0, 4095};

// const uint8 pot_pin = 6;
const uint8 threshold = 1;

const unsigned int midi_channel =
    1; // this might show up as channel 1 depending on start index
const unsigned int cc_command = 0; // bank select command

// number of elements in the MultiMap arrays
const int arrayQty = sizeof(measuredInput) / sizeof(measuredInput[0]);
float adjustedinputval[arrayQty] = {0};

// milliseconds between sending serial data
BlockNot timer(10);

// Probably no need to change these calculated values
float idealOutputValues[arrayQty] = {0, 4095};
// Note: 16383 = 2¹⁴ - 1 (the maximum value that can be represented by
// a 14-bit unsigned number

unsigned int old_value = 0;
unsigned int new_value = 0;
unsigned int analogSliderValues[NUM_SLIDERS];

USBMIDI midi;
USBCompositeSerial CompositeSerial;

void setup() {
  USBComposite.clear(); // clear any plugins previously registered
  CompositeSerial.registerComponent();
  midi.registerComponent();
  USBComposite.begin();
  USBComposite.setProductId(0x0031);  

  midi.begin();
  CompositeSerial.begin(38400);

  while(!CompositeSerial.isConnected()); // spin waiting for usb serial to come active
  delay(500);
}

void loop() {
  

  // Deej loop
  if (timer.HAS_TRIGGERED)
  {
    updateSliderValues();
    sendSliderValues();
  }

//   int temp = analogRead(pot_pin); // a value between 0-4095
//   new_value = temp / 32;          // convert to a value between 0-127

//   // If difference between new_value and old_value is greater than threshold
//   if ((new_value > old_value && new_value - old_value > threshold) ||
//       (new_value < old_value && old_value - new_value > threshold)) {

//     midi.sendControlChange(midi_channel, cc_command, new_value);

//     // Update old_value
//     old_value = new_value;
//   }

//   CompositeSerial.println(new_value);
  
//   // Wait 50ms before reading the pin again
//   delay(50);
}

void updateSliderValues() {
  for (int i = 0; i < NUM_SLIDERS; i++) {
    analogSliderValues[i] = round((multiMap<float>(analogRead(analogInputs[i]) * 4, adjustedinputval, idealOutputValues, arrayQty))) / 16;
    //analogSliderValues[i] = analogRead(analogInputs[i]); //unfiltered output
  }
}

// Deej Serial Support
void sendSliderValues() {
  String builtString = String("");
  for (int i = 0; i < NUM_SLIDERS; i++) {
    builtString += String((int)round(analogSliderValues[i])); // raw (Deej software does the filtering)
    // builtString += String((int)round(volumePotentiometers[i].getRawValue() / 16)); // raw (Deej software does the filtering)
    //builtString += String((int)volumePotentiometers[i].getValue()); // filtered
    if (i < NUM_SLIDERS - 1) {
      builtString += String("|");
    }
  }
  CompositeSerial.println(builtString);
}
