// hackpad - arduino sketch for xiao nrf52840
// 3x3 keys, encoder, 5 status leds, oled
// sends keys over bluetooth AND usb at the same time, so no mode switching
//
// board: Seeed nRF52 Boards -> Seeed XIAO nRF52840 (NOT the mbed one)
// tools -> usb stack -> tinyusb
// libs: Adafruit NeoPixel, U8g2

#include <bluefruit.h>
#include <Adafruit_TinyUSB.h>
#include <Adafruit_NeoPixel.h>
#include <U8g2lib.h>
#include <Wire.h>

// ---------- pins (same as pcb plan, change if diffrent) ----------
const uint8_t colPins[3] = {D0, D1, D2};
const uint8_t rowPins[3] = {D3, D8, D9};
#define ENC_A D6
#define ENC_B D7
#define LED_PIN D10

// ---------- settings ----------
#define ENC_REVERSE false        // knob goes the wrong way? set true
#define FULL_V 4.18              // usb plugged + above this = charged
#define VBAT_CAL 1.00            // fix after checking with multimeter
#define OLED_OFF_MS 45000        // screen off after this (saves battery)
const float levels[5] = {3.60, 3.75, 3.90, 4.05, 4.15};  // led turns on above this

// ---------- keymap ----------
// mod, key, consumer (media key). only one of key/consumer is used
struct Key { uint8_t mod; uint8_t key; uint16_t cons; };
const Key keymap[9] = {
  {0,                  HID_KEY_ESCAPE, 0},
  {KEYBOARD_MODIFIER_LEFTCTRL, HID_KEY_C, 0},
  {KEYBOARD_MODIFIER_LEFTCTRL, HID_KEY_V, 0},
  {KEYBOARD_MODIFIER_LEFTCTRL, HID_KEY_Z, 0},
  {KEYBOARD_MODIFIER_LEFTCTRL, HID_KEY_Y, 0},
  {KEYBOARD_MODIFIER_LEFTCTRL, HID_KEY_S, 0},
  {0, 0, HID_USAGE_CONSUMER_SCAN_PREVIOUS},
  {0, 0, HID_USAGE_CONSUMER_PLAY_PAUSE},
  {0, 0, HID_USAGE_CONSUMER_SCAN_NEXT},
};

// ---------- stuff ----------
Adafruit_NeoPixel leds(5, LED_PIN, NEO_GRB + NEO_KHZ800);
U8G2_SSD1309_128X64_NONAME0_F_HW_I2C oled(U8G2_R0, U8X8_PIN_NONE);

BLEDis bledis;
BLEHidAdafruit blehid;

uint8_t usbDesc[] = {
  TUD_HID_REPORT_DESC_KEYBOARD(HID_REPORT_ID(1)),
  TUD_HID_REPORT_DESC_CONSUMER(HID_REPORT_ID(2))
};
Adafruit_USBD_HID usbHid;

bool keyDown[9];
uint8_t debounce[9];
volatile int8_t encDelta = 0;
volatile int8_t encAcc = 0;
volatile uint8_t encState = 0;

float vbat = 0;
bool blinkOn = false;
unsigned long lastLed = 0, lastOled = 0, lastActivity = 0;
bool oledAsleep = false;

// ---------- sending keys ----------
void sendKeyboard(uint8_t mod, uint8_t key) {   // key = 0 means release
  uint8_t keys[6] = {key, 0, 0, 0, 0, 0};
  if (Bluefruit.connected()) blehid.keyboardReport(mod, keys);
  if (usbHid.ready()) usbHid.keyboardReport(1, mod, keys);
}

void sendConsumer(uint16_t usage) {             // usage = 0 means release
  if (Bluefruit.connected()) {
    if (usage) blehid.consumerKeyPress(usage);
    else blehid.consumerKeyRelease();
  }
  if (usbHid.ready()) usbHid.sendReport16(2, usage);
}

void press(const Key &k, bool down) {
  if (k.cons) sendConsumer(down ? k.cons : 0);
  else sendKeyboard(down ? k.mod : 0, down ? k.key : 0);
}

// ---------- matrix ----------
// diodes go col -> row, so drive the row low and read the cols
bool readKey(int r, int c) {
  pinMode(rowPins[r], OUTPUT);
  digitalWrite(rowPins[r], LOW);
  delayMicroseconds(20);
  bool pressed = (digitalRead(colPins[c]) == LOW);
  pinMode(rowPins[r], INPUT);   // back to floating
  return pressed;
}

void scanKeys() {
  for (int r = 0; r < 3; r++) {
    for (int c = 0; c < 3; c++) {
      int i = r * 3 + c;
      bool now = readKey(r, c);
      if (now != keyDown[i]) {
        debounce[i]++;
        if (debounce[i] >= 3) {       // 3 scans in a row = real
          keyDown[i] = now;
          debounce[i] = 0;
          press(keymap[i], now);
          lastActivity = millis();
        }
      } else {
        debounce[i] = 0;
      }
    }
  }
}

// ---------- encoder (interrupt so we dont miss steps while oled draws) ----------
void encISR() {
  static const int8_t table[16] = {0,-1,1,0, 1,0,0,-1, -1,0,0,1, 0,1,-1,0};
  encState = ((encState << 2) | (digitalRead(ENC_A) << 1) | digitalRead(ENC_B)) & 0x0F;
  encAcc += table[encState];
  if (encAcc >= 4)  { encDelta++; encAcc = 0; }
  if (encAcc <= -4) { encDelta--; encAcc = 0; }
}

void handleEncoder() {
  int8_t d = encDelta;
  if (d == 0) return;
  encDelta = 0;
  if (ENC_REVERSE) d = -d;
  uint16_t usage = (d > 0) ? HID_USAGE_CONSUMER_VOLUME_INCREMENT : HID_USAGE_CONSUMER_VOLUME_DECREMENT;
  for (int i = 0; i < abs(d); i++) {
    sendConsumer(usage);
    sendConsumer(0);
  }
  lastActivity = millis();
}

// ---------- battery / usb ----------
float readBattery() {
  digitalWrite(VBAT_ENABLE, LOW);     // turns the divider on
  delay(2);
  long total = 0;
  for (int i = 0; i < 8; i++) total += analogRead(PIN_VBAT);
  digitalWrite(VBAT_ENABLE, HIGH);
  float raw = total / 8.0;
  // 12 bit adc, 3.0v ref, divider about 2.96 (seeed docs, not 100% sure)
  return raw / 4095.0 * 3.0 * 2.96 * VBAT_CAL;
}

bool usbPower() {
  return (NRF_POWER->USBREGSTATUS & POWER_USBREGSTATUS_VBUSDETECT_Msk) != 0;
}

int batteryPercent() {
  int p = (int)((vbat - 3.3) / (4.2 - 3.3) * 100);
  if (p < 0) p = 0;
  if (p > 100) p = 100;
  return p;
}

// ---------- leds ----------
void setAll(uint32_t color) {
  for (int i = 0; i < 5; i++) leds.setPixelColor(i, color);
  leds.show();
}

void updateLeds() {
  blinkOn = !blinkOn;
  uint32_t green = leds.Color(0, 255, 0);
  uint32_t amber = leds.Color(255, 90, 0);
  uint32_t blue  = leds.Color(0, 0, 255);

  if (vbat < 2.5) { setAll(0); return; }      // no battery plugged in

  if (usbPower()) {
    if (vbat >= FULL_V) {
      setAll(green);                          // charged
    } else {
      bool filling = false;                   // charging, show level
      for (int i = 0; i < 5; i++) {
        if (vbat >= levels[i]) {
          leds.setPixelColor(i, amber);
        } else if (!filling) {
          leds.setPixelColor(i, blinkOn ? amber : 0);   // next one blinks
          filling = true;
        } else {
          leds.setPixelColor(i, 0);
        }
      }
      leds.show();
    }
  } else {
    if (Bluefruit.connected()) setAll(0);     // paired, leds off
    else setAll(blinkOn ? blue : 0);          // not paired = blue blink
  }
}

// ---------- oled ----------
void updateOled() {
  if (millis() - lastActivity > OLED_OFF_MS) {
    if (!oledAsleep) { oled.setPowerSave(1); oledAsleep = true; }
    return;
  }
  if (oledAsleep) { oled.setPowerSave(0); oledAsleep = false; }

  oled.clearBuffer();
  oled.setFont(u8g2_font_ncenB10_tr);
  oled.drawStr(0, 14, "HACKPAD");
  oled.setFont(u8g2_font_6x12_tr);

  char line[24];
  snprintf(line, sizeof(line), "bt: %s", Bluefruit.connected() ? "connected" : "waiting");
  oled.drawStr(0, 32, line);
  snprintf(line, sizeof(line), "usb: %s", usbPower() ? "plugged" : "no");
  oled.drawStr(0, 45, line);
  snprintf(line, sizeof(line), "bat: %d%% (%.2fV)", batteryPercent(), vbat);
  oled.drawStr(0, 58, line);
  oled.sendBuffer();
}

// ---------- setup ----------
void startBle() {
  Bluefruit.begin();
  Bluefruit.setTxPower(4);
  Bluefruit.setName("Hackpad");

  bledis.setManufacturer("DIY");
  bledis.setModel("Hackpad");
  bledis.begin();
  blehid.begin();

  Bluefruit.Advertising.addFlags(BLE_GAP_ADV_FLAGS_LE_ONLY_GENERAL_DISC_MODE);
  Bluefruit.Advertising.addTxPower();
  Bluefruit.Advertising.addAppearance(BLE_APPEARANCE_HID_KEYBOARD);
  Bluefruit.Advertising.addService(blehid);
  Bluefruit.Advertising.addName();
  Bluefruit.Advertising.restartOnDisconnect(true);
  Bluefruit.Advertising.setInterval(32, 244);
  Bluefruit.Advertising.setFastTimeout(30);
  Bluefruit.Advertising.start(0);    // 0 = never stop advertising
}

void setup() {
  // usb hid
  usbHid.setPollInterval(2);
  usbHid.setReportDescriptor(usbDesc, sizeof(usbDesc));
  usbHid.begin();

  // matrix pins
  for (int c = 0; c < 3; c++) pinMode(colPins[c], INPUT_PULLUP);
  for (int r = 0; r < 3; r++) pinMode(rowPins[r], INPUT);

  // encoder
  pinMode(ENC_A, INPUT_PULLUP);
  pinMode(ENC_B, INPUT_PULLUP);
  attachInterrupt(digitalPinToInterrupt(ENC_A), encISR, CHANGE);
  attachInterrupt(digitalPinToInterrupt(ENC_B), encISR, CHANGE);

  // battery
  pinMode(VBAT_ENABLE, OUTPUT);
  digitalWrite(VBAT_ENABLE, HIGH);
  analogReference(AR_INTERNAL_3_0);
  analogReadResolution(12);

  // leds + oled
  leds.begin();
  leds.setBrightness(40);      // low = battery lasts
  leds.show();
  oled.begin();

  startBle();
  lastActivity = millis();
}

void loop() {
  scanKeys();
  handleEncoder();

  if (millis() - lastLed > 500) {
    lastLed = millis();
    vbat = readBattery();
    updateLeds();
  }
  if (millis() - lastOled > 1000) {
    lastOled = millis();
    updateOled();
  }
  delay(2);
}
