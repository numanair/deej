#include <Arduino.h>
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

const unsigned int midi_channel[NUM_SLIDERS] = {1, 2, 3, 4, 5};
const unsigned int cc_command[NUM_SLIDERS] = {7, 7, 7, 7, 7}; // MIDI CC number

// number of elements in the MultiMap arrays
const int arrayQty = sizeof(measuredInput) / sizeof(measuredInput[0]);
float adjustedinputval[arrayQty] = {0};

BlockNot timer(10); // milliseconds between sending serial data

// Probably no need to change these calculated values
float idealOutputValues[arrayQty] = {0, 4095};
// Note: 16383 = 2¹⁴ - 1 (the maximum value that can be represented by
// a 14-bit unsigned number

unsigned int old_value[NUM_SLIDERS] = {0};
unsigned int new_value[NUM_SLIDERS] = {0};
unsigned int analogSliderValues[NUM_SLIDERS];

USBMIDI midi;
USBCompositeSerial CompositeSerial;

void sendSliderValues();
void updateSliderValues();
void filteredAnalog();

void setup() {
  USBComposite.clear(); // clear any plugins previously registered
  CompositeSerial.registerComponent();
  midi.registerComponent();
  USBComposite.begin();
  USBComposite.setProductId(0x0031);  

  midi.begin();
  CompositeSerial.begin(38400);

  while(!CompositeSerial.isConnected()); // wait for usb serial
  delay(500);
}

void loop() {
  // Deej loop and MIDI values and sending
  if (timer.HAS_TRIGGERED){
    updateSliderValues();
    sendSliderValues();
    filteredAnalog();
    timer.RESET;
  }
}

void filteredAnalog() {
  unsigned int val[NUM_SLIDERS] = {};
  for (int i = 0; i < NUM_SLIDERS; i++) {
    val[i] = {analogRead(analogInputs[i])};
    // If difference between new_value and old_value is greater than threshold, send new values
    if ((new_value[i] > old_value[i] && new_value[i] - old_value[i] > threshold) ||
    (new_value[i] < old_value[i] && old_value[i] - new_value[i] > threshold)) {
      // Send MIDI
      midi.sendControlChange(midi_channel[i], cc_command[i], new_value[i]);
      // Update old_value
      old_value[i] = new_value[i];
    }
  }
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


void updateSliderValues() {
  for (int i = 0; i < NUM_SLIDERS; i++) {
    analogSliderValues[i] = multiMap<float>(analogRead(analogInputs[i]), adjustedinputval, idealOutputValues, arrayQty) / 4;
  }
}

// Deej Serial Support
void sendSliderValues() {
  String builtString = String("");
  for (int i = 0; i < NUM_SLIDERS; i++) {
    builtString += String((int)round(analogSliderValues[i]));
    if (i < NUM_SLIDERS - 1) {
      builtString += String("|");
    }
  }
  CompositeSerial.println(builtString);
}
