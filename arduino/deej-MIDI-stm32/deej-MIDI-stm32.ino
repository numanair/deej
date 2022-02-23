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
float measuredInput[] = {14, 50, 165,  413, 907, 1450, 1975, 2545, 3095, 3645, 3923, 4030, 4088};

// const uint8 pot_pin = 6;
const uint8 threshold = 1;

const unsigned int midi_channel[NUM_SLIDERS] = {1, 2, 3, 4, 5};
const unsigned int cc_command[NUM_SLIDERS] = {7, 7, 7, 7, 7}; // MIDI CC number

// number of elements in the MultiMap arrays
const int arrayQty = sizeof(measuredInput) / sizeof(measuredInput[0]);
float adjustedinputval[arrayQty] = {0};

BlockNot timer(10); // milliseconds between sending serial data

// Probably no need to change these calculated values
float idealOutputValues[arrayQty] = {0, 341, 682, 1024, 1365, 1706, 2048, 2389, 2730, 3072, 3413, 3754, 4096};
// Note: 16383 = 2¹⁴ - 1 (the maximum value that can be represented by
// a 14-bit unsigned number

unsigned int old_value[NUM_SLIDERS] = {0};
unsigned int new_value[NUM_SLIDERS] = {0};
float analogSliderValues[NUM_SLIDERS];

USBMIDI midi;
USBCompositeSerial CompositeSerial;

void sendSliderValues();
void updateSliderValues();
void filteredAnalog();

void setup() {

  for (int i = 0; i < NUM_SLIDERS; i++) {
    pinMode(analogInputs[i], INPUT_ANALOG);
  }

  USBComposite.clear(); // clear any plugins previously registered
  CompositeSerial.registerComponent();
  midi.registerComponent();
  USBComposite.begin();
  USBComposite.setProductId(0x0031);
  USBComposite.setProductString("MIX5R");

  // multiplier correction
  for (size_t i = 0; i < arrayQty; i++) {
    adjustedinputval[i] = round(idealOutputValues[i] + (measuredInput[i] - idealOutputValues[i]) * correctionMultiplier);
    // theoretical ideal + (measured - theoretical) * multi
  }
  adjustedinputval[0] = measuredInput[0]; // min value
  adjustedinputval[arrayQty] = measuredInput[arrayQty]; // max value

  midi.begin();
  CompositeSerial.begin(9600);

  while(!CompositeSerial.isConnected()); // wait for usb serial
  delay(500);
}

void loop() {
  // Deej loop and MIDI values and sending
  if (timer.HAS_TRIGGERED){
    updateSliderValues();
    filteredAnalog();
    sendSliderValues();
    timer.RESET;
  }
}

void filteredAnalog() {
  int val[NUM_SLIDERS] = {};
  for (int i = 0; i < NUM_SLIDERS; i++) {
    val[i] = {analogSliderValues[i]};
    new_value[i] = val[i] / 32;
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

void updateSliderValues() {
  for (int i = 0; i < NUM_SLIDERS; i++) {
    analogSliderValues[i] = multiMap<float>(analogRead(analogInputs[i]), adjustedinputval, idealOutputValues, arrayQty);
  }
}

// Deej Serial Support
void sendSliderValues() {
  String builtString = String("");
  for (int i = 0; i < NUM_SLIDERS; i++) {
    builtString += String((int)round(analogSliderValues[i] / 4));
    if (i < NUM_SLIDERS - 1) {
      builtString += String("|");
    }
  }
  CompositeSerial.println(builtString);
}
