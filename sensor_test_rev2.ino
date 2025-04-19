#include <Adafruit_NeoPixel.h>

// ---------------------------
// NeoPixel Setup
// ---------------------------
#define LED_PIN     17     // Pin for NeoPixels
#define NUM_ROWS    8
#define NUM_COLS    8
#define LED_COUNT   (NUM_ROWS * NUM_COLS)
#define BRIGHTNESS  100

Adafruit_NeoPixel strip(LED_COUNT, LED_PIN, NEO_GRB + NEO_KHZ800);

// ---------------------------
// Shift Register (74HC594) Pins
// ---------------------------
#define SER_PIN     2   // Serial data input  (74HC594 pin 14)
#define SRCLK_PIN   3   // Shift register clock (pin 11)
#define RCLK_PIN    4   // Latch clock (pin 12)
// Pin 13 (OE) on 74HC594 must be tied HIGH (active-high).
// Pin 10 (SRCLR) on 74HC594 must be tied HIGH if not used for clearing.

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
// Button State Tracking
// ---------------------------
bool buttonState[NUM_ROWS][NUM_COLS] = {0};
bool prevButtonState[NUM_ROWS][NUM_COLS] = {0};
bool matrixChanged = false;

// ---------------------------------------------------------------------
// SETUP
// ---------------------------------------------------------------------
void setup() {
  Serial.begin(9600);

  // NeoPixel init
  strip.begin();
  strip.show(); // Initialize all pixels to 'off'
  strip.setBrightness(BRIGHTNESS);

  // Shift register control pins
  pinMode(SER_PIN,   OUTPUT);
  pinMode(SRCLK_PIN, OUTPUT);
  pinMode(RCLK_PIN,  OUTPUT);

  // Column pins as inputs
  for (int c = 0; c < NUM_COLS; c++) {
    pinMode(colPins[c], INPUT_PULLUP);
  }

  // Initialize shift register to all LOW (no line active)
  loadShiftRegister(0x00);
  
  // Initialize the LED matrix once
  updateLEDs();
}

// ---------------------------------------------------------------------
// LOOP
// ---------------------------------------------------------------------
void loop() {
  matrixChanged = false;

  // Scan each row
  for (int row = 0; row < NUM_ROWS; row++) {
    // Enable this row via the shift register
    loadShiftRegister(rowPatterns[row]);
    
    // Small delay to let signals settle
    delayMicroseconds(500);

    // Read all columns
    for (int col = 0; col < NUM_COLS; col++) {
      int sensorVal = digitalRead(colPins[col]);
      
      // Update button state
      buttonState[row][col] = (sensorVal == LOW);
      
      // Check if state changed
      if (buttonState[row][col] != prevButtonState[row][col]) {
        matrixChanged = true;
        prevButtonState[row][col] = buttonState[row][col];
      }
    }
  }

  // Turn off the row lines
  loadShiftRegister(0x00);

  // Only update LEDs if there was a change
  if (matrixChanged) {
    updateLEDs();
  }

  // Small pause
  delay(20);
}

// ---------------------------------------------------------------------
// Update LED Display
// ---------------------------------------------------------------------
void updateLEDs() {
  // Clear all LEDs
  for(int i = 0; i < LED_COUNT; i++) {
    strip.setPixelColor(i, 0); // Off
  }

  // Set LEDs based on button states
  for (int row = 0; row < NUM_ROWS; row++) {
    for (int col = 0; col < NUM_COLS; col++) {
      if (buttonState[row][col]) {
        // Map row, col to the correct NeoPixel index
        int pixelIndex = col * NUM_COLS + (7-row);
        strip.setPixelColor(pixelIndex, strip.Color(255, 255, 255));
      }
    }
  }

  // Update NeoPixels
  strip.show();
}

// ---------------------------------------------------------------------
// SHIFT OUT  (LSB-first to match rowPatterns[])
// ---------------------------------------------------------------------
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
