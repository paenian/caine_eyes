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
#define MATRIX_WIDTH   34
#define MATRIX_HEIGHT  8
#define NUM_LEDS       (MATRIX_WIDTH * MATRIX_HEIGHT)

// Set to 1 to run the matrix bring-up test pattern instead of the eyes.
// Use this to verify dimensions, wiring order, and orientation on a new
// panel, then set back to 0 for the eyes. See runTestPattern() below.
#define TEST_PATTERN   0

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
#define BRIGHTNESS     32

// Caine's eyes: left iris green, right iris blue.
#define LEFT_EYE_COLOR   CRGB(0,   255, 60)    // left eye (green)
#define RIGHT_EYE_COLOR  CRGB(0,   80,  255)   // right eye (blue)
// Pupil color. For a dark pupil (Caine), use CRGB::Black — the pixels
// under the pupil are fully carved out so it reads as a true hole even
// against the glow. For a bright highlight pupil, use e.g. white.
#define PUPIL_COLOR      CRGB::Black           // pupil (dark, Caine-style)
#define BG_COLOR         CRGB(0,   0,   0)     // background

// Pupil radius in pixels (at normal eye size). Kept small so a ring of
// iris color always frames it.
#define PUPIL_RADIUS     1.0f

// Color bleed / fade: width in pixels of the OUTWARD glow halo only. The
// eye interior stays solid/crisp; this only softens the outside edge and
// spills color into surrounding pixels. 0 = hard outer edge (no bleed);
// larger = softer, dreamier. ~1.5-2.5 looks good on 8px.
#define BLEED_WIDTH      1.9f

// Pupil edge: 1 = antialiased (soft) pupil edge, 0 = hard crisp edge.
#define PUPIL_SOFT_EDGE  0

// Floatiness: how much the eyes continuously roll/wander around their
// orbits. 0 = still (only discrete darts); 1 = lively constant roaming.
// Controls both the orbit radius and the drift speed.
#define FLOATINESS       0.8f

// Average seconds between blinks. Higher = rarer blinks. The actual gap
// is randomized around this value (roughly 0.6x to 1.4x) so it looks
// natural. Blinks are timed independently of the other behaviors.
#define BLINK_INTERVAL_SEC   7.0f

// Eye size scaling. The eye radius is multiplied by a scale that the
// animation drives between SCALE_MIN and SCALE_MAX.
#define SCALE_MIN      0.80f
#define SCALE_MAX      0.90f

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
  float cx;       // eye center x on the full canvas (may be fractional)
  float cy;       // eye center y (may be fractional)
  float baseRx;   // base horizontal radius (before scale)
  float baseRy;   // base vertical radius (before scale)
};

// Shared pupil look direction (discrete gaze from the state machine).
float pupilX = 0.0f, pupilY = 0.0f;
float pupilTargetX = 0.0f, pupilTargetY = 0.0f;

// Continuous floaty drift added on top of the gaze, so the eyes roll
// around their orbits almost constantly. Updated each frame.
float driftX = 0.0f, driftY = 0.0f;

// Eyelid openness, 1.0 = open, 0.0 = closed. Per eye so we can wink.
float lidLeft = 1.0f, lidRight = 1.0f;

// Size scale per eye, eased toward a target.
float scaleLeft = 1.0f, scaleRight = 1.0f;
float scaleTargetLeft = 1.0f, scaleTargetRight = 1.0f;

Eye leftEye, rightEye;

void setupEyes() {
  // True geometric center of the panel. For an even height (e.g. 8 rows,
  // 0..7) this is a half-pixel value (3.5) so the eye spans every row
  // symmetrically instead of leaving the last row dark.
  float cx = (MATRIX_WIDTH  - 1) / 2.0f;
  float cy = (MATRIX_HEIGHT - 1) / 2.0f;
  float ry = (MATRIX_HEIGHT / 2.0f) - 0.5f;   // vertical radius fills height
  float rx = ry + 1.0f;                        // slightly wider than tall

  float quarter = MATRIX_WIDTH / 4.0f;
  leftEye  = { quarter,                          cy, rx, ry };
  rightEye = { (MATRIX_WIDTH - 1) - quarter,     cy, rx, ry };
}

// Draw one eye as a soft-edged ellipse that bleeds its color into the
// surrounding pixels, clipped by the eyelid, with a bright pupil.
//
// Coverage model: for each pixel we compute an ellipse "radius" value
//   r = sqrt((dx/rx)^2 + (dy/ry)^2)
// r < 1 is solidly inside the eye. Between 1 and (1 + feather) the
// coverage ramps from full down to zero, producing an antialiased edge
// and an outward glow halo (BLEED_WIDTH) that softens the pixelation.
void drawEye(const Eye& eye, float lid, float scale, const CRGB& color) {
  float rx = eye.baseRx * scale;
  float ry = eye.baseRy * scale;
  if (rx < 0.4f || ry < 0.4f) return;

  // Lid closes the eye vertically.
  float openRy = ry * lid;
  if (openRy < 0.25f) return;  // effectively shut

  // Feather amount expressed in normalized-radius units for each axis,
  // derived from the bleed width in pixels. A larger BLEED_WIDTH softens
  // the edge and spills more color into neighboring pixels.
  float featherX = BLEED_WIDTH / rx;
  float featherY = BLEED_WIDTH / ry;
  float feather  = (featherX + featherY) * 0.5f;
  if (feather < 0.0001f) feather = 0.0001f;  // avoid divide-by-zero

  // Iterate the integer pixels around the (possibly fractional) center.
  int ccx = (int)roundf(eye.cx);
  int ccy = (int)roundf(eye.cy);
  int spanX = (int)ceilf(rx + BLEED_WIDTH) + 1;
  int spanY = (int)ceilf(ry + BLEED_WIDTH) + 1;

  for (int py = ccy - spanY; py <= ccy + spanY; py++) {
    for (int px = ccx - spanX; px <= ccx + spanX; px++) {
      // Offset of this pixel from the true (fractional) eye center.
      float ddx = px - eye.cx;
      float ddy = py - eye.cy;

      float nx = ddx / rx;
      float ny = ddy / ry;
      float r = sqrtf(nx * nx + ny * ny);

      // Radial coverage: 1 inside, ramping to 0 across the feather band.
      float cov;
      if (r <= 1.0f)               cov = 1.0f;
      else if (r >= 1.0f + feather) cov = 0.0f;
      else                          cov = 1.0f - (r - 1.0f) / feather;

      if (cov <= 0.0f) continue;

      // Eyelid clip: hard vertical cut so the eye interior stays solid
      // (the bleed is an OUTWARD glow only, not an inward fade). During a
      // blink the whole eye shrinks vertically via openRy.
      if (fabs(ddy) > openRy) continue;

      addPixel(px, py, color, cov);
    }
  }

  // Pupil: a crisp dark disc carved into the iris, offset by the gaze
  // plus the continuous floaty drift and scaled with the eye. Only shown
  // when the eye is sufficiently open.
  if (lid > 0.4f) {
    // Gaze + floaty drift, both in eye-local pixels, scaled with the eye.
    float pcx = (pupilX + driftX) * scale;
    float pcy = (pupilY + driftY) * scale;
    float pr  = PUPIL_RADIUS * scale;    // grows/shrinks with the eye
    if (pr < 0.4f) pr = 0.4f;

    // Clamp the pupil so it stays visually inside the iris as it roams.
    float limX = (rx - pr) * 0.9f;
    float limY = (openRy - pr) * 0.9f;
    if (limX < 0) limX = 0;
    if (limY < 0) limY = 0;
    pcx = constrain(pcx, -limX, limX);
    pcy = constrain(pcy, -limY, limY);

    // Absolute (fractional) pupil center on the canvas.
    float cxAbs = eye.cx + pcx;
    float cyAbs = eye.cy + pcy;
    int pspan = (int)ceilf(pr + 1.0f);
    int baseX = (int)roundf(cxAbs);
    int baseY = (int)roundf(cyAbs);
    for (int py = baseY - pspan; py <= baseY + pspan; py++) {
      for (int px = baseX - pspan; px <= baseX + pspan; px++) {
        float ddx = px - cxAbs;
        float ddy = py - cyAbs;
        float d = sqrtf(ddx * ddx + ddy * ddy);

        // Coverage: hard edge by default, optional 1px antialias band.
        float cov;
#if PUPIL_SOFT_EDGE
        if (d <= pr - 0.5f)      cov = 1.0f;
        else if (d >= pr + 0.5f) cov = 0.0f;
        else                     cov = 1.0f - (d - (pr - 0.5f));
#else
        cov = (d <= pr) ? 1.0f : 0.0f;   // crisp disc
#endif
        if (cov <= 0.0f) continue;

        int sx = px;
        int sy = py;

        // Fully carve the iris color out under the pupil so it reads as a
        // true dark hole. Then add the pupil color on top: for a black
        // pupil this contributes nothing (the hole stays dark); for a
        // bright pupil color it fills the carved area as a highlight.
        darkenPixel(sx, sy, cov);
        addPixel(sx, sy, PUPIL_COLOR, cov);
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
unsigned long nextBlink  = 0;    // when the next blink should start
bool winkLeft = true;

float approach(float current, float target, float rate) {
  return current + (target - current) * rate;
}

// Random float in [lo, hi].
float frand(float lo, float hi) {
  return lo + (hi - lo) * (random(1001) / 1000.0f);
}

// Schedule the next blink around BLINK_INTERVAL_SEC, randomized 0.6x-1.4x
// so the rhythm looks natural rather than metronomic.
void scheduleNextBlink() {
  float jitter = frand(0.6f, 1.4f);
  nextBlink = millis() + (unsigned long)(BLINK_INTERVAL_SEC * 1000.0f * jitter);
}

void pickNewAction() {
  int roll = random(100);
  unsigned long now = millis();

  // Blinks are handled separately on their own timer (see the blink
  // scheduler in updateAnimation), so this roll only covers the ambient
  // look-around, resize, wink, and rest behaviors.
  if (roll < 55) {
    // Saccade: dart the gaze to a new point. Kept modest so it layers as
    // an accent on top of the continuous floaty drift rather than
    // fighting it. Held briefly so the eyes rarely sit still.
    pupilTargetX = frand(-1.6f, 1.6f);
    pupilTargetY = frand(-1.0f, 1.0f);
    state = LOOKING;
    stateUntil = now + random(300, 900);
  } else if (roll < 82) {
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
  } else if (roll < 90) {
    // Wink one eye.
    state = WINKING;
    stateUntil = now + 220;
  } else {
    // Brief rest. Gaze eases back toward center, but the floaty drift
    // keeps the eyes moving, so they never fully freeze.
    pupilTargetX = pupilTargetY = 0.0f;
    scaleTargetLeft = scaleTargetRight = 1.0f;
    state = IDLE;
    stateUntil = now + random(250, 700);
  }
  nextAction = stateUntil;
}

void updateAnimation() {
  unsigned long now = millis();

  // Continuous floaty roll: two slow sine waves per axis at mismatched
  // frequencies trace a wandering, non-repeating orbit so the eyes keep
  // moving almost all the time. FLOATINESS scales both the orbit size
  // and the speed. The horizontal orbit is wider than the vertical since
  // the eye is wider than it is tall.
  float t = now / 1000.0f;
  float spd = 0.6f + FLOATINESS * 1.4f;   // radians/sec base rate
  driftX = FLOATINESS * 2.2f *
           (0.6f * sinf(t * spd) + 0.4f * sinf(t * spd * 1.7f + 1.3f));
  driftY = FLOATINESS * 1.3f *
           (0.6f * sinf(t * spd * 0.8f + 2.1f) +
            0.4f * sinf(t * spd * 1.3f));

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

  // Dedicated blink timer: fires independently of the random behaviors so
  // blink frequency is controlled solely by BLINK_INTERVAL_SEC. Only
  // start a blink if we're not mid-blink/wink, to avoid stacking them.
  if (now >= nextBlink && state != BLINKING && state != WINKING) {
    state = BLINKING;
    stateUntil = now + 180;
    nextAction = stateUntil;   // resume normal behavior after the blink
    scheduleNextBlink();
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
  scheduleNextBlink();
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
