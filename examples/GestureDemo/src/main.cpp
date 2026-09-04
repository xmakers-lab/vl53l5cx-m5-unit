// M5Stack Basic + VL53L5CX (I2C Port A) hand swipe gesture recognition demo
#include <M5Unified.h>
#include <Wire.h>
#include <SparkFun_VL53L5CX_Library.h>

static const int I2C_SDA = 21;  // M5Stack Basic Port A
static const int I2C_SCL = 22;  // M5Stack Basic Port A
static const int GRID_SIZE = 8; // 8x8 = 64 zones

// Zones within this distance range are treated as a hand passing over the sensor.
static const int16_t HAND_MIN_MM = 30;
static const int16_t HAND_MAX_MM = 400;
static const int MIN_VALID_ZONES = 6; // zones needed to consider a hand present

// A tracked hand must move at least this far (in grid cells) within this
// duration window to be reported as a swipe.
static const float MIN_SWIPE_DISTANCE_CELLS = 2.5f;
static const uint32_t MIN_SWIPE_DURATION_MS = 80;
static const uint32_t MAX_SWIPE_DURATION_MS = 1500;
static const uint32_t GESTURE_DISPLAY_MS = 1200;
static const uint8_t MISSING_FRAMES_TO_END = 3; // consecutive empty frames before ending a track

SparkFun_VL53L5CX imager;
VL53L5CX_ResultsData measurementData;

enum TrackState { STATE_IDLE, STATE_TRACKING };
TrackState state = STATE_IDLE;

float startCol = 0, startRow = 0;
float lastCol = 0, lastRow = 0;
uint32_t trackStartMs = 0;
uint8_t missingFrames = 0;

String lastGesture = "";
uint32_t gestureShownAt = 0;

uint16_t distanceToColor(int16_t mm, uint8_t status) {
  if (status != 5 || mm <= 0) {
    return M5.Display.color565(40, 40, 40); // invalid / no target
  }
  int v = constrain((int)mm, 100, 2000);
  int ratio = map(v, 100, 2000, 255, 0); // near = red, far = blue
  return M5.Display.color565(ratio, 0, 255 - ratio);
}

// Computes the centroid (in grid cell units, display space) of all zones
// currently reporting a target within the hand distance range.
bool computeHandCentroid(float &outCol, float &outRow) {
  float sumCol = 0, sumRow = 0;
  int count = 0;

  for (int row = 0; row < GRID_SIZE; row++) {
    for (int col = 0; col < GRID_SIZE; col++) {
      // VL53L5CX zone layout: row-major with columns mirrored (SparkFun convention)
      int idx = (GRID_SIZE - 1 - col) + row * GRID_SIZE;
      int16_t mm = measurementData.distance_mm[idx];
      uint8_t status = measurementData.target_status[idx];
      if (status == 5 && mm >= HAND_MIN_MM && mm <= HAND_MAX_MM) {
        sumCol += col;
        sumRow += row;
        count++;
      }
    }
  }

  if (count < MIN_VALID_ZONES) {
    return false;
  }
  outCol = sumCol / count;
  outRow = sumRow / count;
  return true;
}

void drawHeatmap(int top) {
  int cellW = M5.Display.width() / GRID_SIZE;
  int cellH = (M5.Display.height() - top) / GRID_SIZE;

  M5.Display.startWrite();
  for (int row = 0; row < GRID_SIZE; row++) {
    for (int col = 0; col < GRID_SIZE; col++) {
      int idx = (GRID_SIZE - 1 - col) + row * GRID_SIZE;
      int16_t d = measurementData.distance_mm[idx];
      uint8_t status = measurementData.target_status[idx];
      uint16_t color = distanceToColor(d, status);
      M5.Display.fillRect(col * cellW, top + row * cellH, cellW, cellH, color);
    }
  }
  M5.Display.endWrite();
}

// Evaluates the completed track (startCol/Row -> lastCol/Row) and, if it
// looks like a deliberate swipe, records the dominant direction.
void endTracking() {
  uint32_t duration = millis() - trackStartMs;
  float dCol = lastCol - startCol;
  float dRow = lastRow - startRow;
  float dist = sqrtf(dCol * dCol + dRow * dRow);

  state = STATE_IDLE;

  if (duration < MIN_SWIPE_DURATION_MS || duration > MAX_SWIPE_DURATION_MS) {
    return;
  }
  if (dist < MIN_SWIPE_DISTANCE_CELLS) {
    return;
  }

  if (fabsf(dCol) >= fabsf(dRow)) {
    lastGesture = (dCol > 0) ? "RIGHT" : "LEFT";
  } else {
    lastGesture = (dRow > 0) ? "DOWN" : "UP";
  }
  gestureShownAt = millis();
}

void drawStatusBar(int top) {
  M5.Display.fillRect(0, 0, M5.Display.width(), top, BLACK);
  M5.Display.setCursor(0, 0);
  M5.Display.setTextColor(WHITE, BLACK);

  if (lastGesture.length() > 0 && millis() - gestureShownAt < GESTURE_DISPLAY_MS) {
    M5.Display.printf("Gesture: %s", lastGesture.c_str());
  } else if (state == STATE_TRACKING) {
    M5.Display.print("Tracking...");
  } else {
    M5.Display.print("Wave a hand above sensor");
  }
}

void setup() {
  auto cfg = M5.config();
  M5.begin(cfg);

  M5.Display.setRotation(1);
  M5.Display.setTextSize(2);
  M5.Display.fillScreen(BLACK);
  M5.Display.setCursor(0, 0);
  M5.Display.println("VL53L5CX Gesture Demo");

  Wire.begin(I2C_SDA, I2C_SCL);
  Wire.setClock(400000);

  M5.Display.println("Init sensor...");
  if (!imager.begin(0x29, Wire)) {
    M5.Display.println("Sensor not found!");
    while (1) {
      delay(1000);
    }
  }

  imager.setResolution(GRID_SIZE * GRID_SIZE); // 8x8
  imager.setRangingFrequency(15);
  imager.startRanging();

  M5.Display.fillScreen(BLACK);
}

void loop() {
  M5.update();

  if (imager.isDataReady() && imager.getRangingData(&measurementData)) {
    float col, row;
    bool handPresent = computeHandCentroid(col, row);

    if (handPresent) {
      missingFrames = 0;
      if (state == STATE_IDLE) {
        state = STATE_TRACKING;
        trackStartMs = millis();
        startCol = col;
        startRow = row;
      }
      lastCol = col;
      lastRow = row;
    } else if (state == STATE_TRACKING) {
      missingFrames++;
      if (missingFrames >= MISSING_FRAMES_TO_END) {
        endTracking();
      }
    }

    static const int TOP = 20;
    drawHeatmap(TOP);
    drawStatusBar(TOP);
  }

  delay(5);
}
