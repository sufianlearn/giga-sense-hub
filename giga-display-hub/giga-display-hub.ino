// Arduino GIGA R1 WiFi + GIGA Display Shield — Central Hub
// Connects to ESP32 AP, decodes MJPEG stream, polls sensor data,
// reads local sensors, and renders a dashboard on the 800x480 display.

#include <WiFi.h>
#include <HttpClient.h>
#include <Arduino_H7_Video.h>
#include <Arduino_GigaDisplayTouch.h>
#include <ArduinoJson.h>
#include "Arduino_BMI270_BMM150.h"
#include <TJpg_Decoder.h>
#include "../shared/protocol.h"

// --- Display ---
Arduino_H7_Video display(800, 480, GigaDisplayShield);
Arduino_GigaDisplayTouch touch;

// --- Framebuffer: RGB565, 800x480 ---
// We draw to this buffer and the video library scans it out.
#define FB_WIDTH  800
#define FB_HEIGHT 480
static uint16_t framebuffer[FB_WIDTH * FB_HEIGHT] __attribute__((aligned(32), section(".sdram")));

// --- Layout constants ---
// Left side: video feed (320x240 scaled to fit, with padding)
#define VIDEO_X       20
#define VIDEO_Y       20
#define VIDEO_W       FRAME_WIDTH   // 320
#define VIDEO_H       FRAME_HEIGHT  // 240

// Right side: sensor panels
#define PANEL_X       370
#define PANEL_W       410
#define PANEL_Y_REMOTE 20
#define PANEL_H_REMOTE 200
#define PANEL_Y_LOCAL  240
#define PANEL_H_LOCAL  200
#define PANEL_Y_STATUS 450
#define PANEL_H_STATUS 25

// --- Colors (RGB565) ---
#define COLOR_BG        0x0000  // black
#define COLOR_PANEL_BG  0x18E3  // dark gray
#define COLOR_TEXT       0xFFFF  // white
#define COLOR_ACCENT     0x07E0  // green
#define COLOR_WARN       0xFD20  // orange
#define COLOR_TITLE      0x04FF  // cyan
#define COLOR_DIVIDER    0x4208  // mid gray

// --- State ---
WiFiClient streamClient;
WiFiClient sensorClient;

struct SensorData {
  float accelX, accelY, accelZ;
  float gyroX, gyroY, gyroZ;
  float temp;
  unsigned long uptime;
  bool valid;
};

SensorData remoteSensors = {0, 0, 0, 0, 0, 0, 0, 0, false};
SensorData localSensors  = {0, 0, 0, 0, 0, 0, 0, 0, false};

unsigned long lastSensorPoll  = 0;
unsigned long lastLocalRead   = 0;
unsigned long lastFpsCalc     = 0;
unsigned int  frameCount      = 0;
float         currentFps      = 0;
bool          streamConnected = false;
bool          wifiConnected   = false;

// JPEG receive buffer
#define JPEG_BUF_SIZE (32 * 1024)
static uint8_t jpegBuf[JPEG_BUF_SIZE];

// --- Drawing helpers ---

void fillRect(int x, int y, int w, int h, uint16_t color) {
  for (int row = y; row < y + h && row < FB_HEIGHT; row++) {
    for (int col = x; col < x + w && col < FB_WIDTH; col++) {
      framebuffer[row * FB_WIDTH + col] = color;
    }
  }
}

void drawChar(int x, int y, char c, uint16_t color, int scale) {
  // Minimal 5x7 bitmap font for digits, letters, punctuation
  // Only implements ASCII 32-127 range with a basic built-in font
  // For production, use a proper font library — this is a compact fallback.
  extern const uint8_t font5x7[];
  if (c < 32 || c > 127) return;

  const uint8_t *glyph = &font5x7[(c - 32) * 5];
  for (int col = 0; col < 5; col++) {
    uint8_t line = glyph[col];
    for (int row = 0; row < 7; row++) {
      if (line & (1 << row)) {
        for (int sy = 0; sy < scale; sy++) {
          for (int sx = 0; sx < scale; sx++) {
            int px = x + col * scale + sx;
            int py = y + row * scale + sy;
            if (px < FB_WIDTH && py < FB_HEIGHT) {
              framebuffer[py * FB_WIDTH + px] = color;
            }
          }
        }
      }
    }
  }
}

void drawString(int x, int y, const char *str, uint16_t color, int scale) {
  int cx = x;
  while (*str) {
    drawChar(cx, y, *str, color, scale);
    cx += 6 * scale;
    str++;
  }
}

void drawHLine(int x, int y, int w, uint16_t color) {
  for (int i = x; i < x + w && i < FB_WIDTH; i++) {
    if (y >= 0 && y < FB_HEIGHT) framebuffer[y * FB_WIDTH + i] = color;
  }
}

// --- TJpgDec output callback ---
// Writes decoded MCU blocks directly into the framebuffer at the video panel location.

bool tjpgOutputCallback(int16_t x, int16_t y, uint16_t w, uint16_t h, uint16_t *bitmap) {
  for (int row = 0; row < h; row++) {
    int fbY = VIDEO_Y + y + row;
    if (fbY < 0 || fbY >= FB_HEIGHT) continue;
    for (int col = 0; col < w; col++) {
      int fbX = VIDEO_X + x + col;
      if (fbX < 0 || fbX >= FB_WIDTH) continue;
      framebuffer[fbY * FB_WIDTH + fbX] = bitmap[row * w + col];
    }
  }
  return true;
}

// --- WiFi connection ---

void connectWiFi() {
  Serial.print("Connecting to ");
  Serial.println(AP_SSID);

  WiFi.begin(AP_SSID, AP_PASSWORD);

  int attempts = 0;
  while (WiFi.status() != WL_CONNECTED && attempts < 30) {
    delay(500);
    Serial.print(".");
    attempts++;

    // Show connection status on display
    char msg[40];
    snprintf(msg, sizeof(msg), "Connecting... %d/30", attempts);
    fillRect(0, FB_HEIGHT / 2 - 20, FB_WIDTH, 40, COLOR_BG);
    drawString(FB_WIDTH / 2 - 100, FB_HEIGHT / 2 - 10, msg, COLOR_WARN, 2);
  }

  if (WiFi.status() == WL_CONNECTED) {
    wifiConnected = true;
    Serial.print("\nConnected — IP: ");
    Serial.println(WiFi.localIP());
  } else {
    Serial.println("\nConnection failed");
  }
}

// --- MJPEG stream handling ---

bool connectStream() {
  if (streamClient.connected()) return true;

  Serial.println("Connecting to MJPEG stream...");
  if (!streamClient.connect(ESP32_IP, STREAM_PORT)) {
    Serial.println("Stream connection failed");
    return false;
  }

  // Send HTTP GET
  streamClient.print("GET " STREAM_PATH " HTTP/1.1\r\n"
                     "Host: " ESP32_IP "\r\n"
                     "Connection: keep-alive\r\n"
                     "\r\n");

  // Skip HTTP headers — read until we see the first boundary
  unsigned long timeout = millis() + 5000;
  String line;
  while (millis() < timeout) {
    if (streamClient.available()) {
      line = streamClient.readStringUntil('\n');
      if (line.startsWith("--" MJPEG_BOUNDARY)) {
        streamConnected = true;
        return true;
      }
    }
  }

  Serial.println("Stream header timeout");
  streamClient.stop();
  return false;
}

bool readOneFrame() {
  if (!streamClient.connected()) {
    streamConnected = false;
    return false;
  }

  // Read part headers to find Content-Length
  int contentLength = -1;
  unsigned long timeout = millis() + 3000;

  while (millis() < timeout) {
    if (!streamClient.available()) {
      delay(1);
      continue;
    }
    String line = streamClient.readStringUntil('\n');
    line.trim();
    if (line.length() == 0) break;  // blank line = end of headers

    if (line.startsWith("Content-Length:")) {
      contentLength = line.substring(15).toInt();
    }
  }

  if (contentLength <= 0 || contentLength > JPEG_BUF_SIZE) {
    return false;
  }

  // Read JPEG data
  int bytesRead = 0;
  timeout = millis() + 3000;
  while (bytesRead < contentLength && millis() < timeout) {
    if (streamClient.available()) {
      int toRead = min((int)streamClient.available(), contentLength - bytesRead);
      int got = streamClient.read(jpegBuf + bytesRead, toRead);
      if (got > 0) bytesRead += got;
    } else {
      delay(1);
    }
  }

  if (bytesRead != contentLength) return false;

  // Skip trailing \r\n and boundary line
  timeout = millis() + 1000;
  while (millis() < timeout) {
    if (streamClient.available()) {
      String line = streamClient.readStringUntil('\n');
      line.trim();
      if (line.startsWith("--" MJPEG_BOUNDARY)) break;
    } else {
      delay(1);
    }
  }

  return true;
}

void decodeAndDisplayFrame() {
  TJpgDec.setJpgScale(1);
  TJpgDec.setCallback(tjpgOutputCallback);
  TJpgDec.drawJpg(0, 0, jpegBuf, JPEG_BUF_SIZE);
}

// --- Sensor polling ---

void pollRemoteSensors() {
  WiFiClient client;
  if (!client.connect(ESP32_IP, STREAM_PORT)) return;

  client.print("GET " SENSOR_PATH " HTTP/1.1\r\n"
               "Host: " ESP32_IP "\r\n"
               "Connection: close\r\n"
               "\r\n");

  unsigned long timeout = millis() + 2000;
  String body;
  bool headersEnded = false;

  while (client.connected() && millis() < timeout) {
    if (client.available()) {
      String line = client.readStringUntil('\n');
      if (!headersEnded) {
        line.trim();
        if (line.length() == 0) headersEnded = true;
      } else {
        body += line;
      }
    }
  }
  client.stop();

  if (body.length() == 0) return;

  JsonDocument doc;
  DeserializationError err = deserializeJson(doc, body);
  if (err) return;

  remoteSensors.accelX = doc["accel"]["x"] | 0.0f;
  remoteSensors.accelY = doc["accel"]["y"] | 0.0f;
  remoteSensors.accelZ = doc["accel"]["z"] | 0.0f;
  remoteSensors.gyroX  = doc["gyro"]["x"]  | 0.0f;
  remoteSensors.gyroY  = doc["gyro"]["y"]  | 0.0f;
  remoteSensors.gyroZ  = doc["gyro"]["z"]  | 0.0f;
  remoteSensors.temp   = doc["temp"]        | 0.0f;
  remoteSensors.uptime = doc["uptime"]      | 0UL;
  remoteSensors.valid  = true;
}

void readLocalSensors() {
  if (IMU.accelerationAvailable()) {
    IMU.readAcceleration(localSensors.accelX, localSensors.accelY, localSensors.accelZ);
  }
  if (IMU.gyroscopeAvailable()) {
    IMU.readGyroscope(localSensors.gyroX, localSensors.gyroY, localSensors.gyroZ);
  }
  localSensors.temp = 0;  // GIGA doesn't expose temp through IMU lib directly
  localSensors.uptime = millis();
  localSensors.valid = true;
}

// --- Dashboard rendering ---

void drawPanelBorder(int x, int y, int w, int h, const char *title) {
  fillRect(x, y, w, h, COLOR_PANEL_BG);
  drawHLine(x, y, w, COLOR_DIVIDER);
  drawHLine(x, y + h - 1, w, COLOR_DIVIDER);
  drawString(x + 8, y + 6, title, COLOR_TITLE, 2);
  drawHLine(x, y + 24, w, COLOR_DIVIDER);
}

void drawSensorPanel(int x, int y, int w, int h, const char *title, SensorData &data) {
  drawPanelBorder(x, y, w, h, title);

  if (!data.valid) {
    drawString(x + 8, y + 40, "No data", COLOR_WARN, 2);
    return;
  }

  char buf[48];
  int lineY = y + 32;
  int lineH = 22;

  snprintf(buf, sizeof(buf), "Accel X:%.2f Y:%.2f Z:%.2f", data.accelX, data.accelY, data.accelZ);
  drawString(x + 8, lineY, buf, COLOR_TEXT, 1);
  lineY += lineH;

  snprintf(buf, sizeof(buf), "Gyro  X:%.1f Y:%.1f Z:%.1f", data.gyroX, data.gyroY, data.gyroZ);
  drawString(x + 8, lineY, buf, COLOR_TEXT, 1);
  lineY += lineH;

  if (data.temp != 0) {
    snprintf(buf, sizeof(buf), "Temp  %.1f C", data.temp);
    drawString(x + 8, lineY, buf, COLOR_TEXT, 1);
    lineY += lineH;
  }

  unsigned long secs = data.uptime / 1000;
  snprintf(buf, sizeof(buf), "Uptime %lum %lus", secs / 60, secs % 60);
  drawString(x + 8, lineY, buf, COLOR_TEXT, 1);
}

void drawVideoFrame() {
  // Border around video area
  fillRect(VIDEO_X - 2, VIDEO_Y - 2, VIDEO_W + 4, 2, COLOR_DIVIDER);
  fillRect(VIDEO_X - 2, VIDEO_Y + VIDEO_H, VIDEO_W + 4, 2, COLOR_DIVIDER);
  fillRect(VIDEO_X - 2, VIDEO_Y, 2, VIDEO_H, COLOR_DIVIDER);
  fillRect(VIDEO_X + VIDEO_W, VIDEO_Y, 2, VIDEO_H, COLOR_DIVIDER);

  // Label
  drawString(VIDEO_X, VIDEO_Y + VIDEO_H + 8, "Live Feed", COLOR_TITLE, 2);

  char fpsBuf[20];
  snprintf(fpsBuf, sizeof(fpsBuf), "%.1f fps", currentFps);
  drawString(VIDEO_X + 160, VIDEO_Y + VIDEO_H + 8, fpsBuf, COLOR_ACCENT, 2);
}

void drawStatusBar() {
  fillRect(0, PANEL_Y_STATUS, FB_WIDTH, PANEL_H_STATUS, COLOR_PANEL_BG);

  const char *status = wifiConnected ? (streamConnected ? "STREAMING" : "CONNECTED") : "DISCONNECTED";
  uint16_t statusColor = wifiConnected ? (streamConnected ? COLOR_ACCENT : COLOR_WARN) : 0xF800;
  drawString(10, PANEL_Y_STATUS + 5, status, statusColor, 2);

  char ipBuf[40];
  if (wifiConnected) {
    snprintf(ipBuf, sizeof(ipBuf), "IP:%s", WiFi.localIP().toString().c_str());
    drawString(250, PANEL_Y_STATUS + 5, ipBuf, COLOR_TEXT, 1);
  }
}

void drawDashboard() {
  drawVideoFrame();
  drawSensorPanel(PANEL_X, PANEL_Y_REMOTE, PANEL_W, PANEL_H_REMOTE, "Remote Sensors (ESP32)", remoteSensors);
  drawSensorPanel(PANEL_X, PANEL_Y_LOCAL, PANEL_W, PANEL_H_LOCAL, "Local Sensors (GIGA)", localSensors);

  // Connection info under video
  drawString(VIDEO_X, VIDEO_Y + VIDEO_H + 32, "SSID: " AP_SSID, COLOR_TEXT, 1);

  drawStatusBar();
}

// --- Setup ---

void setup() {
  Serial.begin(115200);
  delay(1000);
  Serial.println("\n=== GigaSenseHub Display Hub ===");

  // Init display
  display.begin();
  memset(framebuffer, 0, sizeof(framebuffer));

  // Splash screen
  drawString(200, 200, "GigaSenseHub", COLOR_TITLE, 3);
  drawString(260, 250, "Initializing...", COLOR_TEXT, 2);

  // Init touch
  touch.begin();

  // Init local IMU
  if (IMU.begin()) {
    Serial.println("Local IMU initialized");
  } else {
    Serial.println("Local IMU init failed");
  }

  // Init JPEG decoder
  TJpgDec.setJpgScale(1);
  TJpgDec.setCallback(tjpgOutputCallback);

  // Connect WiFi
  connectWiFi();

  // Clear screen for dashboard
  memset(framebuffer, 0, sizeof(framebuffer));

  lastSensorPoll = millis();
  lastLocalRead = millis();
  lastFpsCalc = millis();
}

// --- Main loop ---

void loop() {
  unsigned long now = millis();

  // Maintain WiFi
  if (WiFi.status() != WL_CONNECTED) {
    wifiConnected = false;
    streamConnected = false;
    connectWiFi();
    return;
  }

  // Maintain stream connection
  if (!streamConnected) {
    connectStream();
  }

  // Read and decode one MJPEG frame
  if (streamConnected) {
    if (readOneFrame()) {
      decodeAndDisplayFrame();
      frameCount++;
    }
  }

  // Calculate FPS every second
  if (now - lastFpsCalc >= 1000) {
    currentFps = frameCount * 1000.0f / (now - lastFpsCalc);
    frameCount = 0;
    lastFpsCalc = now;
  }

  // Poll remote sensors every 500ms
  if (now - lastSensorPoll >= 500) {
    pollRemoteSensors();
    lastSensorPoll = now;
  }

  // Read local sensors every 100ms
  if (now - lastLocalRead >= 100) {
    readLocalSensors();
    lastLocalRead = now;
  }

  // Redraw dashboard overlay (sensor panels, status bar)
  // Video pixels are written directly by the JPEG decoder callback
  drawDashboard();
}
