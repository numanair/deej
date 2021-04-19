#include "BluetoothSerial.h"

BluetoothSerial SerialBT;

volatile boolean change = true;

static gpio_num_t rot1_pinA = GPIO_NUM_15;
static gpio_num_t rot1_pinB = GPIO_NUM_16;
static gpio_num_t rot1_btn  = GPIO_NUM_27;
volatile int rot1_pos  = 0;
volatile byte rot1_prev = 0;
volatile byte rot1_btn_state = 0;

static gpio_num_t rot2_pinA = GPIO_NUM_17;
static gpio_num_t rot2_pinB = GPIO_NUM_18;
static gpio_num_t rot2_btn  = GPIO_NUM_33;
volatile int rot2_pos  = 0;
volatile byte rot2_prev = 0;
volatile byte rot2_btn_state = 0;

static gpio_num_t rot3_pinA = GPIO_NUM_25;
static gpio_num_t rot3_pinB = GPIO_NUM_26;
static gpio_num_t rot3_btn  = GPIO_NUM_35;
volatile int rot3_pos  = 0;
volatile byte rot3_prev = 0;
volatile byte rot3_btn_state = 0;

static gpio_num_t rot4_pinA = GPIO_NUM_19;
static gpio_num_t rot4_pinB = GPIO_NUM_21;
static gpio_num_t rot4_btn  = GPIO_NUM_32;
volatile int rot4_pos  = 0;
volatile byte rot4_prev = 0;
volatile byte rot4_btn_state = 0;

static gpio_num_t rot5_pinA = GPIO_NUM_22;
static gpio_num_t rot5_pinB = GPIO_NUM_23;
static gpio_num_t rot5_btn  = GPIO_NUM_34;
volatile int rot5_pos  = 0;
volatile byte rot5_prev = 0;
volatile byte rot5_btn_state = 0;

void IRAM_ATTR rotate(gpio_num_t pin_a, gpio_num_t pin_b, volatile int &pos, volatile byte &prev) {
  int pin_a_level = gpio_get_level(pin_a);
  int pin_b_level = gpio_get_level(pin_b);

  prev = (prev << 2 | (pin_b_level << 1) | pin_a_level) & 15;

  if (prev == 14) {
    pos--;
    change = true;
  } else if (prev == 13) {
    pos++;
    change = true;
  }
}

void IRAM_ATTR rot1_f() {
  rotate(rot1_pinA, rot1_pinB, rot1_pos, rot1_prev);
}

void IRAM_ATTR rot2_f() {
  rotate(rot2_pinA, rot2_pinB, rot2_pos, rot2_prev);
}

void IRAM_ATTR rot3_f() {
  rotate(rot3_pinA, rot3_pinB, rot3_pos, rot3_prev);
}

void IRAM_ATTR rot4_f() {
  rotate(rot4_pinA, rot4_pinB, rot4_pos, rot4_prev);
}

void IRAM_ATTR rot5_f() {
  rotate(rot5_pinA, rot5_pinB, rot5_pos, rot5_prev);
}

void IRAM_ATTR button(gpio_num_t pin, volatile byte &state) {
  int pin_button_level = gpio_get_level(pin);
  if (pin_button_level == 0) {
    state = 1 - state;
    change = true;
  }
}

void IRAM_ATTR rot1_btn_f() {
  button(rot1_btn, rot1_btn_state);
}

void IRAM_ATTR rot2_btn_f() {
  button(rot2_btn, rot2_btn_state);
}

void IRAM_ATTR rot3_btn_f() {
  button(rot3_btn, rot3_btn_state);
}

void IRAM_ATTR rot4_btn_f() {
  button(rot4_btn, rot4_btn_state);
}

void IRAM_ATTR rot5_btn_f() {
  button(rot5_btn, rot5_btn_state);
}

void setup() {
  Serial.begin(9600);
  SerialBT.begin("ESP32-deej");

  pinMode(rot1_pinA, INPUT);
  pinMode(rot1_pinB, INPUT);
  pinMode(rot1_btn,  INPUT);

  pinMode(rot2_pinA, INPUT);
  pinMode(rot2_pinB, INPUT);
  pinMode(rot2_btn,  INPUT);

  pinMode(rot3_pinA, INPUT);
  pinMode(rot3_pinB, INPUT);
  pinMode(rot3_btn,  INPUT);

  pinMode(rot4_pinA, INPUT);
  pinMode(rot4_pinB, INPUT);
  pinMode(rot4_btn,  INPUT);

  pinMode(rot5_pinA, INPUT);
  pinMode(rot5_pinB, INPUT);
  pinMode(rot5_btn,  INPUT);

  attachInterrupt(rot1_pinA, rot1_f, CHANGE);
  attachInterrupt(rot1_pinB, rot1_f, CHANGE);
  attachInterrupt(rot1_btn,  rot1_btn_f,  CHANGE);

  attachInterrupt(rot2_pinA, rot2_f, CHANGE);
  attachInterrupt(rot2_pinB, rot2_f, CHANGE);
  attachInterrupt(rot2_btn,  rot2_btn_f,  CHANGE);

  attachInterrupt(rot3_pinA, rot3_f, CHANGE);
  attachInterrupt(rot3_pinB, rot3_f, CHANGE);
  attachInterrupt(rot3_btn,  rot3_btn_f,  CHANGE);

  attachInterrupt(rot4_pinA, rot4_f, CHANGE);
  attachInterrupt(rot4_pinB, rot4_f, CHANGE);
  attachInterrupt(rot4_btn,  rot4_btn_f,  CHANGE);

  attachInterrupt(rot5_pinA, rot5_f, CHANGE);
  attachInterrupt(rot5_pinB, rot5_f, CHANGE);
  attachInterrupt(rot5_btn,  rot5_btn_f,  CHANGE);
}

void sendSliderValues() {
  String builtString = String("");

  builtString += rot1_btn_state ? "x" : String(rot1_pos);
  builtString += String("|");
  builtString += rot2_btn_state ? "x" : String(rot2_pos);
  builtString += String("|");
  builtString += rot3_btn_state ? "x" : String(rot3_pos);
  builtString += String("|");
  builtString += rot4_btn_state ? "x" : String(rot4_pos);
  builtString += String("|");
  builtString += rot5_btn_state ? "x" : String(rot5_pos);

  Serial.println(builtString);
  SerialBT.println(builtString);
}

void loop() {
  if (change) {
    sendSliderValues();
    change = false;

    rot1_pos = 0;
    rot2_pos = 0;
    rot3_pos = 0;
    rot4_pos = 0;
    rot5_pos = 0;
  }
  delay(10);
}
