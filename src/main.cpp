#include <Arduino.h>
#include <SPI.h>
#include <TFT_eSPI.h>  // display library for the TTGO T-Display's built-in screen

TFT_eSPI tft = TFT_eSPI();  // the display object — configured via build flags in platformio.ini

// capacitive touch threshold — interrupt fires when the raw reading drops below this
// higher value = more sensitive (fires on lighter touches), giving us more dynamic range
const int THRESHOLD = 60;

// these are set inside the ISRs (interrupt service routines) and read in the main loop
// volatile tells the compiler "don't cache these — they can change at any time"
volatile bool    touchDetected[5] = {false, false, false, false, false};
volatile uint8_t touchVal[5]      = {0, 0, 0, 0, 0};

// which mode we're in: 0 = drone (sustained), 1 = rhythm (percussive hits)
int currentMode = 0;
const int NUM_MODES = 2;

// ISR = interrupt service routine — these fire instantly when a pad is touched,
// bypassing the main loop entirely. IRAM_ATTR keeps them in fast RAM so they
// don't cause a cache miss when the flash is busy.
// safe GPIO pins on TTGO T-Display (avoids TFT pins 4,5,16,18,19,23 and boot pins 0,35)
// pad 0 → GPIO 2  (T2)
// pad 1 → GPIO 12 (T5)
// pad 2 → GPIO 13 (T4)
// pad 3 → GPIO 33 (T8)  ← was T6/GPIO14 which is a JTAG pin and unreliable for touch
// pad 4 → GPIO 27 (T7)  ← MODE SWITCH
void IRAM_ATTR gotTouch0() { touchDetected[0] = true; touchVal[0] = touchRead(T2); }
void IRAM_ATTR gotTouch1() { touchDetected[1] = true; touchVal[1] = touchRead(T5); }
void IRAM_ATTR gotTouch2() { touchDetected[2] = true; touchVal[2] = touchRead(T4); }
void IRAM_ATTR gotTouch3() { touchDetected[3] = true; touchVal[3] = touchRead(T8); }
void IRAM_ATTR gotTouch4() { touchDetected[4] = true; touchVal[4] = touchRead(T7); }

// prevent pad 4 (mode switch) from switching multiple times per tap
unsigned long lastModeSwitch = 0;
const unsigned long MODE_DEBOUNCE_MS = 600;

// how long each pad's box stays lit up on the display after a touch
unsigned long padActiveUntil[5] = {0, 0, 0, 0, 0};
const unsigned long PAD_ACTIVE_MS = 200;

// TFT color constants in RGB565 format (what the display uses)
#define CLR_BG      TFT_BLACK
#define CLR_GREEN   0x07E0
#define CLR_MAGENTA 0xF81F
#define CLR_YELLOW  0xFFE0
#define CLR_DARK    0x2104  // dim gray for inactive pads

// draws a single pad box on the TFT display
// active = true → colored and shows the raw touch value
// active = false → dark/dim idle state
void drawPadBox(int i, uint8_t val, bool active) {
  int boxW = 36, boxH = 50;
  int x = 8 + i * (boxW + 6);  // evenly space 5 boxes across the 240px wide screen
  int y = 24;

  uint16_t col = active
    ? (currentMode == 0 ? CLR_GREEN : CLR_MAGENTA)  // green for drone, magenta for rhythm
    : CLR_DARK;

  tft.fillRect(x, y, boxW, boxH, col);
  tft.setTextSize(2);
  tft.setTextColor(active ? CLR_BG : (uint16_t)0x6B4D);
  tft.setCursor(x + 10, y + 16);
  tft.print(i);  // show pad number

  if (active) {
    // show the raw capacitive reading in the corner (useful for debugging sensitivity)
    tft.setTextSize(1);
    tft.setTextColor(CLR_BG);
    tft.setCursor(x + 2, y + boxH - 12);
    tft.print(val);
  }
}

// redraws the whole screen — called on startup and after every mode switch
void drawInterface() {
  tft.fillScreen(CLR_BG);
  tft.setTextSize(1);

  // mode label at top left
  if (currentMode == 0) {
    tft.setTextColor(CLR_GREEN);
    tft.setCursor(4, 4);
    tft.println("MODE 0: DRONE");
  } else {
    tft.setTextColor(CLR_MAGENTA);
    tft.setCursor(4, 4);
    tft.println("MODE 1: RHYTHM");
  }

  // draw all 5 pad boxes in their idle/dark state
  for (int i = 0; i < 5; i++) drawPadBox(i, 0, false);

  // label under the mode-switch pad (pad 4, rightmost)
  tft.setTextSize(1);
  tft.setTextColor(CLR_YELLOW);
  tft.setCursor(8 + 4 * (36 + 6) + 2, 24 + 50 + 4);
  tft.print("MODE");
}

// brief full-screen flash when switching modes — like a TV channel changing
// uses millis() instead of delay() so touch ISRs aren't blocked during the flash
void flashModeSwitch() {
  uint16_t col = (currentMode == 0) ? CLR_GREEN : CLR_MAGENTA;
  tft.fillScreen(col);
  unsigned long t = millis();
  while (millis() - t < 60) {}   // hold flash for 60ms
  tft.fillScreen(CLR_BG);
  t = millis();
  while (millis() - t < 30) {}   // brief black gap before redraw
}

void setup() {
  Serial.begin(115200);  // must match the baudRate in the WebSerial connect call
  delay(1000);           // give the serial monitor time to connect before printing
  Serial.println("NEUROMANCER TOUCH INSTRUMENT");

  // hook up each touch pin to its ISR — fires whenever raw reading drops below THRESHOLD
  touchAttachInterrupt(T2, gotTouch0, THRESHOLD);
  touchAttachInterrupt(T5, gotTouch1, THRESHOLD);
  touchAttachInterrupt(T4, gotTouch2, THRESHOLD);
  touchAttachInterrupt(T8, gotTouch3, THRESHOLD);
  touchAttachInterrupt(T7, gotTouch4, THRESHOLD);

  tft.init();
  tft.setRotation(1);  // landscape mode (240×135)
  drawInterface();
}

void loop() {
  // check each pad's flag — set by ISR, cleared here once we've handled it
  for (int i = 0; i < 5; i++) {
    if (!touchDetected[i]) continue;

    touchDetected[i] = false;  // clear flag before processing so we don't miss the next touch
    uint8_t val = touchVal[i];

    // pad 4 is the mode switch — handled separately, doesn't send pad data
    if (i == 4) {
      unsigned long now = millis();
      if (now - lastModeSwitch > MODE_DEBOUNCE_MS) {  // ignore rapid re-triggers
        lastModeSwitch = now;
        currentMode = (currentMode + 1) % NUM_MODES;
        Serial.print("mode,");
        Serial.println(currentMode);  // tells the browser to switch modes
        flashModeSwitch();
        drawInterface();
      }
      continue;  // skip the pad data output below
    }

    // send the pad event to the browser: "padIndex,rawValue\n"
    // browser's parseLine() picks this up and routes it to the audio/visual engine
    Serial.print(i);
    Serial.print(",");
    Serial.println(val);

    padActiveUntil[i] = millis() + PAD_ACTIVE_MS;  // keep box lit for 200ms
    drawPadBox(i, val, true);
  }

  // check if any active pad's display timer has expired and dim it back down
  unsigned long now = millis();
  for (int i = 0; i < 4; i++) {  // only pads 0-3 (pad 4 = mode switch, never lights up)
    if (padActiveUntil[i] && now >= padActiveUntil[i]) {
      padActiveUntil[i] = 0;
      drawPadBox(i, 0, false);
    }
  }
}
