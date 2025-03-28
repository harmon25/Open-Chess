#include <Adafruit_NeoPixel.h>
#include <math.h>

// ---------------------------
// NeoPixel Setup
// ---------------------------
#define LED_PIN     17       // Pin for NeoPixels
#define NUM_ROWS    8
#define NUM_COLS    8
#define LED_COUNT   (NUM_ROWS * NUM_COLS)
#define BRIGHTNESS  100

Adafruit_NeoPixel strip(LED_COUNT, LED_PIN, NEO_GRBW + NEO_KHZ800);

// ---------------------------
// Shift Register (74HC594) Pins
// ---------------------------
#define SER_PIN     2   // Serial data input (74HC594 pin 14)
#define SRCLK_PIN   3   // Shift register clock (pin 11)
#define RCLK_PIN    4   // Latch clock (pin 12)
// (Pin 13 (OE) must be tied HIGH and Pin 10 (SRCLR) tied HIGH if unused)

// ---------------------------
// Column Input Pins (D6..D13)
// ---------------------------
int colPins[NUM_COLS] = {6, 7, 8, 9, 10, 11, 12, 13};

// ---------------------------
// Row Patterns (LSB-first for shift register)
// ---------------------------
byte rowPatterns[8] = {
  0x01, // row 0
  0x02, // row 1
  0x04, // row 2
  0x08, // row 3
  0x10, // row 4
  0x20, // row 5
  0x40, // row 6
  0x80  // row 7
};

// ---------------------------
// Global Variables and Board Setup
// ---------------------------

// sensorState[row][col]: true if a magnet (piece) is detected at board square (row, col)
// Note: row 0 is the top row and row 7 the bottom row as read directly from sensors.
bool sensorState[8][8];
bool sensorPrev[8][8];

// Expected initial configuration (as printed in the grid)
char initialBoard[8][8] = {
  {'R', 'N', 'B', 'Q', 'K', 'B', 'N', 'R'},  // row 0 (rank 1)
  {'P', 'P', 'P', 'P', 'P', 'P', 'P', 'P'},  // row 1 (rank 2)
  {' ', ' ', ' ', ' ', ' ', ' ', ' ', ' '},    // row 2 (rank 3)
  {' ', ' ', ' ', ' ', ' ', ' ', ' ', ' '},    // row 3 (rank 4)
  {' ', ' ', ' ', ' ', ' ', ' ', ' ', ' '},    // row 4 (rank 5)
  {' ', ' ', ' ', ' ', ' ', ' ', ' ', ' '},    // row 5 (rank 6)
  {'p', 'p', 'p', 'p', 'p', 'p', 'p', 'p'},    // row 6 (rank 7)
  {'r', 'n', 'b', 'q', 'k', 'b', 'n', 'r'}     // row 7 (rank 8)
};

// Internal board state for gameplay (initialized from initialBoard)
char board[8][8];

// ---------------------------
// Function Prototypes
// ---------------------------
void loadShiftRegister(byte data);
void readSensors();
bool checkInitialBoard();
void updateSetupDisplay();
void printBoardState();
void fireworkAnimation();
void blinkSquare(int row, int col);
void pulsePossibleMoves(int moves[][2], int moveCount); // legacy pulse (not used during waiting)
void getPossibleMoves(int row, int col, int &moveCount, int moves[][2]);

// ---------------------------
// SETUP
// ---------------------------
void setup() {
  Serial.begin(9600);

  // Initialize NeoPixel strip
  strip.begin();
  strip.show(); // turn off all pixels
  strip.setBrightness(BRIGHTNESS);

  // Setup shift register control pins
  pinMode(SER_PIN,   OUTPUT);
  pinMode(SRCLK_PIN, OUTPUT);
  pinMode(RCLK_PIN,  OUTPUT);

  // Setup column input pins
  for (int c = 0; c < NUM_COLS; c++) {
    pinMode(colPins[c], INPUT);
  }

  // Initialize shift register to no row active
  loadShiftRegister(0x00);

  // Copy expected configuration into our board state
  for (int row = 0; row < 8; row++){
    for (int col = 0; col < 8; col++){
      board[row][col] = initialBoard[row][col];
    }
  }

  // Wait for board setup: repeatedly check sensors and update display until every expected piece is in place.
  Serial.println("Waiting for pieces to be placed...");
  while(!checkInitialBoard()){
    updateSetupDisplay();
    printBoardState();
    delay(500);
  }
  
  Serial.println("Ready to start");
  fireworkAnimation();

  // Initialize sensorPrev for move detection
  readSensors();
  for (int row = 0; row < 8; row++){
    for (int col = 0; col < 8; col++){
      sensorPrev[row][col] = sensorState[row][col];
    }
  }
}

// ---------------------------
// MAIN LOOP
// ---------------------------
// In the main loop, modify the piece movement logic
void loop() {
  readSensors();

  // Look for a piece pickup
  for (int row = 0; row < 8; row++) {
    for (int col = 0; col < 8; col++) {
      if (sensorPrev[row][col] && !sensorState[row][col]) {
        char piece = board[row][col];
        
        // Skip empty squares
        if (piece == ' ') continue;
        
        Serial.print("Piece lifted from ");
        Serial.print((char)('a' + col));
        Serial.println(row + 1);
        
        // Generate possible moves
        int moveCount = 0;
        int moves[20][2]; // up to 20 moves
        getPossibleMoves(row, col, moveCount, moves);
        
        // Light up current square and possible move squares
        int currentPixelIndex = col * NUM_COLS + (7 - row);
        strip.setPixelColor(currentPixelIndex, strip.Color(0, 0, 0, 100)); // Dimmer, but solid
        
        // Track potential captured pieces
        int capturedRow = -1;
        int capturedCol = -1;
        
        // Possible move squares with capture logic
        for (int i = 0; i < moveCount; i++) {
          int r = moves[i][0];
          int c = moves[i][1];
          int movePixelIndex = c * NUM_COLS + (7 - r);
          
          // Different highlighting for empty squares vs capture squares
          if (board[r][c] == ' ') {
            strip.setPixelColor(movePixelIndex, strip.Color(0, 0, 0, 50)); // Soft white for moves
          } else {
            strip.setPixelColor(movePixelIndex, strip.Color(255, 0, 0, 50)); // Red tint for captures
            // Store potential capture locations
            capturedRow = r;
            capturedCol = c;
          }
        }
        strip.show();
        
        // Wait for piece placement - new logic for capture moves
        int targetRow = -1, targetCol = -1;
        bool piecePlaced = false;
        bool captureMove = false;

        if (capturedRow != -1 && capturedCol != -1) {
          // Capture move: wait indefinitely for enemy piece removal and then for capturing piece placement.
          captureMove = true;
          Serial.println("Waiting for enemy piece removal (capture move)...");
          
          // Wait for the enemy piece to be removed (sensor goes LOW)
          while (sensorState[capturedRow][capturedCol]) {
            readSensors();
            delay(50);
          }
          
          Serial.println("Enemy piece removed, waiting for capturing piece placement...");
          
          // Wait for the capturing piece to be placed on that square (sensor goes HIGH)
          while (!sensorState[capturedRow][capturedCol]) {
            readSensors();
            delay(50);
          }
          
          targetRow = capturedRow;
          targetCol = capturedCol;
          piecePlaced = true;
        } else {
          // Normal move detection: wait indefinitely for any new piece placement.
          while (!piecePlaced) {
            readSensors();
            for (int r2 = 0; r2 < 8; r2++) {
              for (int c2 = 0; c2 < 8; c2++) {
                if (r2 == row && c2 == col) {
                  // Special case: the piece is placed back in its original square.
                  if (sensorState[r2][c2]) {
                    targetRow = r2;
                    targetCol = c2;
                    piecePlaced = true;
                    break;
                  }
                } else {
                  // For any other square, detect a new activation.
                  if (sensorState[r2][c2] && !sensorPrev[r2][c2]) {
                    targetRow = r2;
                    targetCol = c2;
                    piecePlaced = true;
                    break;
                  }
                }
              }
              if (piecePlaced) break;
            }
            delay(50);
          }
        }

        // Check if piece is replaced in the original spot
        if (targetRow == row && targetCol == col) {
          Serial.println("Piece replaced in original spot");
          // Blink once to confirm
          int currentPixelIndex = col * NUM_COLS + (7 - row);
          strip.setPixelColor(currentPixelIndex, strip.Color(0, 0, 0, 255));
          strip.show();
          delay(200);
          strip.setPixelColor(currentPixelIndex, strip.Color(0, 0, 0, 100));
          strip.show();
          
          // Clear all LED effects
          for (int i = 0; i < LED_COUNT; i++) {
            strip.setPixelColor(i, 0);
          }
          strip.show();
          
          continue; // Skip to next iteration
        }
        
        // If no piece placed within timeout, reset
        if (!piecePlaced) {
          Serial.println("No piece placement detected, resetting");
          
          // Clear LED effects
          for (int i = 0; i < LED_COUNT; i++) {
            strip.setPixelColor(i, 0);
          }
          strip.show();
          
          continue;
        }
        
        // Check if move is legal
        bool legalMove = false;
        for (int i = 0; i < moveCount; i++) {
          if (moves[i][0] == targetRow && moves[i][1] == targetCol) {
            legalMove = true;
            break;
          }
        }
        
        if (legalMove) {
          Serial.print("Legal move to ");
          Serial.print((char)('a' + targetCol));
          Serial.println(targetRow + 1);
          
          // Capture animation if a piece is being captured
          if (captureMove) {
            Serial.println("Performing capture animation");
            captureAnimation();
            
            // Remove the captured piece from the board
            board[capturedRow][capturedCol] = ' ';
            
            // Optional: Light up the captured square briefly
            int capturedPixelIndex = capturedCol * NUM_COLS + (7 - capturedRow);
            strip.setPixelColor(capturedPixelIndex, strip.Color(255, 0, 0, 100));
            strip.show();
            delay(500);
          }
          
          // Update board state
          board[targetRow][targetCol] = piece;
          board[row][col] = ' ';
          
          // Confirmation: Double blink destination square
          int newPixelIndex = targetCol * NUM_COLS + (7 - targetRow);
          for (int blink = 0; blink < 2; blink++) {
            strip.setPixelColor(newPixelIndex, strip.Color(0, 0, 0, 255));
            strip.show();
            delay(200);
            strip.setPixelColor(newPixelIndex, strip.Color(0, 0, 0, 50));
            strip.show();
            delay(200);
          }
        } else {
          Serial.println("Illegal move, reverting");
        }
        
        // Clear any remaining LED effects
        for (int i = 0; i < LED_COUNT; i++) {
          strip.setPixelColor(i, 0);
        }
        strip.show();
      }
    }
  }
  
  // Update previous sensor state
  for (int row = 0; row < 8; row++) {
    for (int col = 0; col < 8; col++) {
      sensorPrev[row][col] = sensorState[row][col];
    }
  }
  
  delay(100);
}
// ---------------------------
// FUNCTIONS
// ---------------------------

// loadShiftRegister: Shifts out 8 bits (LSB-first) to the 74HC594.
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

// readSensors: Activates each row and reads column sensors.
// Stores sensorState[row][col] directly (no inversion).
void readSensors(){
  for (int row = 0; row < 8; row++){
    loadShiftRegister(rowPatterns[row]);
    delayMicroseconds(100);
    for (int col = 0; col < NUM_COLS; col++){
      int sensorVal = digitalRead(colPins[col]);
      sensorState[row][col] = (sensorVal == LOW);
    }
  }
  loadShiftRegister(0x00);
}

// checkInitialBoard: Returns true only when every expected piece (non-space in initialBoard) is detected.
bool checkInitialBoard(){
  readSensors();
  bool allPresent = true;
  for (int row = 0; row < 8; row++){
    for (int col = 0; col < 8; col++){
      if(initialBoard[row][col] != ' ' && !sensorState[row][col]){
        allPresent = false;
      }
    }
  }
  return allPresent;
}

// updateSetupDisplay: Lights up each square (white) if a piece is detected.
void updateSetupDisplay(){
  for (int row = 0; row < 8; row++){
    for (int col = 0; col < 8; col++){
      int pixelIndex = col * NUM_COLS + (7 - row);
      if(initialBoard[row][col] != ' ' && sensorState[row][col]){
         strip.setPixelColor(pixelIndex, strip.Color(0, 0, 0, 255));
      } else {
         strip.setPixelColor(pixelIndex, 0);
      }
    }
  }
  strip.show();
}

// printBoardState: Prints the board grid to Serial; shows the expected piece if detected, or '-' if missing.
void printBoardState(){
  Serial.println("Current Board:");
  for (int row = 0; row < 8; row++){
    Serial.print("{ ");
    for (int col = 0; col < 8; col++){
      char displayChar = ' ';
      if(initialBoard[row][col] != ' '){
        displayChar = sensorState[row][col] ? initialBoard[row][col] : '-';
      }
      Serial.print("'");
      Serial.print(displayChar);
      Serial.print("'");
      if(col < 7) Serial.print(", ");
    }
    Serial.println(" },");
  }
  Serial.println();
}

// fireworkAnimation: A simple firework animation from the center out, contracting back, then out again.
void fireworkAnimation(){
  float centerX = 3.5;
  float centerY = 3.5;
  // Expansion phase:
  for (float radius = 0; radius < 6; radius += 0.5) {
    for (int row = 0; row < 8; row++){
      for (int col = 0; col < 8; col++){
        float dx = col - centerX;
        float dy = row - centerY;
        float dist = sqrt(dx * dx + dy * dy);
        int pixelIndex = col * NUM_COLS + (7 - row);
        if (fabs(dist - radius) < 0.5)
          strip.setPixelColor(pixelIndex, strip.Color(0, 0, 0, 255));
        else
          strip.setPixelColor(pixelIndex, 0);
      }
    }
    strip.show();
    delay(100);
  }
  // Contraction phase:
  for (float radius = 6; radius > 0; radius -= 0.5) {
    for (int row = 0; row < 8; row++){
      for (int col = 0; col < 8; col++){
        float dx = col - centerX;
        float dy = row - centerY;
        float dist = sqrt(dx * dx + dy * dy);
        int pixelIndex = col * NUM_COLS + (7 - row);
        if (fabs(dist - radius) < 0.5)
          strip.setPixelColor(pixelIndex, strip.Color(0, 0, 0, 255));
        else
          strip.setPixelColor(pixelIndex, 0);
      }
    }
    strip.show();
    delay(100);
  }
  // Second expansion phase:
  for (float radius = 0; radius < 6; radius += 0.5) {
    for (int row = 0; row < 8; row++){
      for (int col = 0; col < 8; col++){
        float dx = col - centerX;
        float dy = row - centerY;
        float dist = sqrt(dx * dx + dy * dy);
        int pixelIndex = col * NUM_COLS + (7 - row);
        if (fabs(dist - radius) < 0.5)
          strip.setPixelColor(pixelIndex, strip.Color(0, 0, 0, 255));
        else
          strip.setPixelColor(pixelIndex, 0);
      }
    }
    strip.show();
    delay(100);
  }
  // Clear all LEDs
  for (int i = 0; i < LED_COUNT; i++){
    strip.setPixelColor(i, 0);
  }
  strip.show();
}

// blinkSquare: Blinks the LED at the given square (row, col) three times.
void blinkSquare(int row, int col){
  int pixelIndex = col * NUM_COLS + (7 - row);
  for (int i = 0; i < 3; i++){
    strip.setPixelColor(pixelIndex, strip.Color(0, 0, 0, 255));
    strip.show();
    delay(200);
    strip.setPixelColor(pixelIndex, 0);
    strip.show();
    delay(200);
  }
}

void captureAnimation() {
  float centerX = 3.5;
  float centerY = 3.5;
  
  // Pulsing outward animation
  for (int pulse = 0; pulse < 3; pulse++) {
    for (int row = 0; row < 8; row++) {
      for (int col = 0; col < 8; col++) {
        float dx = col - centerX;
        float dy = row - centerY;
        float dist = sqrt(dx * dx + dy * dy);
        
        // Create a pulsing effect around the center
        float pulseWidth = 1.5 + pulse;
        int pixelIndex = col * NUM_COLS + (7 - row);
        
        if (dist >= pulseWidth - 0.5 && dist <= pulseWidth + 0.5) {
          // Alternate between red and orange for capture effect
          uint32_t color = (pulse % 2 == 0) 
            ? strip.Color(255, 0, 0, 0)   // Red
            : strip.Color(255, 165, 0, 0); // Orange
          strip.setPixelColor(pixelIndex, color);
        } else {
          strip.setPixelColor(pixelIndex, 0);
        }
      }
    }
    strip.show();
    delay(150);
  }
  
  // Clear LEDs
  for (int i = 0; i < LED_COUNT; i++) {
    strip.setPixelColor(i, 0);
  }
  strip.show();
}

// getPossibleMoves: A very simplified move generator for demonstration purposes.
// (This does not implement full chess rules.)
void getPossibleMoves(int row, int col, int &moveCount, int moves[][2]) {
  moveCount = 0;
  char piece = board[row][col];
  char pieceColor = (piece >= 'a' && piece <= 'z') ? 'b' : 'w';
  
  // Convert to uppercase for easier comparison
  piece = (piece >= 'a' && piece <= 'z') ? piece - 32 : piece;

  switch(piece) {
    case 'P': { // Pawn movement
      int direction = (pieceColor == 'w') ? 1 : -1;
      
      // One square forward
      if (row + direction >= 0 && row + direction < 8 && board[row + direction][col] == ' ') {
        moves[moveCount][0] = row + direction;
        moves[moveCount][1] = col;
        moveCount++;
        
        // Initial two-square move
        if ((pieceColor == 'w' && row == 1) || (pieceColor == 'b' && row == 6)) {
          if (board[row + 2*direction][col] == ' ') {
            moves[moveCount][0] = row + 2*direction;
            moves[moveCount][1] = col;
            moveCount++;
          }
        }
        
        // Diagonal captures (simplified)
        int captureColumns[] = {col-1, col+1};
        for (int i = 0; i < 2; i++) {
          if (captureColumns[i] >= 0 && captureColumns[i] < 8) {
            char targetPiece = board[row + direction][captureColumns[i]];
            if (targetPiece != ' ' && 
                ((pieceColor == 'w' && targetPiece >= 'a' && targetPiece <= 'z') || 
                 (pieceColor == 'b' && targetPiece >= 'A' && targetPiece <= 'Z'))) {
              moves[moveCount][0] = row + direction;
              moves[moveCount][1] = captureColumns[i];
              moveCount++;
            }
          }
        }
      }
      break;
    }
    
    case 'R': { // Rook movement
      int directions[4][2] = {{1,0}, {-1,0}, {0,1}, {0,-1}};
      for (int d = 0; d < 4; d++) {
        for (int step = 1; step < 8; step++) {
          int newRow = row + step * directions[d][0];
          int newCol = col + step * directions[d][1];
          
          if (newRow < 0 || newRow >= 8 || newCol < 0 || newCol >= 8) break;
          
          char targetPiece = board[newRow][newCol];
          if (targetPiece == ' ') {
            moves[moveCount][0] = newRow;
            moves[moveCount][1] = newCol;
            moveCount++;
          } else {
            // Check if it's a capturable piece
            if ((pieceColor == 'w' && targetPiece >= 'a' && targetPiece <= 'z') ||
                (pieceColor == 'b' && targetPiece >= 'A' && targetPiece <= 'Z')) {
              moves[moveCount][0] = newRow;
              moves[moveCount][1] = newCol;
              moveCount++;
            }
            break;
          }
        }
      }
      break;
    }
    
    case 'N': { // Knight movement
      int knightMoves[8][2] = {{2,1}, {1,2}, {-1,2}, {-2,1},
                                {-2,-1}, {-1,-2}, {1,-2}, {2,-1}};
      for (int i = 0; i < 8; i++) {
        int newRow = row + knightMoves[i][0];
        int newCol = col + knightMoves[i][1];
        
        if (newRow >= 0 && newRow < 8 && newCol >= 0 && newCol < 8) {
          char targetPiece = board[newRow][newCol];
          if (targetPiece == ' ' || 
              ((pieceColor == 'w' && targetPiece >= 'a' && targetPiece <= 'z') ||
               (pieceColor == 'b' && targetPiece >= 'A' && targetPiece <= 'Z'))) {
            moves[moveCount][0] = newRow;
            moves[moveCount][1] = newCol;
            moveCount++;
          }
        }
      }
      break;
    }
    
    case 'B': { // Bishop movement
      int directions[4][2] = {{1,1}, {1,-1}, {-1,1}, {-1,-1}};
      for (int d = 0; d < 4; d++) {
        for (int step = 1; step < 8; step++) {
          int newRow = row + step * directions[d][0];
          int newCol = col + step * directions[d][1];
          
          if (newRow < 0 || newRow >= 8 || newCol < 0 || newCol >= 8) break;
          
          char targetPiece = board[newRow][newCol];
          if (targetPiece == ' ') {
            moves[moveCount][0] = newRow;
            moves[moveCount][1] = newCol;
            moveCount++;
          } else {
            // Check if it's a capturable piece
            if ((pieceColor == 'w' && targetPiece >= 'a' && targetPiece <= 'z') ||
                (pieceColor == 'b' && targetPiece >= 'A' && targetPiece <= 'Z')) {
              moves[moveCount][0] = newRow;
              moves[moveCount][1] = newCol;
              moveCount++;
            }
            break;
          }
        }
      }
      break;
    }
    
    case 'Q': { // Queen movement (combination of rook and bishop)
      int directions[8][2] = {{1,0}, {-1,0}, {0,1}, {0,-1}, 
                               {1,1}, {1,-1}, {-1,1}, {-1,-1}};
      for (int d = 0; d < 8; d++) {
        for (int step = 1; step < 8; step++) {
          int newRow = row + step * directions[d][0];
          int newCol = col + step * directions[d][1];
          
          if (newRow < 0 || newRow >= 8 || newCol < 0 || newCol >= 8) break;
          
          char targetPiece = board[newRow][newCol];
          if (targetPiece == ' ') {
            moves[moveCount][0] = newRow;
            moves[moveCount][1] = newCol;
            moveCount++;
          } else {
            // Check if it's a capturable piece
            if ((pieceColor == 'w' && targetPiece >= 'a' && targetPiece <= 'z') ||
                (pieceColor == 'b' && targetPiece >= 'A' && targetPiece <= 'Z')) {
              moves[moveCount][0] = newRow;
              moves[moveCount][1] = newCol;
              moveCount++;
            }
            break;
          }
        }
      }
      break;
    }
    
    case 'K': { // King movement with simple range limitation
      int kingMoves[8][2] = {{1,0}, {-1,0}, {0,1}, {0,-1},
                              {1,1}, {1,-1}, {-1,1}, {-1,-1}};
      for (int i = 0; i < 8; i++) {
        int newRow = row + kingMoves[i][0];
        int newCol = col + kingMoves[i][1];
        
        if (newRow >= 0 && newRow < 8 && newCol >= 0 && newCol < 8) {
          char targetPiece = board[newRow][newCol];
          if (targetPiece == ' ' || 
              ((pieceColor == 'w' && targetPiece >= 'a' && targetPiece <= 'z') ||
               (pieceColor == 'b' && targetPiece >= 'A' && targetPiece <= 'Z'))) {
            moves[moveCount][0] = newRow;
            moves[moveCount][1] = newCol;
            moveCount++;
          }
        }
      }
      break;
    }
  }
}
