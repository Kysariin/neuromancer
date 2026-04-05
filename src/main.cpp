#include <Arduino.h>
#include <SPI.h>
#include <TFT_eSPI.h>  // display library for the TTGO T-Display's built-in screen

TFT_eSPI tft = TFT_eSPI();  // the display object — configured via build flags in platformio.ini

// capacitive touch threshold — reading drops below this when touched
// higher value = more sensitive (fires on lighter touches), giving us more dynamic range
const int THRESHOLD = 60;

// GPIO touch pins for each pad
// pad 0 → GPIO 27 (T7)
// pad 1 → GPIO 12 (T5)
// pad 2 → GPIO 13 (T4)
// pad 3 → GPIO 33 (T8)
// pad 4 → GPIO 2  (T2)  ← MODE SWITCH
const uint8_t PAD_PINS[5] = {T8, T5, T4, T7, T2};

// which mode we're in: 0 = drone (sustained), 1 = rhythm (percussive hits)
int currentMode = 0;
const int NUM_MODES = 2;

// --- attack envelope ---
// how long (ms) it takes for a newly-touched pad to ramp from silence to full intensity.
// raise this for a slower, more bowed/swelled attack; lower it for a snappier pluck.
const unsigned long ATTACK_MS = 500;

// --- per-pad state ---
bool          padActive[5]       = {false};  // is this pad currently being held?
unsigned long padTouchStart[5]   = {0};      // millis() when the touch began
float         padIntensity[5]    = {0.0f};   // touch depth locked in at first contact
unsigned long lastSerialSend[5]  = {0};      // rate-limit serial output per pad

// how often to push a serial update while a pad is held (~50 Hz)
const unsigned long SERIAL_INTERVAL_MS = 20;

// prevent mode-switch pad from toggling multiple times per tap
unsigned long lastModeSwitch = 0;
const unsigned long MODE_DEBOUNCE_MS = 600;

// how long each pad's box stays lit on the display after the last serial send
unsigned long padActiveUntil[5] = {0};
const unsigned long PAD_ACTIVE_MS = 200;

// TFT color constants in RGB565 format
#define CLR_BG      TFT_BLACK
#define CLR_GREEN   0x07E0
#define CLR_MAGENTA 0xF81F
#define CLR_YELLOW  0xFFE0
#define CLR_DARK    0x2104  // dim gray for inactive pads

// draws a single pad box on the TFT display
// active = true → colored and shows the current output value
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
    // show the enveloped output value (useful for seeing the ramp)
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

  if (currentMode == 0) {
    tft.setTextColor(CLR_GREEN);
    tft.setCursor(4, 4);
    tft.println("MODE 0: DRONE");
  } else {
    tft.setTextColor(CLR_MAGENTA);
    tft.setCursor(4, 4);
    tft.println("MODE 1: RHYTHM");
  }

  for (int i = 0; i < 5; i++) drawPadBox(i, 0, false);

  tft.setTextSize(1);
  tft.setTextColor(CLR_YELLOW);
  tft.setCursor(8 + 4 * (36 + 6) + 2, 24 + 50 + 4);
  tft.print("MODE");
}

// brief full-screen flash when switching modes
void flashModeSwitch() {
  uint16_t col = (currentMode == 0) ? CLR_GREEN : CLR_MAGENTA;
  tft.fillScreen(col);
  unsigned long t = millis();
  while (millis() - t < 60) {}
  tft.fillScreen(CLR_BG);
  t = millis();
  while (millis() - t < 30) {}
}

void setup() {
  Serial.begin(115200);
  delay(1000);
  Serial.println("NEUROMANCER TOUCH INSTRUMENT");

  tft.init();
  tft.setRotation(1);  // landscape mode (240×135)
  drawInterface();

  // no touch interrupts — we poll continuously so we can track hold duration
}

void loop() {
  unsigned long now = millis();

  for (int i = 0; i < 5; i++) {
    uint32_t raw     = touchRead(PAD_PINS[i]);
    bool     touching = (raw < THRESHOLD);

    // --- pad 4: mode switch ---
    if (i == 4) {
      if (touching && !padActive[4]) {
        padActive[4] = true;
        if (now - lastModeSwitch > MODE_DEBOUNCE_MS) {
          lastModeSwitch = now;
          currentMode = (currentMode + 1) % NUM_MODES;
          Serial.print("mode,");
          Serial.println(currentMode);
          flashModeSwitch();
          drawInterface();
        }
      } else if (!touching) {
        padActive[4] = false;
      }
      continue;
    }

    // --- pads 0-3: instrument pads with attack envelope ---

    if (touching && !padActive[i]) {
      // new touch — lock in intensity from the first reading and start the ramp
      padActive[i]     = true;
      padTouchStart[i] = now;
      float initIntensity = (float)(THRESHOLD - (int)raw) / (float)THRESHOLD;
      if (initIntensity < 0.0f) initIntensity = 0.0f;
      if (initIntensity > 1.0f) initIntensity = 1.0f;
      padIntensity[i] = initIntensity;
    } else if (!touching && padActive[i]) {
      // released — reset so next touch starts fresh from zero
      padActive[i] = false;
    }

    if (!padActive[i]) continue;

    // rate-limit how often we push serial updates (~50 Hz)
    if (now - lastSerialSend[i] < SERIAL_INTERVAL_MS) continue;
    lastSerialSend[i] = now;

    // --- compute the enveloped output value ---

    // attack ramp: 0.0 → 1.0 over ATTACK_MS milliseconds
    float elapsed = (float)(now - padTouchStart[i]);
    float ramp    = elapsed / (float)ATTACK_MS;
    if (ramp > 1.0f) ramp = 1.0f;

    // quadratic ease-in: slow start, accelerates into full intensity
    // change to ramp = ramp for linear, or ramp * ramp * ramp for cubic (slower start)
    ramp = ramp * ramp;

    // use the intensity locked in at first contact — not the live reading,
    // because the ESP32 touch sensor recalibrates during sustained holds and
    // the raw value drifts back toward the threshold, which would kill the level.
    uint8_t outVal = (uint8_t)(ramp * padIntensity[i] * 127.0f);

    Serial.print(i);
    Serial.print(",");
    Serial.println(outVal);

    padActiveUntil[i] = now + PAD_ACTIVE_MS;
    drawPadBox(i, outVal, true);
  }

  // dim any pad boxes whose display timer has expired
  now = millis();
  for (int i = 0; i < 4; i++) {
    if (padActiveUntil[i] && now >= padActiveUntil[i]) {
      padActiveUntil[i] = 0;
      drawPadBox(i, 0, false);
    }
  }
}
