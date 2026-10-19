# caine_eyes

Animated eyes on a WS2812/NeoPixel matrix, driven by a Wemos D1 mini (ESP8266),
built with [PlatformIO](https://platformio.org/).

## What it does

Renders two eyes on the matrix with:
- Smooth pupil movement (saccades / look-around)
- Periodic blinking
- Occasional single-eye winks
- Idle recentering

Everything tunable lives in the `CONFIG` block at the top of
[`src/main.cpp`](src/main.cpp).

## Hardware

- Wemos D1 mini (ESP8266)
- WS2812B / NeoPixel matrix (default config assumes 16x8)
- External 5V supply sized for your pixel count (~60 mA per pixel at full
  white; the on-board regulator cannot power a full matrix)
- Common ground between the D1 mini and the matrix supply

### Wiring (defaults)

| Matrix | D1 mini |
|--------|---------|
| DIN    | D4 (GPIO2) — via a 300–470 ohm series resistor |
| 5V     | external 5V supply |
| GND    | shared GND with D1 mini |

Add a ~1000 uF capacitor across 5V/GND at the matrix to absorb inrush.

### Matrix layout

Set these in `src/main.cpp` to match your panel:
- `MATRIX_WIDTH` / `MATRIX_HEIGHT`
- `SERPENTINE` — `true` for zigzag wiring (most WS2812 panels), `false` for
  progressive
- `FLIP_VERTICAL` — flip if pixel 0 is top-left instead of bottom-left

## Toolchain

PlatformIO Core is installed in an isolated environment at
`~/.platformio/penv`. The `pio` command is on your PATH via `~/.zshrc`
(open a new terminal, or run `source ~/.zshrc`).

## Common commands

```bash
# Compile
pio run

# Compile + flash the board over USB
pio run -t upload

# Serial monitor (115200 baud)
pio device monitor

# List detected serial ports
pio device list

# Clean build artifacts
pio run -t clean
```
