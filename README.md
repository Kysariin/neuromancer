# NEUROMANCER // TOUCH INSTRUMENT

A capacitive touch instrument built on the ESP32 TTGO T-Display. Five copper-tape pads connect to the ESP32's touch GPIO pins and stream data over USB to a browser page that reads it via the Web Serial API and uses the Web Audio API and Canvas 2D to generate sound and visuals in real time.

The name and aesthetic come from William Gibson's *Neuromancer*. Mode 0 is the matrix -- Gibson described cyberspace as a colorless void of cascading silver-white light, not green, so the visuals are silver data rain and spinning wireframe structures. Mode 1 is the ICE: neon geometric shapes that explode outward on every hit.

[portfolio entry here](https://kysariin.github.io/project/systems/sprawl)

---

## Hardware

- TTGO T-Display (ESP32 with built-in 240×135 TFT)
- 4 instrument pads + 1 mode-switch pad (copper tape)
- Wires from each pad to the GPIO pins below
- Breadboard for organizing connections
- USB-C cable (must be data-capable, not charge-only)
- "Vessel" for holding the ESP32 / copper tape buttons

### Pin Mapping

| Pad | Function     | Touch Channel | GPIO |
|-----|-------------|---------------|------|
| 0   | Instrument  | T8            | 33   |
| 1   | Instrument  | T5            | 12   |
| 2   | Instrument  | T4            | 13   |
| 3   | Instrument  | T7            | 27   |
| 4   | Mode Switch | T2            | 2    |

---

## Software

- PlatformIO to build and flash the firmware
- Chrome or Microsoft Edge to run the browser interface
- `index.html` opens directly from the filesystem, no server needed

---

## Setup

1. Clone the repo
2. Open the folder in VSCode with PlatformIO installed
3. Plug in the TTGO via USB and click Upload
4. Once uploaded, the TFT shows `MODE 0: DRONE` with five pad boxes
5. Open `index.html` in Chrome or Edge
6. Click **[ CONNECT DEVICE ]**, select the ESP32's COM port — audio initializes on the same click

---

## Playing It

**Pads 0–3** are the instrument. Touch and hold — each pad starts silent and ramps up over ~500ms rather than snapping to full intensity immediately. Let go to stop.

**Pad 4** (rightmost) switches modes. The display and browser both flash to confirm.

| Mode | Audio | Visuals |
|------|-------|---------|
| **0 — DRONE** | Sustained oscillators tuned to A minor pentatonic. Multiple pads build a chord. The filter opens the longer you hold. | Silver data rain, spinning polygons, scan beams, a grid |
| **1 — RHYTHM** | Distorted percussive hit on contact. Hold past 350ms and a sustained bandpass tone fades in. | Neon ICE geometry, expanding polygon ripples, flying shards, oscilloscope |

### Keyboard Fallback (no hardware needed)

| Key | Action |
|-----|--------|
| A S D F | Pads 0–3 |
| G | Mode switch |

---

## File Structure

```
neuromancer/
├── src/
│   └── main.cpp        # ESP32 firmware
├── index.html          # Browser interface (Web Serial + Web Audio + Canvas 2D)
├── platformio.ini      # Board config and TFT_eSPI build flags
└── README.md
```
