#include <Arduino.h>
#include <EEPROM.h>
#include <ResponsiveAnalogRead.h>
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

const String firmwareVersion = "v1.2.0";

// Number of potentiometers or faders
const uint8_t NUM_SLIDERS = 5;

// Potentiometer pins assignment
const uint8_t analogInputs[NUM_SLIDERS] = {0, 1, 2, 3, 4};

uint8_t midi_channel[NUM_SLIDERS] = {1, 1, 1, 1, 1};   // 1 through 16
uint8_t cc_command[NUM_SLIDERS] = {1, 11, 7, 14, 21};  // MIDI CC number

uint8_t cc_lower_limit[NUM_SLIDERS] = {
    0, 0, 0, 0, 0};  // optionally limit range of MIDI CC per fader
uint8_t cc_upper_limit[NUM_SLIDERS] = {
    127, 127, 127, 127, 127};  // optionally limit range of MIDI CC per fader

const byte MAX_RECEIVE_LENGTH = (NUM_SLIDERS * 3 - 1) * 2 + 1 + 6;
char receivedChars[MAX_RECEIVE_LENGTH];
char tempChars[MAX_RECEIVE_LENGTH];  // temporary array for use when parsing

// variables to hold the parsed data
char messageFromPC[MAX_RECEIVE_LENGTH] = {0};
int integerFromPC = 0;

bool newData = false;

// Adjusts linearity correction for my specific potentiometers.
// 1 = fully linear but affects resolution. 0.7 is about max for no impact.
// const float correctionMultiplier = 0.0;
const float correctionMultiplier = 0.60;  // good balance
// const float correctionMultiplier = 1.00;
const uint8_t threshold = 36;  // 32ish

// measured output every equal 5mm increment in 12-bit. Minimum and maximum
// values are not affected by correctionMultiplier.
const uint16_t measuredInput[] = {19,   50,   165,  413,  907,  1450, 1975,
                                  2545, 3095, 3645, 3923, 4030, 4082};

// Calculate number of elements in the MultiMap arrays
const uint8_t arrayQty = sizeof(measuredInput) / sizeof(measuredInput[0]);
uint16_t adjustedInputVal[arrayQty] = {0};  // Same type as measuredInput

// Probably no need to change these calculated values
uint16_t idealOutputValues[arrayQty] = {0};
// Note: 4095 = 2^12 - 1 (the maximum value that can be represented by
// a 12-bit unsigned number

int old_value[NUM_SLIDERS] = {0};
int new_value[NUM_SLIDERS] = {0};
int old_midi_value[NUM_SLIDERS] = {0};
int new_midi_value[NUM_SLIDERS] = {0};
int analogSliderValues[NUM_SLIDERS];
const int MAX_MESSAGE_LENGTH = NUM_SLIDERS * 6;  // sliders * 00:00,
bool prog_end = 0;
bool CC_CH_mode = 1;
int deej = 1;  // 1=enabled 0=paused -1=disabled
int addressWriteCC = 20;
int addressWriteChan = addressWriteCC + NUM_SLIDERS;
int addressWriteUpperLimit = addressWriteChan + NUM_SLIDERS;
int addressWriteLowerLimit = addressWriteUpperLimit + NUM_SLIDERS;

Neotimer mytimer = Neotimer(1);     // ms ADC polling interval
Neotimer deejtimer = Neotimer(10);  // ms send deej
Neotimer mytimer2 = Neotimer(2000);
// ms delay before saving settings/resuming Deej output.
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

// Initialize ResponsiveAnalogRead object size
// The actual settings are initialized later
ResponsiveAnalogRead analog[NUM_SLIDERS];

void setup() {
  const int adc_bits = 12;
  for (int i = 0; i < NUM_SLIDERS; i++) {
    // ResponsiveAnalogRead settings
    // (pin, sleep, snapMultiplier)
    analog[i] = ResponsiveAnalogRead(analogInputs[i], true, .0001);
    analog[i].setAnalogResolution(1 << adc_bits);  // 2^adc_bits
    analog[i].setActivityThreshold(threshold);
    analog[i].enableEdgeSnap();
  }
  pinMode(PC13, OUTPUT);
  digitalWrite(PC13, LOW);  // Turn on LED during boot
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
    // Initialize values for idealOutputValues array (4095 = 2^12 - 1)
    idealOutputValues[i] = round(i * (4095.0 / (arrayQty - 1)));

    adjustedInputVal[i] =
        round(idealOutputValues[i] +
              (measuredInput[i] - idealOutputValues[i]) * correctionMultiplier);
    // theoretical ideal + (measured - theoretical) * multi
  }
  // Excludes min and max from adjustment by correctionMultiplier
  adjustedInputVal[0] = idealOutputValues[0];  // min value
  adjustedInputVal[arrayQty - 1] = idealOutputValues[arrayQty - 1];

  midi.begin();
  CompositeSerial.begin(9600);

  delay(1000);
  // EEPROM setup:
  const int addressFlag = 10;
  if (EEPROM.read(addressFlag) == 200) {
    // EEPROM already set. Reading.
    CompositeSerial.println("EEPROM already set. Reading");
    readFromEEPROM(addressWriteCC, cc_command, NUM_SLIDERS, 127);     // CC
    readFromEEPROM(addressWriteChan, midi_channel, NUM_SLIDERS, 16);  // Channel
    readFromEEPROM(addressWriteLowerLimit, cc_lower_limit, NUM_SLIDERS,
                   127);  // Lower bound of each fader output
    readFromEEPROM(addressWriteUpperLimit, cc_upper_limit, NUM_SLIDERS,
                   127);   // Upper bound of each fader output
    printSettings();       // print settings to serial
    printLimitSettings();  // print settings to serial
  } else {
    // First run, set EEPROM data to defaults
    CompositeSerial.println("First run, set EEPROM data to defaults");
    writeToEEPROM(addressWriteCC, cc_command, NUM_SLIDERS, 127);     // CC
    writeToEEPROM(addressWriteChan, midi_channel, NUM_SLIDERS, 16);  // Channel
    writeToEEPROM(addressWriteLowerLimit, cc_lower_limit, NUM_SLIDERS,
                  127);  // Lower bound of each fader output
    writeToEEPROM(addressWriteUpperLimit, cc_upper_limit, NUM_SLIDERS,
                  127);              // Upper bound of each fader output
    EEPROM.write(addressFlag, 200);  // mark EEPROM as set
  }

  delay(500);
  digitalWrite(PC13, HIGH);  // Turn off LED on Blue Pill boards
}

void loop() {
  // Deej loop and MIDI values and sending every 10ms
  if (mytimer.repeat()) {
    updateSliderValues();  // Gets new slider values
    filteredAnalog();      // MIDI

    if (deej > 0 && deejtimer.repeat()) {
      sendSliderValues();  // Deej Serial
    } else if (mytimer2.done()) {
      if (prog_end) {
        writeToEEPROM(addressWriteCC, cc_command, NUM_SLIDERS, 127);
        writeToEEPROM(addressWriteChan, midi_channel, NUM_SLIDERS, 16);
        writeToEEPROM(addressWriteLowerLimit, cc_lower_limit, NUM_SLIDERS, 127);
        writeToEEPROM(addressWriteUpperLimit, cc_upper_limit, NUM_SLIDERS, 127);
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
      // Output MIDI limits to serial in the input format
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

  // Checks for incoming MIDI.
  // Currently only to prevent hangs on some software
  midi.poll();
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

    // replace with switch case?
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
    } else if (rc == togDeej) {
      if (deej > 0) {
        deej = -1;  // disable deej serial output
        CompositeSerial.println("Deej disabled.");
      } else if (deej < 0) {
        deej = 1;  // re-enable deej serial output
        CompositeSerial.println("Deej enabled.");
      }
    } else if (rc == helpMode) {
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
      CompositeSerial.println(
          "Settings are assigned in this format: <1,11,7,14,21:1,1,1,1,1>");
      CompositeSerial.println(
          "and correspond to <CC:Channel> or <lower_limit:upper_limit> ");
      CompositeSerial.println("depending on the mode.");
      CompositeSerial.println("The default limits are 0-127 and can ");
      CompositeSerial.println("be swapped to reverse the output.");
    }
  }
}

// <CC,CC,CC,CC,CC:CH,CH,CH,CH,CH>
// <07,14,14,14,14:01,01,01,01,01>

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
  for (int i = 0; i < NUM_SLIDERS; i++) {
    if (i == 0) {
      strtokIndx2 = strtok(stringCC, ",");
    } else if (strtokIndx1 != NULL) {
      strtokIndx2 = strtok(NULL, ",");  // next token
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
  for (int i = 0; i < NUM_SLIDERS; i++) {
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
  for (int i = 0; i < NUM_SLIDERS; i++) {
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
  printArray(cc_command, NUM_SLIDERS);
  CompositeSerial.print(":");
  printArray(midi_channel, NUM_SLIDERS);
  CompositeSerial.print(">");
  CompositeSerial.print('\n');  // newline
}

void printLimitSettings() {
  CompositeSerial.println("MIDI limits");
  CompositeSerial.print("<");
  printArray(cc_lower_limit, NUM_SLIDERS);
  CompositeSerial.print(":");
  printArray(cc_upper_limit, NUM_SLIDERS);
  CompositeSerial.print(">");
  CompositeSerial.print('\n');  // newline
}

void filteredAnalog() {
  for (int i = 0; i < NUM_SLIDERS; i++) {
    if (analog[i].hasChanged()) {
      uint adaptiveval = analogSliderValues[i];

      // linear adjustment
      adaptiveval = multiMap<uint16>(adaptiveval, adjustedInputVal,
                                     idealOutputValues, arrayQty);

      // trim ends (add deadzone)
      const uint lower_deadzone = 1;
      const uint upper_deadzone = 4095 - 1;
      adaptiveval = constrain(adaptiveval, lower_deadzone, upper_deadzone);
      adaptiveval = map(adaptiveval, lower_deadzone, upper_deadzone, 0, 4095);

      adaptiveval = adaptiveval >> 5;  // 12 to 7-bit (128)
      constrain(adaptiveval, 0, 127);  // cap output to MIDI range

      // map to user specified range
      new_value[i] =
          map(adaptiveval, 0, 127, cc_lower_limit[i], cc_upper_limit[i]);

      if (new_value[i] != old_value[i]) {
        // Update old value from new one
        old_value[i] = new_value[i];

        // Send MIDI
        // Channel starts at 0, but midi_channel starts at 1.
        midi.sendControlChange(midi_channel[i] - 1, cc_command[i],
                               new_value[i]);
      }
    }
  }
}

void updateSliderValues() {
  for (int i = 0; i < NUM_SLIDERS; i++) {
    analog[i].update();  // ResponsiveAnalogRead
    analogSliderValues[i] = analog[i].getValue();
  }
}

// Deej Serial Support
void sendSliderValues() {
  String builtString = String("");
  for (int i = 0; i < NUM_SLIDERS; i++) {
    // User set limits = 0-127.
    uint minVal10bit =
        cc_lower_limit[i] * (1023.0 / 127.0);  // decimals for float math
    uint maxVal10bit =
        cc_upper_limit[i] * (1023.0 / 127.0);  // decimals for float math
    // linearize raw value
    uint limitedVal = multiMap<uint16>(
        analog[i].getRawValue(), adjustedInputVal, idealOutputValues, arrayQty);
    // Map Deej output to MIDI limits (7-bit to 10-bit conversion)
    limitedVal = map(limitedVal, idealOutputValues[0],
                     idealOutputValues[arrayQty - 1], minVal10bit, maxVal10bit);
    constrain(limitedVal, 0, 1023);
    builtString += String((int)limitedVal);
    if (i < NUM_SLIDERS - 1) {
      builtString += String("|");
    }
  }
  CompositeSerial.println(builtString);
}
