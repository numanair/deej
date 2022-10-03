#include <Arduino.h>
// #include <Packet.h>          // for SerialTransfer
// #include <PacketCRC.h>       // for SerialTransfer
#include <STM32ADC.h>        // STM32
#include <SerialTransfer.h>  // for SerialTransfer
#include <neotimer.h>

// AXILLARY (secondary) board firmware
const String firmwareVersion = "v1.1.0-tx-dev";

// Number of potentiometers or faders
const uint8_t NUM_SLIDERS = 5;      // Faders connected to primary board
const uint8_t NUM_AUX_SLIDERS = 4;  //  Faders on secondary I2C board
const uint8_t NUM_TOTAL_SLIDERS =
    NUM_AUX_SLIDERS + NUM_SLIDERS;  //  Total faders count

// Analog pins assignment
const uint8_t analogInputs[NUM_AUX_SLIDERS] = {PA0, PA1, PA2, PA3};

Neotimer mytimer = Neotimer(1);

uint16_t auxVal[NUM_AUX_SLIDERS] = {0};

STM32ADC myADC(ADC1);  // STM32

SerialTransfer myTransfer;

void sendSliderValues();

void setup() {
  for (int i = 0; i < NUM_SLIDERS; i++) {
    pinMode(analogInputs[i], INPUT);
  }
  const int LED_PIN = PC13;
  pinMode(LED_PIN, OUTPUT);
  digitalWrite(LED_PIN, LOW); // LED off on blue pill

  myADC.calibrate();

  Serial.begin(9600);     // usb serial
  Serial1.begin(115200);  // UART port 1
  myTransfer.begin(Serial1);

  delay(100);
}

void loop() {
  for (int i = 0; i < NUM_AUX_SLIDERS; i++) {
    auxVal[i] = analogRead(analogInputs[i]);
  }
  delay(2);
  // send data
  if (mytimer.repeat()) {
    sendSliderValues();  // send
  }
}

void sendSliderValues() {
  String builtString = String("");
  builtString += String("<");
  for (int i = 0; i < NUM_AUX_SLIDERS; i++) {
    builtString += String((int)auxVal[i]);
    if (i < NUM_AUX_SLIDERS - 1) {
      builtString += String(",");
    } else {
      builtString += String(">");
    }
  }
  Serial.println(builtString);
  // Serial1.print(builtString);  // send
  myTransfer.sendDatum(auxVal);  // serialTransfer datum (single obj)
}