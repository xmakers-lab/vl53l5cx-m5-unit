// M5Stack Basic + VL53L5CX (I2C Port A) rock-paper-scissors pose recognition demo
#include <M5Unified.h>
#include <Wire.h>
#include <SparkFun_VL53L5CX_Library.h>

static const int I2C_SDA = 21;  // M5Stack Basic Port A
static const int I2C_SCL = 22;  // M5Stack Basic Port A
static const int GRID_SIZE = 8; // 8x8 = 64 zones

// Zones within this distance range are treated as part of the hand.
// Target subject distance is 20-40cm.
static const int16_t HAND_MIN_MM = 200;
static const int16_t HAND_MAX_MM = 400;
static const int MIN_VALID_ZONES = 4; // zones needed to consider a hand present at all (rock's compact/rounded profile yields fewer valid zones than an open hand)

// Pose classification thresholds (tuned for an 8x8 grid; adjust to taste).
static const float ASPECT_ELONGATED_MIN = 1.7f; // bbox long/short ratio -> extended fingers (scissors)
static const int AREA_ROCK_PAPER_SPLIT = 22;     // compact blob: >= this many zones -> open hand (paper), else fist (rock)

static const uint8_t STABLE_FRAMES_REQUIRED = 5; // consecutive matching frames to confirm a pose
static const uint32_t GESTURE_DISPLAY_MS = 1200;
static const uint8_t MISSING_FRAMES_TO_END = 3; // consecutive empty frames before dropping the candidate

SparkFun_VL53L5CX imager;
VL53L5CX_ResultsData measurementData;

enum Gesture { GESTURE_NONE, GESTURE_ROCK, GESTURE_SCISSORS, GESTURE_PAPER };

const char *gestureName(Gesture g) {
  switch (g) {
    case GESTURE_ROCK: return "\xe3\x82\xb0\xe3\x83\xbc";       // グー
    case GESTURE_SCISSORS: return "\xe3\x83\x81\xe3\x83\xa7\xe3\x82\xad"; // チョキ
    case GESTURE_PAPER: return "\xe3\x83\x91\xe3\x83\xbc";     // パー
    default: return "";
  }
}

Gesture candidateGesture = GESTURE_NONE;
uint8_t candidateStreak = 0;
Gesture confirmedGesture = GESTURE_NONE;
uint32_t confirmedAt = 0;
uint8_t missingFrames = 0;

// Debug readout of the last computed features (shown alongside the heatmap
// so the recognition state stays legible, same spirit as the heatmap itself).
int lastArea = 0, lastWidth = 0, lastHeight = 0;

// Colors only the zones counted as "hand" by computeHandFeatures (same
// HAND_MIN_MM..HAND_MAX_MM window), so the heatmap visually matches what the
// classifier is actually seeing. Everything else is shown as dim background.
uint16_t distanceToColor(int16_t mm, uint8_t status) {
  if (status != 5 || mm < HAND_MIN_MM || mm > HAND_MAX_MM) {
    return M5.Display.color565(40, 40, 40); // invalid / out of hand range
  }
  int v = constrain((int)mm, HAND_MIN_MM, HAND_MAX_MM);
  int ratio = map(v, HAND_MIN_MM, HAND_MAX_MM, 255, 0); // near = red, far = blue
  return M5.Display.color565(ratio, 0, 255 - ratio);
}

// Scans all zones currently reporting a target within the hand distance
// range and returns the zone count plus the bounding box (in grid cells).
bool computeHandFeatures(int &outArea, int &outWidth, int &outHeight) {
  int count = 0;
  int minCol = GRID_SIZE, maxCol = -1;
  int minRow = GRID_SIZE, maxRow = -1;

  for (int row = 0; row < GRID_SIZE; row++) {
    for (int col = 0; col < GRID_SIZE; col++) {
      // VL53L5CX zone layout: row-major with columns mirrored (SparkFun convention)
      int idx = (GRID_SIZE - 1 - col) + row * GRID_SIZE;
      int16_t mm = measurementData.distance_mm[idx];
      uint8_t status = measurementData.target_status[idx];
      if (status == 5 && mm >= HAND_MIN_MM && mm <= HAND_MAX_MM) {
        count++;
        if (col < minCol) minCol = col;
        if (col > maxCol) maxCol = col;
        if (row < minRow) minRow = row;
        if (row > maxRow) maxRow = row;
      }
    }
  }

  if (count < MIN_VALID_ZONES) {
    return false;
  }
  outArea = count;
  outWidth = maxCol - minCol + 1;
  outHeight = maxRow - minRow + 1;
  return true;
}

// Classifies a hand pose from its zone count and bounding box. This is a
// coarse heuristic (8x8 resolution can't resolve individual fingers): an
// elongated blob means extended finger(s) (scissors); a compact blob is
// split into fist (rock) vs open hand (paper) purely by area. Tune the
// thresholds above against the on-screen area/width/height readout if it
// misclassifies.
Gesture classifyPose(int area, int width, int height) {
  int extentMax = max(width, height);
  int extentMin = max(1, min(width, height));
  float aspect = (float)extentMax / (float)extentMin;

  if (aspect >= ASPECT_ELONGATED_MIN) {
    return GESTURE_SCISSORS;
  }
  return (area >= AREA_ROCK_PAPER_SPLIT) ? GESTURE_PAPER : GESTURE_ROCK;
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
  M5.Display.endWrite();
}

void drawStatusBar(int top) {
  M5.Display.fillRect(0, 0, M5.Display.width(), top, BLACK);
  M5.Display.setTextColor(WHITE, BLACK);

  if (confirmedGesture != GESTURE_NONE && millis() - confirmedAt < GESTURE_DISPLAY_MS) {
    M5.Display.setFont(&fonts::efontJA_16_b);
    M5.Display.setTextSize(1);
    M5.Display.setCursor(0, 0);
    M5.Display.print(gestureName(confirmedGesture));
  } else {
    M5.Display.setFont(&fonts::Font0);
    M5.Display.setTextSize(2);
    M5.Display.setCursor(0, 0);
    if (candidateGesture != GESTURE_NONE) {
      M5.Display.print("Recognizing...");
    } else {
      M5.Display.print("Hold hand 15-30cm");
    }
  }
}

void drawDebugBar(int top) {
  M5.Display.fillRect(0, top, M5.Display.width(), M5.Display.height() - top, BLACK);
  M5.Display.setFont(&fonts::Font0);
  M5.Display.setCursor(0, top);
  M5.Display.setTextColor(DARKGREY, BLACK);
  M5.Display.setTextSize(1);
  M5.Display.printf("area:%2d  w:%d  h:%d", lastArea, lastWidth, lastHeight);
}

void setup() {
  auto cfg = M5.config();
  M5.begin(cfg);

  M5.Display.setRotation(1);
  M5.Display.setTextSize(2);
  M5.Display.fillScreen(BLACK);
  M5.Display.setCursor(0, 0);
  M5.Display.println("VL53L5CX RPS Demo");

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
    int area, width, height;
    bool handPresent = computeHandFeatures(area, width, height);

    if (handPresent) {
      missingFrames = 0;
      lastArea = area;
      lastWidth = width;
      lastHeight = height;

      Gesture raw = classifyPose(area, width, height);
      if (raw == candidateGesture) {
        candidateStreak++;
      } else {
        candidateGesture = raw;
        candidateStreak = 1;
      }
      if (candidateStreak >= STABLE_FRAMES_REQUIRED) {
        confirmedGesture = candidateGesture;
        confirmedAt = millis();
      }
    } else {
      missingFrames++;
      if (missingFrames >= MISSING_FRAMES_TO_END) {
        candidateGesture = GESTURE_NONE;
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
