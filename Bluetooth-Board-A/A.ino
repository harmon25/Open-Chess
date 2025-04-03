#include <ArduinoBLE.h>
#include <Adafruit_NeoPixel.h>

// ---------------------------
// NeoPixel Setup
// ---------------------------
#define LED_PIN     5       // Pin for NeoPixels
#define NUM_ROWS    8
#define NUM_COLS    8
#define LED_COUNT   (NUM_ROWS * NUM_COLS)
#define BRIGHTNESS  100

Adafruit_NeoPixel strip(LED_COUNT, LED_PIN, NEO_GRBW + NEO_KHZ800);

// ---------------------------
// Shift Register (74HC594) Pins
// ---------------------------
#define SER_PIN     2   // Serial data input (pin 14)
#define SRCLK_PIN   3   // Shift register clock (pin 11)
#define RCLK_PIN    4   // Latch clock (pin 12)

// ---------------------------
// Column Input Pins (D6..D13)
// ---------------------------
int colPins[NUM_COLS] = {6, 7, 8, 9, 10, 11, 12, 13};

// ---------------------------
// Row Patterns (LSB-first)
// ---------------------------
byte rowPatterns[8] = {
  0x01, // Row 0
  0x02, // Row 1
  0x04, // Row 2
  0x08, // Row 3
  0x10, // Row 4
  0x20, // Row 5
  0x40, // Row 6
  0x80  // Row 7
};

// ---------------------------
// BLE Setup (Central)
// ---------------------------
#define SERVICE_UUID         "19B10010-E8F2-537E-4F6C-D104768A1214"
#define CHARACTERISTIC_UUID  "19B10011-E8F2-537E-4F6C-D104768A1214"

BLEDevice peripheral;
BLECharacteristic chessCharacteristic;  // Discovered characteristic

// ---------------------------
// Global Variables for Sensor Tracking
// ---------------------------
bool lastStableSensor[NUM_ROWS][NUM_COLS];
bool currentSensor[NUM_ROWS][NUM_COLS];
uint8_t pendingState[NUM_ROWS][NUM_COLS];

unsigned long lastBlinkTime = 0;
const unsigned long blinkInterval = 500; // milliseconds
bool blinkOn = false;

bool connectedBefore = false;  // To trigger the explosion animation only once

// ---------------------------
// Helper Functions
// ---------------------------

int getPixelIndex(int row, int col) {
  return col * NUM_ROWS + (NUM_ROWS - 1 - row);
}

void loadShiftRegister(byte data) {
  digitalWrite(RCLK_PIN, LOW);
  for (int i = 0; i < 8; i++) {
    bool bitVal = (data & (1 << i)) != 0;
    digitalWrite(SER_PIN, bitVal ? HIGH : LOW);
    digitalWrite(SRCLK_PIN, HIGH);
    delayMicroseconds(10);
    digitalWrite(SRCLK_PIN, LOW);
    delayMicroseconds(10);
  }
  digitalWrite(RCLK_PIN, HIGH);
  delayMicroseconds(10);
  digitalWrite(RCLK_PIN, LOW);
}

void scanSensors(bool sensors[NUM_ROWS][NUM_COLS]) {
  for (int row = 0; row < NUM_ROWS; row++) {
    loadShiftRegister(rowPatterns[row]);
    delayMicroseconds(100);  // Allow signals to settle
    for (int col = 0; col < NUM_COLS; col++) {
      int sensorVal = digitalRead(colPins[col]);
      sensors[row][col] = (sensorVal == LOW);
    }
  }
  loadShiftRegister(0x00);
}

void sendBLEMessage(String msg) {
  Serial.print("Sending BLE message: ");
  Serial.println(msg);
  chessCharacteristic.writeValue(msg.c_str(), msg.length());
}

void checkForSensorChanges() {
  for (int row = 0; row < NUM_ROWS; row++) {
    for (int col = 0; col < NUM_COLS; col++) {
      if (pendingState[row][col] == 0) {
        if (currentSensor[row][col] != lastStableSensor[row][col]) {
          if (currentSensor[row][col] == false) {
            String msg = "REMOVE," + String(row) + "," + String(col);
            sendBLEMessage(msg);
            pendingState[row][col] = 1;
          } else {
            String msg = "ADD," + String(row) + "," + String(col);
            sendBLEMessage(msg);
            pendingState[row][col] = 2;
          }
          lastStableSensor[row][col] = currentSensor[row][col];
        }
      } else {
        if (pendingState[row][col] == 1 && currentSensor[row][col] == false)
          pendingState[row][col] = 0;
        else if (pendingState[row][col] == 2 && currentSensor[row][col] == true)
          pendingState[row][col] = 0;
      }
    }
  }
}

void updateLEDs() {
  unsigned long currentMillis = millis();
  if (currentMillis - lastBlinkTime >= blinkInterval) {
    blinkOn = !blinkOn;
    lastBlinkTime = currentMillis;
  }
  
  for (int row = 0; row < NUM_ROWS; row++) {
    for (int col = 0; col < NUM_COLS; col++) {
      int idx = getPixelIndex(row, col);
      if (pendingState[row][col] == 1) {
        if (blinkOn)
          strip.setPixelColor(idx, strip.Color(0, 0, 0, 255));
        else
          strip.setPixelColor(idx, 0);
      } else if (pendingState[row][col] == 2) {
        strip.setPixelColor(idx, strip.Color(0, 0, 0, 255));
      } else {
        strip.setPixelColor(idx, 0);
      }
    }
  }
  strip.show();
}

void processBLEMessage(String msg) {
  Serial.print("Received BLE message: ");
  Serial.println(msg);
  int firstComma = msg.indexOf(',');
  int secondComma = msg.indexOf(',', firstComma + 1);
  if (firstComma == -1 || secondComma == -1) return;
  String command = msg.substring(0, firstComma);
  int row = msg.substring(firstComma + 1, secondComma).toInt();
  int col = msg.substring(secondComma + 1).toInt();
  if (command == "REMOVE") {
    pendingState[row][col] = 1;
  } else if (command == "ADD") {
    pendingState[row][col] = 2;
  }
}

void playExplosionAnimation() {
  Serial.println("Playing explosion animation");
  int center = 3;
  for (int r = 0; r <= 4; r++) {
    for (int dx = -r; dx <= r; dx++) {
      for (int dy = -r; dy <= r; dy++) {
        int x = center + dx;
        int y = center + dy;
        if (x >= 0 && x < NUM_COLS && y >= 0 && y < NUM_ROWS) {
          int idx = getPixelIndex(y, x);
          strip.setPixelColor(idx, strip.Color(0, 0, 0, 255));
        }
      }
    }
    strip.show();
    delay(50);
  }
  for (int r = 4; r >= 0; r--) {
    for (int dx = -r; dx <= r; dx++) {
      for (int dy = -r; dy <= r; dy++) {
        int x = center + dx;
        int y = center + dy;
        if (x >= 0 && x < NUM_COLS && y >= 0 && y < NUM_ROWS) {
          int idx = getPixelIndex(y, x);
          strip.setPixelColor(idx, 0);
        }
      }
    }
    strip.show();
    delay(50);
  }
}

void setup() {
  Serial.begin(9600);
  
  for (int row = 0; row < NUM_ROWS; row++) {
    for (int col = 0; col < NUM_COLS; col++) {
      lastStableSensor[row][col] = false;
      currentSensor[row][col] = false;
      pendingState[row][col] = 0;
    }
  }
  
  strip.begin();
  strip.show();
  strip.setBrightness(BRIGHTNESS);

  pinMode(SER_PIN, OUTPUT);
  pinMode(SRCLK_PIN, OUTPUT);
  pinMode(RCLK_PIN, OUTPUT);
  for (int c = 0; c < NUM_COLS; c++) {
    pinMode(colPins[c], INPUT);
  }
  loadShiftRegister(0x00);
  
  if (!BLE.begin()) {
    Serial.println("BLE Central initialization failed!");
    while (1);
  }
  Serial.println("BLE Central - scanning for ChessBoard devices");
  BLE.scanForUuid(SERVICE_UUID);
}

void loop() {
  if (!peripheral) {
    peripheral = BLE.available();
    if (peripheral) {
      Serial.print("Found peripheral: ");
      Serial.println(peripheral.localName());
      if (peripheral.localName() == "ChessBoard") {
        BLE.stopScan();
        Serial.println("Attempting to connect...");
        if (peripheral.connect()) {
          Serial.println("Connected to peripheral");
          if (peripheral.discoverAttributes()) {
            chessCharacteristic = peripheral.characteristic(CHARACTERISTIC_UUID);
            if (chessCharacteristic) {
              Serial.println("Characteristic found!");
              // Subscribe to notifications so that we can receive BLE messages
              if (chessCharacteristic.canSubscribe()) {
                chessCharacteristic.subscribe();
                Serial.println("Subscribed to notifications!");
              }
              if (!connectedBefore) {
                playExplosionAnimation();
                connectedBefore = true;
              }
              String initMsg = "INIT";
              chessCharacteristic.writeValue(initMsg.c_str(), initMsg.length());
            } else {
              Serial.println("Characteristic not found!");
            }
          } else {
            Serial.println("Attribute discovery failed!");
          }
        } else {
          Serial.println("Connection failed!");
        }
      }
    }
  }
  
  if (peripheral && peripheral.connected()) {
    if (chessCharacteristic.valueUpdated()) {
      int len = chessCharacteristic.valueLength();
      char buffer[21];
      if (len > 20) len = 20;
      chessCharacteristic.readValue((byte*)buffer, len);
      buffer[len] = '\0';
      Serial.print("Received: ");
      Serial.println(buffer);
      processBLEMessage(String(buffer));
    }
    scanSensors(currentSensor);
    checkForSensorChanges();
    updateLEDs();
    delay(50);
  } else {
    scanSensors(currentSensor);
    checkForSensorChanges();
    updateLEDs();
    delay(50);
  }
  
  if (peripheral && !peripheral.connected()) {
    Serial.println("Peripheral disconnected");
    peripheral = BLEDevice();
    BLE.scanForUuid(SERVICE_UUID);
  }
}
