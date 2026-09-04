// M5Stack Basic + VL53L5CX (I2C Port A) 8x8 distance map on LCD
#include <M5Unified.h>
#include <Wire.h>
#include <SparkFun_VL53L5CX_Library.h>

static const int I2C_SDA = 21;  // M5Stack Basic Port A
static const int I2C_SCL = 22;  // M5Stack Basic Port A
static const int GRID_SIZE = 8; // 8x8 = 64 zones

SparkFun_VL53L5CX imager;
VL53L5CX_ResultsData measurementData;

uint16_t distanceToColor(int16_t mm, uint8_t status) {
  if (status != 5 || mm <= 0) {
    return M5.Display.color565(40, 40, 40); // invalid / no target
  }
  int v = constrain((int)mm, 100, 2000);
  int ratio = map(v, 100, 2000, 255, 0); // near = red, far = blue
  return M5.Display.color565(ratio, 0, 255 - ratio);
}

void drawGrid() {
  int cellW = M5.Display.width() / GRID_SIZE;
  int cellH = (M5.Display.height() - 20) / GRID_SIZE;
  int top = 20;

  M5.Display.startWrite();
  for (int row = 0; row < GRID_SIZE; row++) {
    for (int col = 0; col < GRID_SIZE; col++) {
      // VL53L5CX zone layout: row-major with columns mirrored (SparkFun convention)
      int idx = (GRID_SIZE - 1 - col) + row * GRID_SIZE;
      int16_t d = measurementData.distance_mm[idx];
      uint8_t status = measurementData.target_status[idx];
      uint16_t color = distanceToColor(d, status);
      M5.Display.fillRect(col * cellW, top + row * cellH, cellW, cellH, color);
    }
  }
  M5.Display.endWrite();

  int centerIdx = (GRID_SIZE - 1 - GRID_SIZE / 2) + (GRID_SIZE / 2) * GRID_SIZE;
  M5.Display.fillRect(0, 0, M5.Display.width(), top, BLACK);
  M5.Display.setCursor(0, 0);
  M5.Display.setTextColor(WHITE, BLACK);
  M5.Display.printf("center: %d mm", measurementData.distance_mm[centerIdx]);
}

void setup() {
  auto cfg = M5.config();
  M5.begin(cfg);

  M5.Display.setRotation(1);
  M5.Display.setTextSize(2);
  M5.Display.fillScreen(BLACK);
  M5.Display.setCursor(0, 0);
  M5.Display.println("VL53L5CX 8x8");

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

  if (imager.isDataReady()) {
    if (imager.getRangingData(&measurementData)) {
      drawGrid();
    }
  }
  delay(5);
}
