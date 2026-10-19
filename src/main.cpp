/*
 * Animated Eyes on a NeoPixel matrix — Wemos D1 mini (ESP8266)
 * ------------------------------------------------------------
 * Emulates the floating eyes of Caine from "The Amazing Digital Circus":
 * two mismatched-color eyes (left green, right blue) that blink, dart
 * around (saccades), wink, and periodically change size (squint / bulge).
 *
 * Wiring:
 *   - Matrix DIN  -> LED_PIN (see CONFIG) on the Wemos D1 mini
 *   - Matrix 5V   -> external 5V supply (NOT the D1 mini 3V3/5V pin for
 *                    anything beyond a few pixels — NeoPixels are hungry)
 *   - Matrix GND  -> common ground shared with the D1 mini GND
 *
 * A 300–470 ohm resistor in series with the data line and a large
 * (~1000 uF) capacitor across 5V/GND at the matrix are recommended.
 *
 * Tune the CONFIG block below to match your panel.
 */

#include <Arduino.h>
#include <FastLED.h>

// ======================= CONFIG =======================

// Matrix dimensions (in pixels).
#define MATRIX_WIDTH   32
#define MATRIX_HEIGHT  8
#define NUM_LEDS       (MATRIX_WIDTH * MATRIX_HEIGHT)

// Set to 1 to run the matrix bring-up test pattern instead of the eyes.
// Use this to verify dimensions, wiring order, and orientation on a new
// panel, then set back to 0 for the eyes. See runTestPattern() below.
#define TEST_PATTERN   1

// Data pin feeding the matrix.
#define LED_PIN        D7

// Panel wiring:
//   SERPENTINE = true  -> rows alternate direction (zigzag), common for
//                         WS2812 panels.
//   SERPENTINE = false -> every row runs the same direction (progressive).
#define SERPENTINE     true

// If the first pixel is at the top-left instead of bottom-left, flip this.
#define FLIP_VERTICAL  false

// Global brightness 0–255. Keep modest unless you have a beefy 5V supply.
#define BRIGHTNESS     40

// Caine's eyes: left iris green, right iris blue.
#define LEFT_EYE_COLOR   CRGB(0,   255, 60)    // left eye (green)
#define RIGHT_EYE_COLOR  CRGB(0,   80,  255)   // right eye (blue)
#define PUPIL_COLOR      CRGB(0,   0,   0)     // pupil (off)
#define BG_COLOR         CRGB(0,   0,   0)     // background

// Eye size scaling. The eye radius is multiplied by a scale that the
// animation drives between SCALE_MIN and SCALE_MAX.
#define SCALE_MIN      0.70f
#define SCALE_MAX      1.30f

// Idle "breathing": a slow, continuous size pulse so the eyes feel alive
// even when resting. BREATH_AMP is added to/subtracted from the scale;
// BREATH_PERIOD_MS is one full inhale+exhale cycle.
#define BREATH_AMP        0.12f
#define BREATH_PERIOD_MS  3200.0f

// ======================================================

CRGB leds[NUM_LEDS];

// Map an (x, y) coordinate to a strip index, honoring the wiring config.
uint16_t xyToIndex(uint8_t x, uint8_t y) {
  if (x >= MATRIX_WIDTH || y >= MATRIX_HEIGHT) {
    return NUM_LEDS;  // off-canvas: caller guards against this
  }

  uint8_t row = FLIP_VERTICAL ? (MATRIX_HEIGHT - 1 - y) : y;
  uint16_t index;

  if (SERPENTINE && (row & 0x01)) {
    // Odd rows run right-to-left.
    index = row * MATRIX_WIDTH + (MATRIX_WIDTH - 1 - x);
  } else {
    index = row * MATRIX_WIDTH + x;
  }
  return index;
}

void setPixel(int x, int y, const CRGB& color) {
  if (x < 0 || y < 0 || x >= MATRIX_WIDTH || y >= MATRIX_HEIGHT) return;
  uint16_t i = xyToIndex((uint8_t)x, (uint8_t)y);
  if (i < NUM_LEDS) leds[i] = color;
}

// Add a color at `amount` (0..1) intensity on top of what's already there.
// Uses saturating addition so overlapping glows blend smoothly without
// wrapping around.
void addPixel(int x, int y, const CRGB& color, float amount) {
  if (x < 0 || y < 0 || x >= MATRIX_WIDTH || y >= MATRIX_HEIGHT) return;
  if (amount <= 0.0f) return;
  if (amount > 1.0f) amount = 1.0f;
  uint16_t i = xyToIndex((uint8_t)x, (uint8_t)y);
  if (i >= NUM_LEDS) return;
  uint8_t a = (uint8_t)(amount * 255.0f);
  leds[i] += color.scale8(a);   // saturating add
}

// Fade a pixel toward black by (1 - amount). Used to carve the pupil in
// softly instead of a hard black block.
void darkenPixel(int x, int y, float amount) {
  if (x < 0 || y < 0 || x >= MATRIX_WIDTH || y >= MATRIX_HEIGHT) return;
  if (amount <= 0.0f) return;
  if (amount > 1.0f) amount = 1.0f;
  uint16_t i = xyToIndex((uint8_t)x, (uint8_t)y);
  if (i >= NUM_LEDS) return;
  uint8_t keep = (uint8_t)((1.0f - amount) * 255.0f);
  leds[i] = leds[i].scale8(keep);
}

// ---- Matrix bring-up test pattern ------------------------------------
//
// Fills the entire array so you can verify, on a freshly built panel:
//   - dimensions are correct (the pattern exactly fills MATRIX_WIDTH x
//     MATRIX_HEIGHT with no gaps or overruns)
//   - the serpentine wiring order (top row left-to-right, next row
//     right-to-left, alternating) matches reality
//   - orientation (top row is actually at the top)
//
// Within each row, brightness ramps from 10% on the first pixel to 50%
// on the last pixel. Rows alternate color so boundaries are obvious.
// The brightness ramp follows the physical data direction of each row,
// so on a correctly wired serpentine panel the bright ends line up on
// the same physical edge (a vertical seam), confirming the zigzag.
//
// This walks the strip by raw LED index (independent of the eye
// coordinate mapping) to show exactly how data snakes through the panel.
void runTestPattern() {
  // Row color palette (cycled per row).
  static const CRGB rowColors[] = {
    CRGB(255, 0,   0),     // red
    CRGB(0,   255, 0),     // green
    CRGB(0,   0,   255),   // blue
    CRGB(255, 255, 0),     // yellow
  };
  const int numColors = sizeof(rowColors) / sizeof(rowColors[0]);

  FastLED.clear();

  for (int row = 0; row < MATRIX_HEIGHT; row++) {
    CRGB base = rowColors[row % numColors];

    for (int col = 0; col < MATRIX_WIDTH; col++) {
      // Fraction along the row, 0 at the first pixel .. 1 at the last.
      float f = (MATRIX_WIDTH > 1)
                  ? (float)col / (float)(MATRIX_WIDTH - 1)
                  : 0.0f;
      // Brightness 10% -> 50% across the row.
      float level = 0.10f + 0.40f * f;
      uint8_t scale = (uint8_t)(level * 255.0f);

      // Physical strip index: serpentine (odd rows reversed), honoring
      // FLIP_VERTICAL so row 0 is the physical top row.
      int phyRow = FLIP_VERTICAL ? (MATRIX_HEIGHT - 1 - row) : row;
      int idx;
      if (SERPENTINE && (phyRow & 0x01)) {
        idx = phyRow * MATRIX_WIDTH + (MATRIX_WIDTH - 1 - col);
      } else {
        idx = phyRow * MATRIX_WIDTH + col;
      }

      if (idx >= 0 && idx < NUM_LEDS) {
        leds[idx] = base;
        leds[idx].nscale8(scale);
      }
    }
  }

  FastLED.show();
}

// ---- Eye geometry ----------------------------------------------------
//
// The canvas is split into a left and a right half, one eye each. Each eye
// is drawn as a filled ellipse whose radii are scaled per-eye; a small
// pupil (1–2 px) rides inside it, offset by the shared look direction.

struct Eye {
  int   cx;       // eye center x on the full canvas
  int   cy;       // eye center y
  float baseRx;   // base horizontal radius (before scale)
  float baseRy;   // base vertical radius (before scale)
};

// Shared pupil look direction.
float pupilX = 0.0f, pupilY = 0.0f;
float pupilTargetX = 0.0f, pupilTargetY = 0.0f;

// Eyelid openness, 1.0 = open, 0.0 = closed. Per eye so we can wink.
float lidLeft = 1.0f, lidRight = 1.0f;

// Size scale per eye, eased toward a target.
float scaleLeft = 1.0f, scaleRight = 1.0f;
float scaleTargetLeft = 1.0f, scaleTargetRight = 1.0f;

Eye leftEye, rightEye;

void setupEyes() {
  int quarter = MATRIX_WIDTH / 4;
  int cy = (MATRIX_HEIGHT - 1) / 2;
  float ry = (MATRIX_HEIGHT / 2.0f) - 0.5f;   // vertical radius fills height
  float rx = ry + 1.0f;                        // slightly wider than tall
  leftEye  = { quarter,                    cy, rx, ry };
  rightEye = { MATRIX_WIDTH - 1 - quarter, cy, rx, ry };
}

// Width (in pixels) of the soft edge + outward glow. Larger = softer and
// more color bleed into the surrounding pixels.
#define GLOW_WIDTH   1.6f

// Draw one eye as a soft-edged ellipse that bleeds its color into the
// surrounding pixels, clipped by the eyelid, with a soft pupil.
//
// Coverage model: for each pixel we compute an ellipse "radius" value
//   r = sqrt((dx/rx)^2 + (dy/ry)^2)
// r < 1 is solidly inside the eye. Between 1 and (1 + feather) the
// coverage ramps from full down to zero, producing an antialiased edge
// and an outward glow halo that softens the pixelation.
void drawEye(const Eye& eye, float lid, float scale, const CRGB& color) {
  float rx = eye.baseRx * scale;
  float ry = eye.baseRy * scale;
  if (rx < 0.4f || ry < 0.4f) return;

  // Lid closes the eye vertically.
  float openRy = ry * lid;
  if (openRy < 0.25f) return;  // effectively shut

  // Feather amount expressed in normalized-radius units for each axis,
  // derived from the glow width in pixels.
  float featherX = GLOW_WIDTH / rx;
  float featherY = GLOW_WIDTH / ry;
  float feather  = (featherX + featherY) * 0.5f;

  int spanX = (int)ceilf(rx + GLOW_WIDTH);
  int spanY = (int)ceilf(ry + GLOW_WIDTH);

  for (int dy = -spanY; dy <= spanY; dy++) {
    for (int dx = -spanX; dx <= spanX; dx++) {
      float nx = dx / rx;
      float ny = dy / ry;
      float r = sqrtf(nx * nx + ny * ny);

      // Radial coverage: 1 inside, ramping to 0 across the feather band.
      float cov;
      if (r <= 1.0f)               cov = 1.0f;
      else if (r >= 1.0f + feather) cov = 0.0f;
      else                          cov = 1.0f - (r - 1.0f) / feather;

      if (cov <= 0.0f) continue;

      // Soft eyelid clip: fade out vertically near the lid boundary
      // instead of a hard cut.
      float ay = fabs((float)dy);
      float lidCov;
      if (ay <= openRy)            lidCov = 1.0f;
      else if (ay >= openRy + 1.0f) lidCov = 0.0f;
      else                          lidCov = 1.0f - (ay - openRy);

      float amount = cov * lidCov;
      if (amount > 0.0f) {
        addPixel(eye.cx + dx, eye.cy + dy, color, amount);
      }
    }
  }

  // Pupil: a soft dark spot that fades at its edge, offset by the look
  // direction and scaled with the eye. Only when the eye is open enough.
  if (lid > 0.4f) {
    float pcx = pupilX * scale;   // pupil center relative to eye center
    float pcy = pupilY * scale;
    float pr  = (scale > 1.05f) ? 1.4f : 1.0f;  // pupil radius in pixels

    int pspan = (int)ceilf(pr + 1.0f);
    int basex = (int)roundf(pcx);
    int basey = (int)roundf(pcy);
    for (int dy = -pspan; dy <= pspan; dy++) {
      for (int dx = -pspan; dx <= pspan; dx++) {
        // Pixel offset from the eye center.
        int ox = basex + dx;
        int oy = basey + dy;
        float ddx = ox - pcx;
        float ddy = oy - pcy;
        float d = sqrtf(ddx * ddx + ddy * ddy);
        float dark;
        if (d <= pr - 0.5f)      dark = 1.0f;
        else if (d >= pr + 0.5f) dark = 0.0f;
        else                     dark = 1.0f - (d - (pr - 0.5f));
        if (dark > 0.0f) {
          darkenPixel(eye.cx + ox, eye.cy + oy, dark);
        }
      }
    }
  }
}

void renderFrame() {
  fill_solid(leds, NUM_LEDS, BG_COLOR);

  // Slow breathing pulse applied on top of the state-machine scale. The
  // two eyes breathe slightly out of phase so it never looks mechanical.
  float phase = (millis() / BREATH_PERIOD_MS) * TWO_PI;
  float breathL = sinf(phase)              * BREATH_AMP;
  float breathR = sinf(phase + 0.6f)       * BREATH_AMP;

  drawEye(leftEye,  lidLeft,  scaleLeft  + breathL, LEFT_EYE_COLOR);
  drawEye(rightEye, lidRight, scaleRight + breathR, RIGHT_EYE_COLOR);
  FastLED.show();
}

// ---- Animation state machine ----------------------------------------

enum EyeState { IDLE, BLINKING, WINKING, LOOKING, RESIZING };
EyeState state = IDLE;

unsigned long stateUntil = 0;
unsigned long nextAction = 0;
bool winkLeft = true;

float approach(float current, float target, float rate) {
  return current + (target - current) * rate;
}

// Random float in [lo, hi].
float frand(float lo, float hi) {
  return lo + (hi - lo) * (random(1001) / 1000.0f);
}

void pickNewAction() {
  int roll = random(100);
  unsigned long now = millis();

  if (roll < 35) {
    // Saccade: dart the gaze to a new point, then hold.
    pupilTargetX = frand(-2.5f, 2.5f);
    pupilTargetY = frand(-1.5f, 1.5f);
    state = LOOKING;
    stateUntil = now + random(500, 1800);
  } else if (roll < 60) {
    // Change size: squint, normal, or bulge. Sometimes both eyes match,
    // sometimes they differ for that off-kilter Caine look.
    if (random(100) < 70) {
      float s = frand(SCALE_MIN, SCALE_MAX);
      scaleTargetLeft = scaleTargetRight = s;
    } else {
      scaleTargetLeft  = frand(SCALE_MIN, SCALE_MAX);
      scaleTargetRight = frand(SCALE_MIN, SCALE_MAX);
    }
    state = RESIZING;
    stateUntil = now + random(500, 1400);
  } else if (roll < 82) {
    // Blink both eyes.
    state = BLINKING;
    stateUntil = now + 180;
  } else if (roll < 92) {
    // Wink one eye.
    state = WINKING;
    stateUntil = now + 220;
  } else {
    // Recenter gaze and return to a neutral size.
    pupilTargetX = pupilTargetY = 0.0f;
    scaleTargetLeft = scaleTargetRight = 1.0f;
    state = IDLE;
    stateUntil = now + random(400, 1200);
  }
  nextAction = stateUntil;
}

void updateAnimation() {
  unsigned long now = millis();

  // Pupils and sizes always ease toward their targets for smooth motion.
  pupilX = approach(pupilX, pupilTargetX, 0.35f);
  pupilY = approach(pupilY, pupilTargetY, 0.35f);
  scaleLeft  = approach(scaleLeft,  scaleTargetLeft,  0.25f);
  scaleRight = approach(scaleRight, scaleTargetRight, 0.25f);

  switch (state) {
    case BLINKING: {
      float t = 1.0f - (float)(stateUntil - now) / 180.0f;   // 0..1
      float lid = (t < 0.5f) ? (1.0f - t * 2.0f) : ((t - 0.5f) * 2.0f);
      lidLeft = lidRight = constrain(lid, 0.0f, 1.0f);
      break;
    }
    case WINKING: {
      float t = 1.0f - (float)(stateUntil - now) / 220.0f;
      float lid = (t < 0.5f) ? (1.0f - t * 2.0f) : ((t - 0.5f) * 2.0f);
      lid = constrain(lid, 0.0f, 1.0f);
      if (winkLeft) { lidLeft = lid;  lidRight = 1.0f; }
      else          { lidRight = lid; lidLeft  = 1.0f; }
      break;
    }
    case LOOKING:
    case RESIZING:
    case IDLE:
    default:
      // Keep eyes open during these behaviors.
      lidLeft  = approach(lidLeft,  1.0f, 0.4f);
      lidRight = approach(lidRight, 1.0f, 0.4f);
      break;
  }

  if (now >= nextAction) {
    if (state == WINKING) winkLeft = !winkLeft;  // alternate wink eye
    pickNewAction();
  }
}

void setup() {
  Serial.begin(115200);
  delay(50);
  Serial.println();
  Serial.println(TEST_PATTERN ? F("Matrix test pattern...")
                              : F("Caine eyes starting..."));

  FastLED.addLeds<WS2812B, LED_PIN, GRB>(leds, NUM_LEDS);
  FastLED.setBrightness(BRIGHTNESS);
  FastLED.clear(true);

  randomSeed(analogRead(A0));

  setupEyes();
  pickNewAction();
}

void loop() {
#if TEST_PATTERN
  // Static bring-up pattern. Drawn once per second is plenty; it does not
  // change, so there is nothing to animate.
  runTestPattern();
  delay(1000);
#else
  updateAnimation();
  renderFrame();
  delay(33);  // ~30 FPS
#endif
}
