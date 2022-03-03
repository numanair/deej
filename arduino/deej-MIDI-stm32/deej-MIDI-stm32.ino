#include <Arduino.h>
#include <USBComposite.h>
#include "MultiMap.h"
#include <BlockNot.h>

// Number of potentiometers or faders
const uint8 NUM_SLIDERS = 5;

const uint8 midi_channel[NUM_SLIDERS] = {0, 1, 2, 3, 4}; // Starts at 0?
const uint8 cc_command[NUM_SLIDERS] = {7, 7, 7, 7, 7}; // MIDI CC number

// Potentiometer pins assignment
const uint8 analogInputs[NUM_SLIDERS] = {0, 1, 2, 3, 4};

// Adjusts linearity correction for my specific potentiometers.
// 1 = fully linear but jittery. 0.7 is about max for no jitter.
const float correctionMultiplier = 0.70;
const uint8 threshold = 1;

// measured output every equal 5mm increment in 12-bit. Minimum and maximum values are not affected by correctionMultiplier.
const float measuredInput[] = {14, 50, 165,  413, 907, 1450, 1975, 2545, 3095, 3645, 3923, 4030, 4088};

// Calculate number of elements in the MultiMap arrays
const int arrayQty = sizeof(measuredInput) / sizeof(measuredInput[0]);
float adjustedinputval[arrayQty] = {0};

BlockNot timer(10); // milliseconds between sending serial data

// Probably no need to change these calculated values
float idealOutputValues[arrayQty] = {0, 341, 682, 1024, 1365, 1706, 2048, 2389, 2730, 3072, 3413, 3754, 4095};
// Note: 4095 = 2^12 - 1 (the maximum value that can be represented by
// a 12-bit unsigned number

uint8 old_value[NUM_SLIDERS] = {0};
uint8 new_value[NUM_SLIDERS] = {0};
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
//  USBComposite.setProductId(0x0031);
//  USBComposite.setProductId(0x0483);
//  USBComposite.setProductString("MIX5R");

  USBComposite.setVendorId(0x0483); // STMicroelectronics
  USBComposite.setProductId(0xf7cc);
  USBComposite.setManufacturerString("STMicroelectronics");
  USBComposite.setProductString("MIDI-MIX5R");

//  USBComposite.setVendorId(uint16 vendor);
//  USBComposite.setProductId(uint16 product);
//  USBComposite.setManufacturerString(const char* manufacturer);
//  USBComposite.setProductString(const char* product);
//  USBComposite.setSerialString(const char* serialNumber);

  // multiplier correction
  for (size_t i = 0; i < arrayQty; i++) {
    adjustedinputval[i] = round(idealOutputValues[i] + (measuredInput[i] - idealOutputValues[i]) * correctionMultiplier);
    // theoretical ideal + (measured - theoretical) * multi
  }
  // Excludes min and max from adjustment by correctionMultiplier
  adjustedinputval[0] = measuredInput[0]; // min value
  adjustedinputval[arrayQty] = measuredInput[arrayQty]; // max value

  midi.begin();
  CompositeSerial.begin(9600);

  delay(1000);
//  while(!CompositeSerial.isConnected()); // wait for usb serial
}

void loop() {
  // Deej loop and MIDI values and sending
  if (timer.HAS_TRIGGERED){
    timer.RESET; // BlockNot non-blocking timer
    updateSliderValues(); // Gets new slider values
    filteredAnalog(); // MIDI
    sendSliderValues(); // Deej Serial
  }
}

void filteredAnalog() {
  for (int i = 0; i < NUM_SLIDERS; i++) {
    new_value[i] = analogSliderValues[i] / 32; // convert from 12-bit to 7-bit for MIDI
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
