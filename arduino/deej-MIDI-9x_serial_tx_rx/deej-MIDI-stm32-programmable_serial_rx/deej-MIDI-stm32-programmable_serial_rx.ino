#include <Arduino.h>
#include <EEPROM.h>
#include <STM32ADC.h>
#include <SerialTransfer.h>  // for SerialTransfer
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

// Libraries:
// https://github.com/PowerBroker2/SerialTransfer/
// https://github.com/jrullan/neotimer
// https://github.com/RobTillaart/MultiMap

const String firmwareVersion = "v1.1.0-rx-dev";

// Number of potentiometers or faders
const uint8_t NUM_SLIDERS = 5;      // Faders connected to primary board
const uint8_t NUM_AUX_SLIDERS = 4;  //  Faders on secondary I2C board
const uint8_t NUM_TOTAL_SLIDERS =
    NUM_AUX_SLIDERS + NUM_SLIDERS;  //  Total faders count

// Potentiometer pins assignment
const uint8_t analogInputs[NUM_SLIDERS] = {PA0, PA1, PA2, PA3, PA4};

uint8_t midi_channel[NUM_TOTAL_SLIDERS] = {1, 1, 1, 1, 1,
                                           1, 1, 1, 1};  // 1 through 16
uint8_t cc_command[NUM_TOTAL_SLIDERS] = {1, 11, 7, 14, 21,
                                         2, 3,  4, 5};  // MIDI CC number

uint8_t cc_lower_limit[NUM_TOTAL_SLIDERS] = {
    0, 0, 0, 0, 0, 0, 0, 0};  // optionally limit range of MIDI CC per fader
uint8_t cc_upper_limit[NUM_TOTAL_SLIDERS] = {
    127, 127, 127, 127, 127,
    127, 127, 127, 127};  // optionally limit range of MIDI CC per fader

const byte MAX_RECEIVE_LENGTH = (NUM_TOTAL_SLIDERS * 4 - 1) * 2 + 1 + 6;
char receivedChars[MAX_RECEIVE_LENGTH];
char tempChars[MAX_RECEIVE_LENGTH];  // temporary array for use when parsing

const byte MAX_AUX_LENGTH = NUM_AUX_SLIDERS * 4;
char receivedCharsAux[MAX_AUX_LENGTH];
char tempCharsAux[MAX_AUX_LENGTH];  // temporary array for use when parsing

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

int deej = 1;  // 1=enabled 0=paused -1=disabled
int old_value[NUM_TOTAL_SLIDERS] = {0};
int new_value[NUM_TOTAL_SLIDERS] = {0};
int old_midi_value[NUM_TOTAL_SLIDERS] = {0};
int new_midi_value[NUM_TOTAL_SLIDERS] = {0};
int analogSliderValues[NUM_TOTAL_SLIDERS];
bool prog_end = 0;
bool CC_CH_mode = 1;
// const int MAX_MESSAGE_LENGTH = NUM_TOTAL_SLIDERS * 6;  // sliders * 00:00,
int addressWriteCC = 2;
int addressWriteChan = addressWriteCC + NUM_TOTAL_SLIDERS;
int addressWriteUpperLimit = addressWriteChan + NUM_TOTAL_SLIDERS;
int addressWriteLowerLimit = addressWriteUpperLimit + NUM_TOTAL_SLIDERS;

// variables to hold the parsed data
char messageFromPC[MAX_RECEIVE_LENGTH] = {0};
int integerFromPC = 0;
uint16_t intAux = 0;
bool newData = false;
bool newAux = false;

uint16_t auxVal[NUM_AUX_SLIDERS] = {0};

Neotimer mytimer = Neotimer(10);  // ms ADC polling interval
Neotimer mytimer2 = Neotimer(2000);
// ms delay before saving settings/resuming Deej output.
// Also prevents rapid EEPROM writes.

SerialTransfer myTransfer;

USBMIDI midi;
USBCompositeSerial CompositeSerial;

void sendSliderValues();
void updateSliderValues();
void filteredAnalog();
void parseData();
void recvWithStartEndMarkers();
void receiveAuxData();
void printArray();
void printSettings();
void printLimitSettings();
void writeToEEPROM();
void readFromEEPROM();
// void parseAux();

STM32ADC myADC(ADC1);

void setup() {
  // set pinMode only for local faders
  for (int i = 0; i < NUM_SLIDERS; i++) {
    pinMode(analogInputs[i], INPUT_ANALOG);
  }
  // LED is inverted on these boards
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
  adjustedinputval[0] = idealOutputValues[0];  // min value
  adjustedinputval[arrayQty - 1] = idealOutputValues[arrayQty - 1];

  midi.begin();
  CompositeSerial.begin(9600);  // USB
  Serial1.begin(115200);          // Aux
  myTransfer.begin(Serial1);

  delay(2000);
  // EEPROM setup:
  const int addressFlag = 10;
  if (EEPROM.read(addressFlag) == 200) {
    // EEPROM already set. Reading.
    CompositeSerial.println("EEPROM already set. Reading");
    readFromEEPROM(addressWriteCC, cc_command, NUM_TOTAL_SLIDERS, 127);  // CC
    readFromEEPROM(addressWriteChan, midi_channel, NUM_TOTAL_SLIDERS,
                   16);  // Channel
    readFromEEPROM(addressWriteLowerLimit, cc_lower_limit, NUM_TOTAL_SLIDERS,
                   127);  // Lower bound of each fader output
    readFromEEPROM(addressWriteUpperLimit, cc_upper_limit, NUM_TOTAL_SLIDERS,
                   127);   // Upper bound of each fader output
    printSettings();       // print settings to serial
    printLimitSettings();  // print settings to serial
  } else {
    // First run, set EEPROM data to defaults
    CompositeSerial.println("First run, set EEPROM data to defaults");
    writeToEEPROM(addressWriteCC, cc_command, NUM_TOTAL_SLIDERS, 127);  // CC
    writeToEEPROM(addressWriteChan, midi_channel, NUM_TOTAL_SLIDERS,
                  16);  // Channel
    writeToEEPROM(addressWriteLowerLimit, cc_lower_limit, NUM_TOTAL_SLIDERS,
                  127);  // Lower bound of each fader output
    writeToEEPROM(addressWriteUpperLimit, cc_upper_limit, NUM_TOTAL_SLIDERS,
                  127);              // Upper bound of each fader output
    EEPROM.write(addressFlag, 200);  // mark EEPROM as set
  }

  delay(800);
}

void loop() {
  // receiveAuxData();
  if (myTransfer.available()) {
    myTransfer.rxObj(auxVal);  // serialTransfer datum (single obj)
    for (int i = 0; i < NUM_AUX_SLIDERS; i++) {
      // CompositeSerial.print(auxVal[i]);
      // CompositeSerial.print(",");
    }
    // CompositeSerial.print('\n');
  }

  // Deej loop and MIDI values and sending every 10ms
  if (mytimer.repeat()) {
    // parseAux();            // Puts Aux fader values into an array (auxVal)
    updateSliderValues();  // Reads fader analog values. Also maps all faders.
    filteredAnalog();      // MIDI. Checks for changed value before sending.
    if (deej > 0) {
      sendSliderValues();  // Deej Serial
    } else if (mytimer2.done()) {
      if (prog_end) {
        writeToEEPROM(addressWriteCC, cc_command, NUM_TOTAL_SLIDERS, 127);
        writeToEEPROM(addressWriteChan, midi_channel, NUM_TOTAL_SLIDERS, 16);
        writeToEEPROM(addressWriteLowerLimit, cc_lower_limit, NUM_TOTAL_SLIDERS,
                      127);
        writeToEEPROM(addressWriteUpperLimit, cc_upper_limit, NUM_TOTAL_SLIDERS,
                      127);
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
      parseFaderLimits();
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
  for (int i = 0; i < NUM_TOTAL_SLIDERS; ++i) {
    if (byteArray[i] > max) {
      // Keeps CC/channel within limit
      byteArray[i] = max;
    }
    EEPROM.write(addressRead, byteArray[i]);
    ++addressRead;
  }
}

void readFromEEPROM(int addressRead, byte byteArray[], int arraySize, int max) {
  for (int i = 0; i < NUM_TOTAL_SLIDERS; ++i) {
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
  char togLimitsEdit = 'm';  // toggle adjusting output limits min/max
  char helpMode = 'h';       // help
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

    else if (rc == togLimitsEdit) {
      // toggles saving CC/CH or setting the limits for max fader output
      CC_CH_mode = !CC_CH_mode;  // toggle
      if (CC_CH_mode) {
        CompositeSerial.println("CC & Channel Assignment Mode");
      } else {
        CompositeSerial.println("Limits Min/Max Assignment Mode");
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

    else if (rc == helpMode) {
      deej = -1;                    // disable deej
      CompositeSerial.print('\n');  // newline
      CompositeSerial.println("MIX5R Pro Help:");
      CompositeSerial.println("h - This help menu");
      CompositeSerial.println("v - Print current firmware version");
      CompositeSerial.println(
          "m - toggle assigning MIDI CC/Channel or setting output min/max");
      CompositeSerial.println("c - Print current settings");
      CompositeSerial.println("d - Toggle Deej serial output");
      CompositeSerial.print('\n');  // newline
      CompositeSerial.println("Settings are assigned in this format:");
      CompositeSerial.println("<1,11,7,14,21,2,3,4,5:1,1,1,1,1,1,1,1,1>");
      CompositeSerial.println(
          "and correspond to <CC:Channel> or <lower_limit:upper_limit> ");
      CompositeSerial.println("depending on the mode.");
      CompositeSerial.println("The default limits are 0-127 and can ");
      CompositeSerial.println("be swapped to reverse the output.");
    }
  }
}

// <CC,CC,CC,CC,CC,CC,CC,CC,CC:CH,CH,CH,CH,CH,CH,CH,CH,CH>
// <07,14,14,14,14>:01,01,01,01,01>

void parseData() {
  // split the data into its parts and recombine

  char stringCC[MAX_RECEIVE_LENGTH / 2] = {0};
  char stringCHAN[MAX_RECEIVE_LENGTH / 2] = {0};

  char *strtokIndx1;  // CC... / CH...
  char *
      strtokIndx2;  // Somehow two pointers is less flash used than a single one

  strtokIndx1 = strtok(tempChars, ":");
  strcpy(stringCC, strtokIndx1);

  // rerun strtok() using the same tempChars to find the next token
  strtokIndx1 = strtok(NULL, ":");
  strcpy(stringCHAN, strtokIndx1);

  // Start CC code
  for (int i = 0; i < NUM_TOTAL_SLIDERS; i++) {
    if (i == 0) {
      strtokIndx2 = strtok(stringCC, ",");
    } else if (strtokIndx1 != NULL) {
      strtokIndx2 = strtok(NULL, ",");  // next token
    }
    integerFromPC = atoi(strtokIndx2);  // convert this part to an integer
    cc_command[i] = integerFromPC;
  }
  // End CC code

  stringCHAN[NUM_TOTAL_SLIDERS * 3] = '\0';  // NULL terminate

  // Start Channel code
  for (int i = 0; i < NUM_TOTAL_SLIDERS; i++) {
    if (i == 0) {
      strtokIndx2 = strtok(stringCHAN, ",");
    } else if (strtokIndx2 != NULL) {
      strtokIndx2 = strtok(NULL, ",");  // next token
    }
    integerFromPC = atoi(strtokIndx2);  // convert this part to an integer
    midi_channel[i] = integerFromPC;
  }  // End Channel code
}

void parseFaderLimits() {
  // split the data into its parts and recombine

  char stringLowerLim[MAX_RECEIVE_LENGTH / 2] = {0};
  char stringUpperLim[MAX_RECEIVE_LENGTH / 2] = {0};

  char *strtokIndx1;  // CC... / CH...
  char *
      strtokIndx2;  // Somehow two pointers is less flash used than a single one

  strtokIndx1 = strtok(tempChars, ":");
  strcpy(stringLowerLim, strtokIndx1);

  // rerun strtok() using the same tempChars to find the next token
  strtokIndx1 = strtok(NULL, ":");
  strcpy(stringUpperLim, strtokIndx1);

  // Start new lower limit code
  for (int i = 0; i < NUM_TOTAL_SLIDERS; i++) {
    if (i == 0) {
      strtokIndx2 = strtok(stringLowerLim, ",");
    } else if (strtokIndx1 != NULL) {
      strtokIndx2 = strtok(NULL, ",");  // next token
    }
    integerFromPC = atoi(strtokIndx2);  // convert this part to an integer
    cc_lower_limit[i] = integerFromPC;
  }
  // End new lower limit code

  // Start new upper limit code
  for (int i = 0; i < NUM_TOTAL_SLIDERS; i++) {
    if (i == 0) {
      strtokIndx2 = strtok(stringUpperLim, ",");
    } else if (strtokIndx2 != NULL) {
      strtokIndx2 = strtok(NULL, ",");  // next token
    }
    integerFromPC = atoi(strtokIndx2);  // convert this part to an integer
    cc_upper_limit[i] = integerFromPC;  //
  }
  // End new upper limit code
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
  printArray(cc_command, NUM_TOTAL_SLIDERS);
  CompositeSerial.print(":");
  printArray(midi_channel, NUM_TOTAL_SLIDERS);
  CompositeSerial.print(">");
  CompositeSerial.print('\n');  // newline
}

void printLimitSettings() {
  CompositeSerial.println("MIDI limits");
  CompositeSerial.print("<");
  printArray(cc_lower_limit, NUM_TOTAL_SLIDERS);
  CompositeSerial.print(":");
  printArray(cc_upper_limit, NUM_TOTAL_SLIDERS);
  CompositeSerial.print(">");
  CompositeSerial.print('\n');  // newline
}

void filteredAnalog() {
  for (int i = 0; i < NUM_TOTAL_SLIDERS; i++) {
    new_value[i] = analogSliderValues[i];  // 12-bit
    // If difference between new_value and old_value is greater than
    // threshold, send new values
    if ((new_value[i] != old_value[i] &&
         abs(new_value[i] - old_value[i]) > threshold)) {
      // Update old_value
      old_value[i] = new_value[i];
      // convert from 12-bit to 7-bit for MIDI
      new_value[i] = floor(new_value[i] / 32);
      // if (new_value[i] > 127) {
      //   new_value[i] = 127;  // cap output to MIDI range
      // }

      new_midi_value[i] =
          map(new_value[i], 0, 127, cc_lower_limit[i], cc_upper_limit[i]);
      if (new_midi_value[i] != old_midi_value[i]) {
        // Update old_midi_value
        old_midi_value[i] = new_midi_value[i];
        if (new_midi_value[i] > 127) {
          new_midi_value[i] = 127;  // cap output to MIDI range
        }
        // Send MIDI
        // channel starts at 0, but midi_channel starts at 1
        midi.sendControlChange(midi_channel[i] - 1, cc_command[i],
                               new_midi_value[i]);
      }
    }
  }
}

// Reads Serial1 input for Aux fader values
void receiveAuxData() {
  static bool recvAuxInProgress = false;
  char startMarker = '<';
  char endMarker = '>';
  static byte ndx = 0;
  char rc;

  while (Serial1.available() > 0 && newAux == false) {
    rc = Serial1.read();
    // CompositeSerial.print("rc:");  // print rc
    // CompositeSerial.print(rc);     // print rc
    // CompositeSerial.print('\n');   // print rc
    if (recvAuxInProgress == true) {
      if (rc != endMarker) {
        // CompositeSerial.println("debugging");  // debugging
        receivedCharsAux[ndx] = rc;
        ndx++;
        if (ndx >= MAX_RECEIVE_LENGTH) {
          ndx = MAX_RECEIVE_LENGTH - 1;
        }
      } else {                         // end marker
        receivedCharsAux[ndx] = '\0';  // terminate the string
        recvAuxInProgress = false;
        ndx = 0;
        newAux = true;
      }
    }

    else if (rc == startMarker) {
      recvAuxInProgress = true;
    }
  }
}

// Gets Aux fader values and stores them in auxVal array.
/*/
void parseAux() {
  char stringAux[MAX_AUX_LENGTH];
  char *strtokIndx1;

  // new parseData for aux:
  if (newAux == true) {
    strcpy(stringAux, receivedCharsAux);
    // this temporary copy is necessary to protect the original data
    // because strtok() used below replaces the commas with \0

    for (int i = 0; i < NUM_AUX_SLIDERS; i++) {
      if (i == 0) {
        strtokIndx1 = strtok(stringAux, ",");
      } else if (strtokIndx1 != NULL) {
        strtokIndx1 = strtok(NULL, ",");  // next token
      }
      intAux = atoi(strtokIndx1);  // convert this part to an integer
      auxVal[i] = intAux;
    }

    newAux = false;
  }
}
/*/

// Called every 10ms
void updateSliderValues() {
  // if (myTransfer.status = 0) {
  // no serial error
  // Apply mapping to Aux faders (external)
  for (int i = 0; i < NUM_AUX_SLIDERS; i++) {
    analogSliderValues[i] =
        multiMap<uint16>(auxVal[i], adjustedinputval, idealOutputValues,
                         arrayQty);  //  Aux fader data from I2C
  }
  // }
  // Apply mapping to mainboard faders (local)
  for (int i = NUM_AUX_SLIDERS; i < NUM_TOTAL_SLIDERS; i++) {
    analogSliderValues[i] =
        multiMap<uint16>(analogRead(analogInputs[i - NUM_AUX_SLIDERS]),
                         adjustedinputval, idealOutputValues, arrayQty);
  }
}

// Deej Serial Support
void sendSliderValues() {
  String builtString = String("");
  for (int i = 0; i < NUM_TOTAL_SLIDERS; i++) {
    // User set limits = 0-127.
    int minVal10bit =
        cc_lower_limit[i] * (1023.0 / 127.0);  // decimals for float math
    int maxVal10bit =
        cc_upper_limit[i] * (1023.0 / 127.0);  // decimals for float math
    // Map Deej output to MIDI limits (7-bit to 10-bit conversion)
    int limitedVal =
        map(analogSliderValues[i], idealOutputValues[0],
            idealOutputValues[arrayQty - 1], minVal10bit, maxVal10bit);
    if (limitedVal > 1023) {
      limitedVal = 1023;
    }
    builtString += String((int)limitedVal);
    if (i < NUM_TOTAL_SLIDERS - 1) {
      builtString += String("|");
    }
  }
  CompositeSerial.println(builtString);
}
