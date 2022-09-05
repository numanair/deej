#include <Arduino.h>
#include <EEPROM.h>
#include <STM32ADC.h>
#include <USBComposite.h>
#include <neotimer.h>

#include "MultiMap.h"

// This sketch uses serial input in the following format:
// <CC,CC,CC,CC,CC:CH,CH,CH,CH,CH>
// For example: <07,07,07,07,07:01,02,03,04,05>
// This sets all faders to MIDI CC 7 (volume control) and assigns each to
// channel 1-5. CC max is 127 and channel can be 1-16
// https://www.midi.org/specifications-old/item/table-3-control-change-messages-data-bytes-2
// https://anotherproducer.com/online-tools-for-musicians/midi-cc-list/

const String firmwareVersion = "v1.1.0";

// Number of potentiometers or faders
const uint8_t NUM_SLIDERS = 5;

// Potentiometer pins assignment
const uint8_t analogInputs[NUM_SLIDERS] = {0, 1, 2, 3, 4};

uint8_t midi_channel[NUM_SLIDERS] = {1, 1, 1, 1, 1};   // 1 through 16
uint8_t cc_command[NUM_SLIDERS] = {1, 11, 7, 14, 21};  // MIDI CC number

uint8_t cc_upper_limit[NUM_SLIDERS] = {
    127, 127, 127, 127, 127};  // optionally limit range of MIDI CC per fader

const byte MAX_RECEIVE_LENGTH = (NUM_SLIDERS * 3 - 1) * 2 + 1 + 6;
char receivedChars[MAX_RECEIVE_LENGTH];
char tempChars[MAX_RECEIVE_LENGTH];  // temporary array for use when parsing

// variables to hold the parsed data
char messageFromPC[MAX_RECEIVE_LENGTH] = {0};
int integerFromPC = 0;
char stringCC[MAX_RECEIVE_LENGTH / 2] = {0};
char stringCHAN[MAX_RECEIVE_LENGTH / 2] = {0};
char stringUpLimit[MAX_RECEIVE_LENGTH / 2] = {0};

bool newData = false;

// Adjusts linearity correction for my specific potentiometers.
// 1 = fully linear but jittery. 0.7 is about max for no jitter.
const float correctionMultiplier = 0.60;
const uint8_t threshold = 32;  // 32ish

// measured output every equal 5mm increment in 12-bit. Minimum and maximum
// values are not affected by correctionMultiplier.
const uint16_t measuredInput[] = {19,   50,   165,  413,  907,  1450, 1975,
                                  2545, 3095, 3645, 3923, 4030, 4082};

// Calculate number of elements in the MultiMap arrays
const uint8_t arrayQty = sizeof(measuredInput) / sizeof(measuredInput[0]);
uint16_t adjustedinputval[arrayQty] = {0};  // Same type as measuredInput

// Probably no need to change these calculated values
uint16_t idealOutputValues[arrayQty] = {
    0, 341, 682, 1024, 1365, 1706, 2048, 2389, 2730, 3072, 3413, 3754, 4095};
// Note: 4095 = 2^12 - 1 (the maximum value that can be represented by
// a 12-bit unsigned number

int old_value[NUM_SLIDERS] = {0};
int new_value[NUM_SLIDERS] = {0};
int analogSliderValues[NUM_SLIDERS];
const int MAX_MESSAGE_LENGTH = NUM_SLIDERS * 6;  // sliders * 00:00,
bool prog_end = 0;
bool CC_CH_mode = 1;
int deej = 1;  // 1=enabled 0=paused -1=disabled
int addressWriteCC = 20;
int addressWriteChan = addressWriteCC + NUM_SLIDERS;
int addressWriteUpperLimit = addressWriteChan + NUM_SLIDERS;

Neotimer mytimer = Neotimer(10);  // ms ADC polling interval
Neotimer mytimer2 = Neotimer(2000);
// ms delay before resuming Deej output.
// Also prevents rapid EEPROM writes.

USBMIDI midi;
USBCompositeSerial CompositeSerial;

void sendSliderValues();
void updateSliderValues();
void filteredAnalog();
void parseData();
void recvWithStartEndMarkers();
void printArray();
void printSettings();
void printLimitSettings();
void writeToEEPROM();
void readFromEEPROM();

STM32ADC myADC(ADC1);

void setup() {
  for (int i = 0; i < NUM_SLIDERS; i++) {
    pinMode(analogInputs[i], INPUT_ANALOG);
  }
  pinMode(PC13, OUTPUT);
  mytimer2.start();

  myADC.calibrate();

  USBComposite.clear();  // clear any plugins previously registered
  CompositeSerial.registerComponent();
  midi.registerComponent();
  USBComposite.setVendorId(0xFEED);  // STMicroelectronics
  USBComposite.setProductId(0xF1CC);
  USBComposite.setManufacturerString("Return to Paradise");
  USBComposite.setProductString("MIX5R Pro");
  USBComposite.begin();

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
  if (EEPROM.read(addressFlag) == 200) {
    // EEPROM already set. Reading.
    CompositeSerial.println("EEPROM already set. Reading");
    readFromEEPROM(addressWriteCC, cc_command, NUM_SLIDERS, 127);     // CC
    readFromEEPROM(addressWriteChan, midi_channel, NUM_SLIDERS, 16);  // Channel
    readFromEEPROM(addressWriteUpperLimit, cc_upper_limit, NUM_SLIDERS,
                   127);   // Channel
    printSettings();       // print settings to serial
    printLimitSettings();  // print settings to serial
  } else {
    // First run, set EEPROM data to defaults
    CompositeSerial.println("First run, set EEPROM data to defaults");
    writeToEEPROM(addressWriteCC, cc_command, NUM_SLIDERS, 127);     // CC
    writeToEEPROM(addressWriteChan, midi_channel, NUM_SLIDERS, 16);  // Channel
    writeToEEPROM(addressWriteUpperLimit, cc_upper_limit, NUM_SLIDERS,
                  127);              // Channel
    EEPROM.write(addressFlag, 200);  // mark EEPROM as set
  }

  delay(500);
}

void loop() {
  // Deej loop and MIDI values and sending every 10ms
  if (mytimer.repeat()) {
    updateSliderValues();  // Gets new slider values
    filteredAnalog();      // MIDI
    if (deej > 0) {
      sendSliderValues();  // Deej Serial
    } else if (mytimer2.done()) {
      if (prog_end) {
        writeToEEPROM(addressWriteCC, cc_command, NUM_SLIDERS, 127);
        writeToEEPROM(addressWriteChan, midi_channel, NUM_SLIDERS, 16);
        CompositeSerial.println("MIDI settings saved");
        prog_end = 0;
        if (deej > 0) {
          CompositeSerial.println("Resuming Deej");
        }
      }
      if (deej >= 0) {
        deej = 1;  // if deej paused then resume
      }
    }
  }
  recvWithStartEndMarkers();
  if (newData == true) {
    strcpy(tempChars, receivedChars);
    // this temporary copy is necessary to protect the original data
    // because strtok() used in parseData() replaces the commas with \0
    if (CC_CH_mode) {
      parseData();
      // Output MIDI settings to serial in the input format
      CompositeSerial.println("New MIDI settings:");
      printSettings();
    } else {
      parseUpperLimit();
      // Output MIDI limuts to serial in the input format
      CompositeSerial.println("New MIDI Limits:");
      printLimitSettings();
    }

    if (deej >= 0) {
      deej = 0;
    }
    mytimer2.reset();
    mytimer2.start();

    prog_end = 1;
    newData = false;
  }
}

void writeToEEPROM(int addressRead, byte byteArray[], int arraySize, int max) {
  for (int i = 0; i < NUM_SLIDERS; ++i) {
    if (byteArray[i] > max) {
      // Keeps CC/channel within limit
      byteArray[i] = max;
    }
    EEPROM.write(addressRead, byteArray[i]);
    ++addressRead;
  }
}

void readFromEEPROM(int addressRead, byte byteArray[], int arraySize, int max) {
  for (int i = 0; i < NUM_SLIDERS; ++i) {
    byteArray[i] = EEPROM.read(addressRead);
    if (byteArray[i] > max) {
      // Keeps CC/channel within limit
      byteArray[i] = max;
    }
    ++addressRead;
  }
}

void recvWithStartEndMarkers() {
  static bool recvInProgress = false;
  static byte ndx = 0;
  char startMarker = '<';
  char endMarker = '>';
  char retSettings = 'c';    // print settings command
  char retVersion = 'v';     // print firmware version
  char togDeej = 'd';        // toggle Deej
  char setUpperLimit = 'u';  // toggle Deej
  char rc;

  while (CompositeSerial.available() > 0 && newData == false) {
    rc = CompositeSerial.read();

    if (recvInProgress == true) {
      if (rc != endMarker && rc != retSettings) {
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

    else if (rc == retSettings) {
      if (CC_CH_mode) {
        // Currently in CC and Channel assignment mode. Print these settings.
        printSettings();
      } else {
        // Currently in limit setting mode. Print these settings.
        printLimitSettings();
      }

    }

    else if (rc == retVersion) {
      CompositeSerial.println(firmwareVersion);
    }

    else if (rc == setUpperLimit) {
      // toggles saving CC/CH or setting the limits for max fader output
      CC_CH_mode = !CC_CH_mode;
      if (CC_CH_mode) {
        CompositeSerial.println("CC and Channel assignment mode");
      } else {
        CompositeSerial.println("Upper Limits Mode");
      }
    }

    else if (rc == togDeej) {
      if (deej > 0) {
        deej = -1;  // disable deej serial output
        CompositeSerial.println("Deej disabled.");
      } else if (deej < 0) {
        deej = 1;  // re-enable deej serial output
        CompositeSerial.println("Deej enabled.");
      }
    }
  }
}

// <CC,CC,CC,CC,CC:CH,CH,CH,CH,CH>
// <07,14,14,14,14>:01,01,01,01,01>

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

void parseUpperLimit() {
  // split the data into its parts and recombine

  char *strtokIndx1;  // CC... / CH...
  // char *
  //     strtokIndx2;  // Somehow two pointers is less flash used than a single
  //     one

  strtokIndx1 = strtok(tempChars, ":");
  // strcpy(stringUpLimit, strtokIndx1);
  // strcpy(stringUpLimit, cc_upper_limit);

  // strtokIndx1 = strtok(NULL, ":");
  // strcpy(stringCHAN, strtokIndx1);

  // Start upper limit saving
  for (int i = 0; i < NUM_SLIDERS; i++) {
    if (i == 0) {
      strtokIndx1 = strtok(tempChars, ",");
    } else if (strtokIndx1 != NULL) {
      strtokIndx1 = strtok(NULL, ",");
    }
    integerFromPC = atoi(strtokIndx1);  // convert this part to an integer
    cc_upper_limit[i] = integerFromPC;
    if (cc_upper_limit[i] > 127) {
      cc_upper_limit[i] = 127;
    }
  }
  // End upper limit code

  // stringCHAN[NUM_SLIDERS * 3] = '\0';  // NULL terminate

  //   // Start Channel code
  //   for (int i = 0; i < NUM_SLIDERS; i++) {
  //     if (i == 0) {
  //       strtokIndx2 = strtok(stringCHAN, ",");
  //     } else if (strtokIndx2 != NULL) {
  //       strtokIndx2 = strtok(NULL, ",");
  //     }
  //     integerFromPC = atoi(strtokIndx2);  // convert this part to an integer
  //     midi_channel[i] = integerFromPC;
  //   }  // End Channel code
}

void printArray(byte inputArray[], int arraySize) {
  for (int i = 0; i < arraySize; i++) {
    CompositeSerial.print(inputArray[i]);
    if (i < arraySize - 1) {
      CompositeSerial.print(",");
      // #,#,#,#...
    }
  }
}

void printSettings() {
  CompositeSerial.println("MIDI assignment");
  CompositeSerial.print("<");
  printArray(cc_command, NUM_SLIDERS);
  CompositeSerial.print(":");
  printArray(midi_channel, NUM_SLIDERS);
  CompositeSerial.print(">");
  CompositeSerial.print('\n');  // newline
}

void printLimitSettings() {
  CompositeSerial.println("MIDI limits");
  CompositeSerial.print("<");
  printArray(cc_upper_limit, NUM_SLIDERS);
  CompositeSerial.print(">");
  CompositeSerial.print('\n');  // newline
}

void filteredAnalog() {
  for (int i = 0; i < NUM_SLIDERS; i++) {
    new_value[i] = analogSliderValues[i];  // 12-bit
    // If difference between new_value and old_value is greater than threshold,
    // then send new values
    if ((new_value[i] > old_value[i] &&
         new_value[i] - old_value[i] > threshold) ||
        (new_value[i] < old_value[i] &&
         old_value[i] - new_value[i] > threshold)) {
      // Update old_value
      old_value[i] = new_value[i];
      // convert from 12-bit to 7-bit for MIDI
      new_value[i] = floor(new_value[i] / 32);
      if (new_value[i] > 127) {
        new_value[i] = 127;
      }
      // Send MIDI
      // channel starts at 0, but midi_channel starts at 1
      midi.sendControlChange(midi_channel[i] - 1, cc_command[i],
                             map(new_value[i], 0, 127, 0, cc_upper_limit[i]));
    }
  }
}

void updateSliderValues() {
  for (int i = 0; i < NUM_SLIDERS; i++) {
    // analogSliderValues[i] = analogRead(analogInputs[i]);
    analogSliderValues[i] =
        multiMap<uint16>(analogRead(analogInputs[i]), adjustedinputval,
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
