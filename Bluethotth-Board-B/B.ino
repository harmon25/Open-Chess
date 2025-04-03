#include <Adafruit_NeoPixel.h>
#include <ArduinoBLE.h>

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
// (Remember: Tie OE and SRCLR HIGH as needed)

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
// BLE Setup (Peripheral)
// ---------------------------
BLEService chessService("19B10010-E8F2-537E-4F6C-D104768A1214");
BLECharacteristic chessCharacteristic("19B10011-E8F2-537E-4F6C-D104768A1214", 
                                       BLERead | BLEWrite | BLENotify, 20);

// ---------------------------
// Global Variables for Sensor Tracking
// ---------------------------
// A sensor is "active" (true) when a piece is present (digital LOW).
bool lastStableSensor[NUM_ROWS][NUM_COLS];
bool currentSensor[NUM_ROWS][NUM_COLS];

// Pending update state for each tile:
// 0 = Normal, 1 = Pending removal (blink), 2 = Pending addition (solid glow)
uint8_t pendingState[NUM_ROWS][NUM_COLS];

// For blinking pending removals
unsigned long lastBlinkTime = 0;
const unsigned long blinkInterval = 500; // milliseconds
bool blinkOn = false;

// ---------------------------
// Helper Functions
// ---------------------------

// Map a row and column to the correct NeoPixel index.
// Here we use: pixelIndex = col * NUM_ROWS + (NUM_ROWS - 1 - row)
int getPixelIndex(int row, int col) {
  return col * NUM_ROWS + (NUM_ROWS - 1 - row);
}

// Shift out data to the 74HC594 (LSB-first to match rowPatterns)
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

// Scan the sensor matrix and update the provided 2D array.
// For each row, activate it via the shift register then read each column.
void scanSensors(bool sensors[NUM_ROWS][NUM_COLS]) {
  for (int row = 0; row < NUM_ROWS; row++) {
    loadShiftRegister(rowPatterns[row]);
    delayMicroseconds(100);  // Allow signals to settle
    for (int col = 0; col < NUM_COLS; col++) {
      int sensorVal = digitalRead(colPins[col]);
      // Sensor is active (true) when digitalRead returns LOW (piece present)
      sensors[row][col] = (sensorVal == LOW);
    }
  }
  // Turn off all row lines after scanning
  loadShiftRegister(0x00);
}

// Send a BLE message (e.g., "REMOVE,6,1" or "ADD,4,3")
void sendBLEMessage(String msg) {
  Serial.print("Sending BLE message: ");
  Serial.println(msg);
  chessCharacteristic.writeValue(msg.c_str(), msg.length());
}

// Check for sensor state changes compared to the last stable state.
// If a change is detected (and the tile isn’t already pending), send a BLE message and mark the tile as pending.
void checkForSensorChanges() {
  for (int row = 0; row < NUM_ROWS; row++) {
    for (int col = 0; col < NUM_COLS; col++) {
      if (pendingState[row][col] == 0) {  // Only if not already pending
        if (currentSensor[row][col] != lastStableSensor[row][col]) {
          // Change detected:
          // If sensor goes from active (true) to inactive (false): piece removed.
          // If sensor goes from inactive (false) to active (true): piece added.
          if (currentSensor[row][col] == false) {
            String msg = "REMOVE," + String(row) + "," + String(col);
            sendBLEMessage(msg);
            pendingState[row][col] = 1;  // Pending removal (blink)
          } else {
            String msg = "ADD," + String(row) + "," + String(col);
            sendBLEMessage(msg);
            pendingState[row][col] = 2;  // Pending addition (solid glow)
          }
          // Update last stable state immediately to avoid repeated broadcasts.
          lastStableSensor[row][col] = currentSensor[row][col];
        }
      } else {
        // If the tile is pending, check if the sensor now reflects the expected state.
        if (pendingState[row][col] == 1 && currentSensor[row][col] == false) {
          pendingState[row][col] = 0;
        } else if (pendingState[row][col] == 2 && currentSensor[row][col] == true) {
          pendingState[row][col] = 0;
        }
      }
    }
  }
}

// Update the NeoPixel display based solely on pending moves.
// Only tiles with uncompleted moves (pendingState != 0) will light up.
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
        // Pending removal: blink the LED.
        if (blinkOn)
          strip.setPixelColor(idx, strip.Color(0, 0, 0, 255));
        else
          strip.setPixelColor(idx, 0);
      } else if (pendingState[row][col] == 2) {
        // Pending addition: solid white glow.
        strip.setPixelColor(idx, strip.Color(0, 0, 0, 255));
      } else {
        // No uncompleted move: ensure the LED is off.
        strip.setPixelColor(idx, 0);
      }
    }
  }
  strip.show();
}

// Process an incoming BLE message and update the pending state accordingly.
// The key change here is to update lastStableSensor for that tile based on the remote command,
// so that the board does not repeatedly broadcast the same change.
void processBLEMessage(String msg) {
  Serial.print("Received BLE message: ");
  Serial.println(msg);
  int firstComma = msg.indexOf(',');
  int secondComma = msg.indexOf(',', firstComma + 1);
  if (firstComma == -1 || secondComma == -1) return; // Invalid format
  String command = msg.substring(0, firstComma);
  int row = msg.substring(firstComma + 1, secondComma).toInt();
  int col = msg.substring(secondComma + 1).toInt();
  if (command == "REMOVE") {
    pendingState[row][col] = 1;
    // Set expected state to "no piece" to prevent further broadcasts.
    lastStableSensor[row][col] = false;
  } else if (command == "ADD") {
    pendingState[row][col] = 2;
    // Set expected state to "piece present" to prevent further broadcasts.
    lastStableSensor[row][col] = true;
  }
}

// Play an explosion animation that radiates outward from the center then contracts.
void playExplosionAnimation() {
  Serial.println("Playing explosion animation");
  int center = 3; // Approximate center of an 8x8 grid
  // Explosion outward
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
  // Contraction inward
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

// ---------------------------
// SETUP
// ---------------------------
void setup() {
  Serial.begin(9600);
  
  // Initialize sensor state arrays and pending states.
  for (int row = 0; row < NUM_ROWS; row++) {
    for (int col = 0; col < NUM_COLS; col++) {
      lastStableSensor[row][col] = false;
      currentSensor[row][col] = false;
      pendingState[row][col] = 0;
    }
  }
  
  // Initialize NeoPixel strip.
  strip.begin();
  strip.show();
  strip.setBrightness(BRIGHTNESS);

  // Initialize shift register control pins.
  pinMode(SER_PIN, OUTPUT);
  pinMode(SRCLK_PIN, OUTPUT);
  pinMode(RCLK_PIN, OUTPUT);

  // Set up column input pins.
  for (int c = 0; c < NUM_COLS; c++) {
    pinMode(colPins[c], INPUT);
  }
  loadShiftRegister(0x00);
  
  // ---------------------------
  // Initialize BLE as Peripheral
  // ---------------------------
  if (!BLE.begin()) {
    Serial.println("BLE initialization failed!");
    while (1);
  }
  
  BLE.setLocalName("ChessBoard");
  BLE.setAdvertisedService(chessService);
  chessService.addCharacteristic(chessCharacteristic);
  BLE.addService(chessService);
  
  // Set an initial value (optional)
  String initMsg = "INIT";
  chessCharacteristic.writeValue(initMsg.c_str(), initMsg.length());
  
  BLE.advertise();
  Serial.println("BLE Peripheral: Advertising as ChessBoard");
}

// ---------------------------
// MAIN LOOP
// ---------------------------
void loop() {
  // Wait for a central to connect.
  BLEDevice central = BLE.central();
  if (central) {
    Serial.print("Connected to central: ");
    Serial.println(central.address());
    playExplosionAnimation();
    while (central.connected()) {
      // Process incoming BLE messages.
      if (chessCharacteristic.written()) {
        int len = chessCharacteristic.valueLength();
        char buffer[21];
        if (len > 20) len = 20;
        chessCharacteristic.readValue((byte*)buffer, len);
        buffer[len] = '\0';
        Serial.print("Received: ");
        Serial.println(buffer);
        processBLEMessage(String(buffer));
      }
      // Regularly scan sensors, check for changes, and update LEDs.
      scanSensors(currentSensor);
      checkForSensorChanges();
      updateLEDs();
      delay(50);
    }
    Serial.println("Central disconnected");
    BLE.advertise();  // Restart advertising after disconnect
  } else {
    // When not connected, still scan sensors and update display.
    scanSensors(currentSensor);
    checkForSensorChanges();
    updateLEDs();
    delay(50);
  }
}
