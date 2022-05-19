#include <Arduino.h>
#include <EEPROM.h>
#include <USBComposite.h>
#include <neotimer.h>

#include "MultiMap.h"

// This sketch uses serial input in the following format:
// <CC,CC,CC,CC,CC:CH,CH,CH,CH,CH>
// For example: <07,07,07,07,07:01,02,03,04,05>
// This sets all faders to MIDI CC 7 (volume control) and assigns each to
// channel 1-5 Channel can be 1-16
// https://www.midi.org/specifications-old/item/table-3-control-change-messages-data-bytes-2
// https://anotherproducer.com/online-tools-for-musicians/midi-cc-list/

// Number of potentiometers or faders
const uint8 NUM_SLIDERS = 5;

// Potentiometer pins assignment
const uint8 analogInputs[NUM_SLIDERS] = {0, 1, 2, 3, 4};

uint8_t midi_channel[NUM_SLIDERS] = {1, 1, 1, 1, 1};   // 1 through 16
uint8_t cc_command[NUM_SLIDERS] = {1, 11, 7, 14, 21};  // MIDI CC number

const byte MAX_RECEIVE_LENGTH = (NUM_SLIDERS * 3 - 1) * 2 + 1 + 6;
char receivedChars[MAX_RECEIVE_LENGTH];
char tempChars[MAX_RECEIVE_LENGTH];  // temporary array for use when parsing

// variables to hold the parsed data
char messageFromPC[MAX_RECEIVE_LENGTH] = {0};
int integerFromPC = 0;
char stringCC[MAX_RECEIVE_LENGTH / 2] = {0};
char stringCHAN[MAX_RECEIVE_LENGTH / 2] = {0};

bool newData = false;

// Adjusts linearity correction for my specific potentiometers.
// 1 = fully linear but jittery. 0.7 is about max for no jitter.
const float correctionMultiplier = 0.60;
const uint8_t threshold = 40;  // 32ish

// measured output every equal 5mm increment in 12-bit. Minimum and maximum
// values are not affected by correctionMultiplier.
const float measuredInput[] = {14,   50,   165,  413,  907,  1450, 1975,
                               2545, 3095, 3645, 3923, 4030, 4088};

// Calculate number of elements in the MultiMap arrays
const int arrayQty = sizeof(measuredInput) / sizeof(measuredInput[0]);
float adjustedinputval[arrayQty] = {0};

// Probably no need to change these calculated values
float idealOutputValues[arrayQty] = {0,    341,  682,  1024, 1365, 1706, 2048,
                                     2389, 2730, 3072, 3413, 3754, 4095};
// Note: 4095 = 2^12 - 1 (the maximum value that can be represented by
// a 12-bit unsigned number

// const uint8 MAX_RECEIVE_LENGTH = NUM_SLIDERS * 3 - 1;
//  static char incoming_message[MAX_RECEIVE_LENGTH];

int old_value[NUM_SLIDERS] = {0};
int new_value[NUM_SLIDERS] = {0};
float analogSliderValues[NUM_SLIDERS];
const int MAX_MESSAGE_LENGTH = NUM_SLIDERS * 6;  // sliders * 00:00,
bool prog_end = 0;
bool deej = 1;
int eeprom_read = 0;
int data_write = 0;
int addressWriteCC = 20;
int addressWriteChan = addressWriteCC + NUM_SLIDERS;

Neotimer mytimer = Neotimer(10);     // ms
Neotimer mytimer2 = Neotimer(5000);  // ms
USBMIDI midi;
USBCompositeSerial CompositeSerial;

void sendSliderValues();
void updateSliderValues();
void filteredAnalog();
void parseData();
void recvWithStartEndMarkers();
void printMIDI_CC();
void printMIDI_CHAN();
void writeToEEPROM();
void readFromEEPROM();

void setup() {
  for (int i = 0; i < NUM_SLIDERS; i++) {
    pinMode(analogInputs[i], INPUT_ANALOG);
  }
  pinMode(PC13, OUTPUT);
  mytimer2.start();

  USBComposite.clear();  // clear any plugins previously registered
  CompositeSerial.registerComponent();
  midi.registerComponent();
  USBComposite.begin();
  USBComposite.setVendorId(0x0483);  // STMicroelectronics
  USBComposite.setProductId(0xf7cc);
  USBComposite.setManufacturerString("STMicroelectronics");
  USBComposite.setProductString("MIDI-MIX5R");

  // multiplier correction
  for (size_t i = 0; i < arrayQty; i++) {
    adjustedinputval[i] =
        round(idealOutputValues[i] +
              (measuredInput[i] - idealOutputValues[i]) * correctionMultiplier);
    // theoretical ideal + (measured - theoretical) * multi
  }
  // Excludes min and max from adjustment by correctionMultiplier
  adjustedinputval[0] = measuredInput[0];                        // min value
  adjustedinputval[arrayQty - 1] = measuredInput[arrayQty - 1];  // max value

  midi.begin();
  CompositeSerial.begin(9600);

  delay(3000);
  // EEPROM setup:
  const int addressFlag = 10;
  // add >= 127 check?
  if (EEPROM.read(addressFlag) == 200) {
    // EEPROM already set. Reading.
    CompositeSerial.println("EEPROM already set. Reading");
    readFromEEPROM(addressWriteCC, cc_command, NUM_SLIDERS);      // CC
    readFromEEPROM(addressWriteChan, midi_channel, NUM_SLIDERS);  // Channel
  } else {
    // First run, set EEPROM data to defaults
    CompositeSerial.println("First run, set EEPROM data to defaults");
    writeToEEPROM(addressWriteCC, cc_command, NUM_SLIDERS);      // CC
    writeToEEPROM(addressWriteChan, midi_channel, NUM_SLIDERS);  // Channel
    EEPROM.write(addressFlag, 200);
  }

  delay(500);
}

void loop() {
  // Deej loop and MIDI values and sending every 10ms
  if (mytimer.repeat()) {
    updateSliderValues();  // Gets new slider values
    filteredAnalog();      // MIDI
    if (deej) {
      // sendSliderValues();  // Deej Serial
    } else if (mytimer2.done()) {
      if (prog_end) {
        writeToEEPROM(addressWriteCC, cc_command, NUM_SLIDERS);
        writeToEEPROM(addressWriteChan, midi_channel, NUM_SLIDERS);
        CompositeSerial.println("PROG END");
        CompositeSerial.println("RESUMING DEEJ");
        prog_end = 0;
      }
      deej = 1;
    }
  }
  recvWithStartEndMarkers();
  if (newData == true) {
    strcpy(tempChars, receivedChars);
    // this temporary copy is necessary to protect the original data
    // because strtok() used in parseData() replaces the commas with \0
    parseData();
    deej = 0;
    CompositeSerial.println("New MIDI settings:");
    mytimer2.reset();
    mytimer2.start();
    prog_end = 1;
    printMIDI_CC();
    printMIDI_CHAN();
    newData = false;
  }
}

void writeToEEPROM(int addressRead, byte byteArray[], int arraySize) {
  for (int i = 0; i < NUM_SLIDERS; ++i) {
    // uint8_t current_val = EEPROM.read(addressRead);
    // if (current_val != byteArray[i]) {
    EEPROM.write(addressRead, byteArray[i]);
    CompositeSerial.println(byteArray[i]);
    ++addressRead;
    // }
  }
}

void readFromEEPROM(int addressRead, byte byteArray[], int arraySize) {
  CompositeSerial.print("eeprom: ");
  for (int i = 0; i < NUM_SLIDERS; ++i) {
    byteArray[i] = EEPROM.read(addressRead);
    ++addressRead;
    CompositeSerial.print(byteArray[i]);
    CompositeSerial.print(" , ");
  }
  CompositeSerial.print('\n');
}

void recvWithStartEndMarkers() {
  static bool recvInProgress = false;
  static byte ndx = 0;
  char startMarker = '<';
  char endMarker = '>';
  char rc;

  while (CompositeSerial.available() > 0 && newData == false) {
    rc = CompositeSerial.read();

    if (recvInProgress == true) {
      if (rc != endMarker) {
        receivedChars[ndx] = rc;
        ndx++;
        if (ndx >= MAX_RECEIVE_LENGTH) {
          ndx = MAX_RECEIVE_LENGTH - 1;
        }
      } else {
        receivedChars[ndx] = '\0';  // terminate the string
        recvInProgress = false;
        ndx = 0;
        newData = true;
      }
    }

    else if (rc == startMarker) {
      recvInProgress = true;
    }
  }
}

// <CC,CC,CC,CC,CC:CH,CH,CH,CH,CH>
// <07,14,14,14,14>:01,01,01,01,01

void parseData() {
  // split the data into its parts and recombine

  char *strtokIndx1;  // CC... / CH...
  char *
      strtokIndx2;  // Somehow two pointers is less flash used than a single one

  strtokIndx1 = strtok(tempChars, ":");
  strcpy(stringCC, strtokIndx1);

  strtokIndx1 = strtok(NULL, ":");
  strcpy(stringCHAN, strtokIndx1);

  // Start CC code
  for (int i = 0; i < NUM_SLIDERS; i++) {
    if (i == 0) {
      strtokIndx2 = strtok(stringCC, ",");
    } else if (strtokIndx1 != NULL) {
      strtokIndx2 = strtok(NULL, ",");
    }
    integerFromPC = atoi(strtokIndx2);  // convert this part to an integer
    cc_command[i] = integerFromPC;
  }
  // End CC code

  stringCHAN[NUM_SLIDERS * 3] = '\0';  // NULL terminate

  // Start Channel code
  for (int i = 0; i < NUM_SLIDERS; i++) {
    if (i == 0) {
      strtokIndx2 = strtok(stringCHAN, ",");
    } else if (strtokIndx2 != NULL) {
      strtokIndx2 = strtok(NULL, ",");
    }
    integerFromPC = atoi(strtokIndx2);  // convert this part to an integer
    midi_channel[i] = integerFromPC;

  }  // End Channel code
}

void printMIDI_CC() {
  CompositeSerial.println("MIDI CC:");
  for (int i = 0; i < NUM_SLIDERS; i++) {
    CompositeSerial.print(cc_command[i]);
    if (i < NUM_SLIDERS - 1) {
      CompositeSerial.print(", ");
    }
  }
  CompositeSerial.print('\n');
}

void printMIDI_CHAN() {
  CompositeSerial.println("MIDI CHANNEL:");
  for (int i = 0; i < NUM_SLIDERS; i++) {
    CompositeSerial.print(midi_channel[i]);
    if (i < NUM_SLIDERS - 1) {
      CompositeSerial.print(", ");
    }
  }
  CompositeSerial.print('\n');
}

void filteredAnalog() {
  for (int i = 0; i < NUM_SLIDERS; i++) {
    new_value[i] = analogSliderValues[i];  // 12-bit
    // If difference between new_value and old_value is greater than threshold,
    // send new values
    if ((new_value[i] > old_value[i] &&
         new_value[i] - old_value[i] > threshold) ||
        (new_value[i] < old_value[i] &&
         old_value[i] - new_value[i] > threshold)) {
      // Send MIDI
      // convert from 12-bit to 7-bit for MIDI
      // channel starts at 0, but midi_channel starts at 1
      int mappedVal = map(new_value[i], 0, 4095 - (32 - 2), 0, 127);
      if (mappedVal > 127) {
        mappedVal = 127;
      }
      //      CompositeSerial.println(mappedVal);
      midi.sendControlChange(midi_channel[i] - 1, cc_command[i], mappedVal);
      // Update old_value
      old_value[i] = new_value[i];
    }
  }
}

void updateSliderValues() {
  for (int i = 0; i < NUM_SLIDERS; i++) {
    analogSliderValues[i] =
        multiMap<float>(analogRead(analogInputs[i]), adjustedinputval,
                        idealOutputValues, arrayQty);
  }
}

// Deej Serial Support
void sendSliderValues() {
  String builtString = String("");
  for (int i = 0; i < NUM_SLIDERS; i++) {
    int limitedVal = analogSliderValues[i] / 4;
    if (limitedVal > 1023) {
      limitedVal = 1023;
    }
    builtString += String((int)limitedVal);
    if (i < NUM_SLIDERS - 1) {
      builtString += String("|");
    }
  }
  CompositeSerial.println(builtString);
}
