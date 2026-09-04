// M5Stack Basic + VL53L5CX (I2C Port A) pointing-finger direction demo
#include <M5Unified.h>
#include <Wire.h>
#include <SparkFun_VL53L5CX_Library.h>

static const int I2C_SDA = 21;  // M5Stack Basic Port A
static const int I2C_SCL = 22;  // M5Stack Basic Port A
static const int GRID_SIZE = 8; // 8x8 = 64 zones

// The hand detection window is dynamic: each frame, find the single nearest
// valid reading across all 64 zones, then treat everything within
// NEAR_WINDOW_MM of it as the hand. This follows the hand wherever it
// actually is instead of relying on a fixed absolute distance.
static const int16_t NEAR_WINDOW_MM = 30; // 5cm band above the nearest reading

// Sanity bound: reject the frame unless the window's center falls in this
// range, so a nearby wall or an out-of-position hand isn't picked up as if
// it were a properly placed hand.
static const int16_t WINDOW_CENTER_MIN_MM = 120; // 12cm
static const int16_t WINDOW_CENTER_MAX_MM = 300; // 30cm

static const int MIN_VALID_ZONES = 4; // zones needed to consider a hand present at all

// A finger reads as an elongated blob whose tip sits off to one side of the
// blob's centroid (the centroid is pulled toward the wider palm/knuckle
// mass). This is the minimum centroid->tip distance (in grid cells) before
// that offset is trusted as a pointing direction rather than sensor noise.
static const float MIN_POINT_VECTOR_CELLS = 1.1f;

static const uint8_t STABLE_FRAMES_REQUIRED = 5; // consecutive matching frames to confirm a direction
static const uint32_t DIRECTION_DISPLAY_MS = 1200;
static const uint8_t MISSING_FRAMES_TO_END = 3; // consecutive empty frames before dropping the candidate

SparkFun_VL53L5CX imager;
VL53L5CX_ResultsData measurementData;

enum Direction {
  DIR_NONE,
  DIR_UP,
  DIR_UPPER_RIGHT,
  DIR_RIGHT,
  DIR_LOWER_RIGHT,
  DIR_DOWN,
  DIR_LOWER_LEFT,
  DIR_LEFT,
  DIR_UPPER_LEFT
};

const char *directionName(Direction d) {
  switch (d) {
    case DIR_UP: return "\xe4\xb8\x8a";                         // 上
    case DIR_UPPER_RIGHT: return "\xe5\x8f\xb3\xe4\xb8\x8a";     // 右上
    case DIR_RIGHT: return "\xe5\x8f\xb3";                       // 右
    case DIR_LOWER_RIGHT: return "\xe5\x8f\xb3\xe4\xb8\x8b";     // 右下
    case DIR_DOWN: return "\xe4\xb8\x8b";                       // 下
    case DIR_LOWER_LEFT: return "\xe5\xb7\xa6\xe4\xb8\x8b";     // 左下
    case DIR_LEFT: return "\xe5\xb7\xa6";                       // 左
    case DIR_UPPER_LEFT: return "\xe5\xb7\xa6\xe4\xb8\x8a";     // 左上
    default: return "";
  }
}

Direction candidateDirection = DIR_NONE;
uint8_t candidateStreak = 0;
Direction confirmedDirection = DIR_NONE;
uint32_t confirmedAt = 0;
uint8_t missingFrames = 0;

// Debug readout of the last computed features, shown alongside the heatmap
// so the recognition state stays legible.
int lastArea = 0;
float lastCentroidCol = 0, lastCentroidRow = 0;
float lastTipCol = 0, lastTipRow = 0;
float lastVectorLen = 0;
bool haveTip = false;

// Current frame's dynamic hand window, kept as globals so distanceToColor
// (called from drawHeatmap, which runs after computeHandFeatures) can shade
// the heatmap to match what the classifier is actually seeing.
int16_t lastWindowMin = 0, lastWindowMax = 0;
bool windowValid = false;

uint16_t distanceToColor(int16_t mm, uint8_t status) {
  if (!windowValid || status != 5 || mm < lastWindowMin || mm > lastWindowMax) {
    return M5.Display.color565(40, 40, 40); // invalid / outside this frame's hand window
  }
  int v = constrain((int)mm, lastWindowMin, lastWindowMax);
  int ratio = map(v, lastWindowMin, lastWindowMax, 255, 0); // near = red, far = blue
  return M5.Display.color565(ratio, 0, 255 - ratio);
}

// Finds the nearest valid reading across all 64 zones, then defines the
// hand window as [nearest, nearest + NEAR_WINDOW_MM]. Rejects the frame if
// the window's center falls outside WINDOW_CENTER_MIN_MM..MAX_MM (e.g. the
// nearest thing in view is a wall, not a hand at a plausible distance).
// Scans zones within that window and returns the zone count, the centroid
// (grid coords), and the tip -- the zone farthest from the centroid, which
// for a pointing finger sits at the fingertip since the centroid is pulled
// toward the bulkier hand mass.
bool computeHandFeatures(int &outArea, float &outCentroidCol, float &outCentroidRow,
                          float &outTipCol, float &outTipRow,
                          int16_t &outWindowMin, int16_t &outWindowMax) {
  int16_t nearest = INT16_MAX;
  for (int i = 0; i < GRID_SIZE * GRID_SIZE; i++) {
    if (measurementData.target_status[i] == 5 && measurementData.distance_mm[i] < nearest) {
      nearest = measurementData.distance_mm[i];
    }
  }
  if (nearest == INT16_MAX) {
    return false;
  }

  int16_t windowMin = nearest;
  int16_t windowMax = nearest + NEAR_WINDOW_MM;
  int16_t windowCenter = nearest + NEAR_WINDOW_MM / 2;
  if (windowCenter < WINDOW_CENTER_MIN_MM || windowCenter > WINDOW_CENTER_MAX_MM) {
    return false;
  }

  float sumCol = 0, sumRow = 0;
  int count = 0;
  int cols[GRID_SIZE * GRID_SIZE];
  int rows[GRID_SIZE * GRID_SIZE];

  for (int row = 0; row < GRID_SIZE; row++) {
    for (int col = 0; col < GRID_SIZE; col++) {
      // VL53L5CX zone layout: row-major with columns mirrored (SparkFun convention)
      int idx = (GRID_SIZE - 1 - col) + row * GRID_SIZE;
      int16_t mm = measurementData.distance_mm[idx];
      uint8_t status = measurementData.target_status[idx];
      if (status == 5 && mm >= windowMin && mm <= windowMax) {
        cols[count] = col;
        rows[count] = row;
        sumCol += col;
        sumRow += row;
        count++;
      }
    }
  }

  if (count < MIN_VALID_ZONES) {
    return false;
  }

  outArea = count;
  outCentroidCol = sumCol / count;
  outCentroidRow = sumRow / count;
  outWindowMin = windowMin;
  outWindowMax = windowMax;

  float bestDist2 = -1;
  for (int i = 0; i < count; i++) {
    float dCol = cols[i] - outCentroidCol;
    float dRow = rows[i] - outCentroidRow;
    float dist2 = dCol * dCol + dRow * dRow;
    if (dist2 > bestDist2) {
      bestDist2 = dist2;
      outTipCol = cols[i];
      outTipRow = rows[i];
    }
  }
  return true;
}

// Classifies the centroid->tip vector into one of 8 compass directions.
// Row increases downward (screen space), so a positive dRow points DOWN.
Direction classifyDirection(float dCol, float dRow, float &outLen) {
  outLen = sqrtf(dCol * dCol + dRow * dRow);
  if (outLen < MIN_POINT_VECTOR_CELLS) {
    return DIR_NONE;
  }
  float angleDeg = atan2f(dRow, dCol) * 180.0f / PI;
  if (angleDeg < 0) angleDeg += 360.0f;

  int sector = (int)((angleDeg + 22.5f) / 45.0f) % 8;
  static const Direction SECTOR_DIRECTIONS[8] = {
    DIR_RIGHT, DIR_LOWER_RIGHT, DIR_DOWN, DIR_LOWER_LEFT,
    DIR_LEFT, DIR_UPPER_LEFT, DIR_UP, DIR_UPPER_RIGHT
  };
  return SECTOR_DIRECTIONS[sector];
}

void drawHeatmap(int top, int bottom) {
  int cellW = M5.Display.width() / GRID_SIZE;
  int cellH = (bottom - top) / GRID_SIZE;

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

  // Overlay an arrow from the hand centroid through the fingertip so the
  // detected pointing direction is visible directly on the heatmap.
  if (haveTip) {
    float cx = (lastCentroidCol + 0.5f) * cellW;
    float cy = top + (lastCentroidRow + 0.5f) * cellH;
    float tx = (lastTipCol + 0.5f) * cellW;
    float ty = top + (lastTipRow + 0.5f) * cellH;

    M5.Display.drawLine((int)cx, (int)cy, (int)tx, (int)ty, WHITE);

    float dx = tx - cx, dy = ty - cy;
    float len = sqrtf(dx * dx + dy * dy);
    if (len > 1.0f) {
      float ux = dx / len, uy = dy / len;
      float perpX = -uy, perpY = ux;
      float headLen = min(10.0f, len * 0.4f);
      float headWidth = headLen * 0.6f;
      int hx1 = (int)(tx - ux * headLen + perpX * headWidth);
      int hy1 = (int)(ty - uy * headLen + perpY * headWidth);
      int hx2 = (int)(tx - ux * headLen - perpX * headWidth);
      int hy2 = (int)(ty - uy * headLen - perpY * headWidth);
      M5.Display.fillTriangle((int)tx, (int)ty, hx1, hy1, hx2, hy2, WHITE);
    }
  }
  M5.Display.endWrite();
}

void drawStatusBar(int top) {
  M5.Display.fillRect(0, 0, M5.Display.width(), top, BLACK);
  M5.Display.setTextColor(WHITE, BLACK);

  if (confirmedDirection != DIR_NONE && millis() - confirmedAt < DIRECTION_DISPLAY_MS) {
    M5.Display.setFont(&fonts::efontJA_16_b);
    M5.Display.setTextSize(1);
    M5.Display.setCursor(0, 0);
    M5.Display.print(directionName(confirmedDirection));
  } else {
    M5.Display.setFont(&fonts::Font0);
    M5.Display.setTextSize(2);
    M5.Display.setCursor(0, 0);
    if (candidateDirection != DIR_NONE) {
      M5.Display.print("Recognizing...");
    } else {
      M5.Display.print("Point finger 12-30cm");
    }
  }
}

void drawDebugBar(int top) {
  M5.Display.fillRect(0, top, M5.Display.width(), M5.Display.height() - top, BLACK);
  M5.Display.setFont(&fonts::Font0);
  M5.Display.setCursor(0, top);
  M5.Display.setTextColor(DARKGREY, BLACK);
  M5.Display.setTextSize(1);
  M5.Display.printf("area:%2d win:%d-%dmm vec:%.1f", lastArea, lastWindowMin, lastWindowMax, lastVectorLen);
}

void setup() {
  auto cfg = M5.config();
  M5.begin(cfg);

  M5.Display.setRotation(1);
  M5.Display.setTextSize(2);
  M5.Display.fillScreen(BLACK);
  M5.Display.setCursor(0, 0);
  M5.Display.println("VL53L5CX Finger Direction");

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
    int area;
    float centroidCol, centroidRow, tipCol, tipRow;
    int16_t windowMin, windowMax;
    bool handPresent = computeHandFeatures(area, centroidCol, centroidRow, tipCol, tipRow,
                                            windowMin, windowMax);

    if (handPresent) {
      missingFrames = 0;
      lastArea = area;
      lastCentroidCol = centroidCol;
      lastCentroidRow = centroidRow;
      lastTipCol = tipCol;
      lastTipRow = tipRow;
      lastWindowMin = windowMin;
      lastWindowMax = windowMax;
      windowValid = true;
      haveTip = true;

      float len;
      Direction raw = classifyDirection(tipCol - centroidCol, tipRow - centroidRow, len);
      lastVectorLen = len;

      if (raw != DIR_NONE) {
        if (raw == candidateDirection) {
          candidateStreak++;
        } else {
          candidateDirection = raw;
          candidateStreak = 1;
        }
        if (candidateStreak >= STABLE_FRAMES_REQUIRED) {
          confirmedDirection = candidateDirection;
          confirmedAt = millis();
        }
      }
    } else {
      haveTip = false;
      windowValid = false;
      missingFrames++;
      if (missingFrames >= MISSING_FRAMES_TO_END) {
        candidateDirection = DIR_NONE;
        candidateStreak = 0;
      }
    }

    static const int TOP = 20;
    static const int DEBUG_H = 12;
    drawHeatmap(TOP, M5.Display.height() - DEBUG_H);
    drawStatusBar(TOP);
    drawDebugBar(M5.Display.height() - DEBUG_H);
  }

  delay(5);
}
