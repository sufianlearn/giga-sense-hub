// ESP32-S3 Sense — Wireless Camera Node with Motion Detection
// Runs as WiFi AP, serves MJPEG video stream.
// Frame-differencing motion detection with configurable thresholds.
// Board: Seeed Studio XIAO ESP32-S3 Sense

#include <Arduino.h>
#include <WiFi.h>
#include <WebServer.h>
#include "esp_camera.h"
#include "protocol.h"

// Camera pin definitions for XIAO ESP32-S3 Sense
#define PWDN_GPIO_NUM  -1
#define RESET_GPIO_NUM -1
#define XCLK_GPIO_NUM  10
#define SIOD_GPIO_NUM  40
#define SIOC_GPIO_NUM  39
#define Y9_GPIO_NUM    48
#define Y8_GPIO_NUM    11
#define Y7_GPIO_NUM    12
#define Y6_GPIO_NUM    14
#define Y5_GPIO_NUM    16
#define Y4_GPIO_NUM    18
#define Y3_GPIO_NUM    17
#define Y2_GPIO_NUM    15
#define VSYNC_GPIO_NUM 38
#define HREF_GPIO_NUM  47
#define PCLK_GPIO_NUM  13

WebServer server(STREAM_PORT);

// ============================================================
// Motion Detection State
// ============================================================
static uint8_t *refFrame = nullptr;        // Reference frame (grayscale)
static uint8_t *curFrame = nullptr;        // Current frame (grayscale)
static bool     motionDetected = false;
static float    motionScore    = 0.0f;     // % of changed pixels
static uint32_t motionCount    = 0;        // Cumulative motion events
static unsigned long lastMotionTime = 0;
static unsigned long lastDetectTime = 0;
static int      pixelThreshold = MOTION_THRESHOLD_DEFAULT;
static int      percentThreshold = MOTION_PERCENT_DEFAULT;
static bool     motionEnabled  = true;
static const int DETECT_PIXELS = MOTION_WIDTH * MOTION_HEIGHT;

// ============================================================
// Camera Init
// ============================================================
void initCamera() {
  camera_config_t config;
  config.ledc_channel = LEDC_CHANNEL_0;
  config.ledc_timer   = LEDC_TIMER_0;
  config.pin_d0       = Y2_GPIO_NUM;
  config.pin_d1       = Y3_GPIO_NUM;
  config.pin_d2       = Y4_GPIO_NUM;
  config.pin_d3       = Y5_GPIO_NUM;
  config.pin_d4       = Y6_GPIO_NUM;
  config.pin_d5       = Y7_GPIO_NUM;
  config.pin_d6       = Y8_GPIO_NUM;
  config.pin_d7       = Y9_GPIO_NUM;
  config.pin_xclk     = XCLK_GPIO_NUM;
  config.pin_pclk     = PCLK_GPIO_NUM;
  config.pin_vsync    = VSYNC_GPIO_NUM;
  config.pin_href     = HREF_GPIO_NUM;
  config.pin_sccb_sda = SIOD_GPIO_NUM;
  config.pin_sccb_scl = SIOC_GPIO_NUM;
  config.pin_pwdn     = PWDN_GPIO_NUM;
  config.pin_reset    = RESET_GPIO_NUM;
  config.xclk_freq_hz = 20000000;
  config.pixel_format = PIXFORMAT_JPEG;
  config.frame_size   = FRAMESIZE_QVGA;
  config.jpeg_quality = JPEG_QUALITY;
  config.fb_count     = 2;
  config.grab_mode    = CAMERA_GRAB_LATEST;

  esp_err_t err = esp_camera_init(&config);
  if (err != ESP_OK) {
    Serial.printf("Camera init failed: 0x%x\n", err);
    while (true) { delay(1000); }
  }
  Serial.println("Camera initialized");
}

// ============================================================
// Bilinear Downscale JPEG → Grayscale
// ============================================================
// Decodes JPEG to RGB565, then downscales to grayscale.
// We use the hardware JPEG decoder and do a fast box-filter downsample.
// For a QVGA (320x240) → 80x60 that's a 4x4 box per output pixel.

static bool jpegToGrayscale(camera_fb_t *fb, uint8_t *outBuf) {
  // Temporarily switch to grayscale to get raw luma
  // We'll use a second camera capture in grayscale mode
  // Instead: decode the JPEG via ESP32's built-in decoder

  // Simple approach: use fmt2rgb888 then average
  // But that needs a 320*240*3 = 230KB buffer — too much.
  // Better: capture a separate low-res grayscale frame.
  // Most efficient: use two frame buffers and switch pixel format.

  // Actually, the cleanest approach for the ESP32-S3 is to use
  // the JPEG decoder built into esp_camera. Let's decode to RGB565
  // in a small temp buffer, then downsample.

  // For efficiency, we'll use a line-by-line approach.
  // But the simplest working approach: just capture in grayscale.
  // We can't switch pixel_format on the fly without reinit.
  
  // PRAGMATIC SOLUTION: Use the JPEG data directly.
  // Decode JPEG → RGB888 using fmt2rgb888, then downsample.
  // We allocate from PSRAM which the ESP32-S3 has.

  uint8_t *rgb = (uint8_t *)ps_malloc(FRAME_WIDTH * FRAME_HEIGHT * 3);
  if (!rgb) return false;

  bool ok = fmt2rgb888(fb->buf, fb->len, PIXFORMAT_JPEG, rgb);
  if (!ok) { free(rgb); return false; }

  // Box-filter downsample to grayscale
  int scaleX = FRAME_WIDTH / MOTION_WIDTH;    // 4
  int scaleY = FRAME_HEIGHT / MOTION_HEIGHT;   // 4

  for (int oy = 0; oy < MOTION_HEIGHT; oy++) {
    for (int ox = 0; ox < MOTION_WIDTH; ox++) {
      uint32_t sum = 0;
      int count = 0;
      for (int dy = 0; dy < scaleY; dy++) {
        for (int dx = 0; dx < scaleX; dx++) {
          int sx = ox * scaleX + dx;
          int sy = oy * scaleY + dy;
          int idx = (sy * FRAME_WIDTH + sx) * 3;
          // Luminance: 0.299R + 0.587G + 0.114B (integer approx)
          uint32_t lum = (rgb[idx] * 77 + rgb[idx+1] * 150 + rgb[idx+2] * 29) >> 8;
          sum += lum;
          count++;
        }
      }
      outBuf[oy * MOTION_WIDTH + ox] = sum / count;
    }
  }

  free(rgb);
  return true;
}

// ============================================================
// Motion Detection — Frame Differencing
// ============================================================

static void runMotionDetection() {
  if (!motionEnabled) return;

  camera_fb_t *fb = esp_camera_fb_get();
  if (!fb) return;

  bool ok = jpegToGrayscale(fb, curFrame);
  esp_camera_fb_return(fb);
  if (!ok) return;

  // If no reference frame yet, copy current as reference
  if (refFrame[0] == 0 && refFrame[1] == 0 && refFrame[2] == 0) {
    memcpy(refFrame, curFrame, DETECT_PIXELS);
    return;
  }

  // Count pixels that differ by more than threshold
  int changedPixels = 0;
  for (int i = 0; i < DETECT_PIXELS; i++) {
    int diff = abs((int)curFrame[i] - (int)refFrame[i]);
    if (diff > pixelThreshold) changedPixels++;
  }

  motionScore = (float)changedPixels * 100.0f / (float)DETECT_PIXELS;
  bool wasDetected = motionDetected;
  motionDetected = (motionScore >= (float)percentThreshold);

  if (motionDetected && !wasDetected) {
    motionCount++;
    lastMotionTime = millis();
    Serial.printf("MOTION DETECTED: %.1f%% changed (%d/%d pixels)\n",
                  motionScore, changedPixels, DETECT_PIXELS);
  }

  // Exponential moving average: blend reference toward current
  // This adapts to slow lighting changes while still detecting fast motion.
  // alpha = 0.05 (5% new frame blended each cycle)
  for (int i = 0; i < DETECT_PIXELS; i++) {
    refFrame[i] = (uint8_t)((refFrame[i] * 243 + curFrame[i] * 13) >> 8);
    // 243/256 ≈ 0.95, 13/256 ≈ 0.05
  }

  lastDetectTime = millis();
}

// ============================================================
// HTTP Handlers
// ============================================================

void handleStream() {
  WiFiClient client = server.client();

  String header = "HTTP/1.1 200 OK\r\n"
                  "Content-Type: multipart/x-mixed-replace;boundary=" MJPEG_BOUNDARY "\r\n"
                  "\r\n";
  client.print(header);

  unsigned long frameInterval = 1000 / TARGET_FPS;

  while (client.connected()) {
    unsigned long frameStart = millis();

    camera_fb_t *fb = esp_camera_fb_get();
    if (!fb) continue;

    String part = "--" MJPEG_BOUNDARY "\r\n"
                  "Content-Type: image/jpeg\r\n"
                  "Content-Length: " + String(fb->len) + "\r\n"
                  "\r\n";
    client.print(part);
    client.write(fb->buf, fb->len);
    client.print("\r\n");

    esp_camera_fb_return(fb);

    unsigned long elapsed = millis() - frameStart;
    if (elapsed < frameInterval) {
      delay(frameInterval - elapsed);
    }
  }
}

void handleMotion() {
  // Return motion state as JSON
  // The GIGA polls this endpoint to get real detection data
  char json[256];
  snprintf(json, sizeof(json),
    "{\"detected\":%s,"
    "\"score\":%.1f,"
    "\"threshold\":{\"pixel\":%d,\"percent\":%d},"
    "\"count\":%lu,"
    "\"lastMotion\":%lu,"
    "\"lastCheck\":%lu,"
    "\"enabled\":%s,"
    "\"uptime\":%lu}",
    motionDetected ? "true" : "false",
    motionScore,
    pixelThreshold, percentThreshold,
    (unsigned long)motionCount,
    lastMotionTime,
    lastDetectTime,
    motionEnabled ? "true" : "false",
    millis() / 1000);

  server.send(200, "application/json", json);
}

void handleMotionConfig() {
  // POST /motion with query params to configure
  if (server.hasArg("pixel_threshold")) {
    pixelThreshold = constrain(server.arg("pixel_threshold").toInt(), 1, 100);
  }
  if (server.hasArg("percent_threshold")) {
    percentThreshold = constrain(server.arg("percent_threshold").toInt(), 1, 50);
  }
  if (server.hasArg("enabled")) {
    motionEnabled = (server.arg("enabled") == "1" || server.arg("enabled") == "true");
  }
  if (server.hasArg("reset")) {
    // Reset reference frame — forces recalibration
    memset(refFrame, 0, DETECT_PIXELS);
    motionDetected = false;
    motionScore = 0;
    motionCount = 0;
  }

  Serial.printf("Motion config: pixel=%d, percent=%d%%, enabled=%s\n",
    pixelThreshold, percentThreshold, motionEnabled ? "yes" : "no");

  handleMotion(); // Return current state
}

void handleRoot() {
  String html = "<html><body>"
                "<h1>GigaSenseHub Camera Node</h1>"
                "<p><a href=\"" STREAM_PATH "\">MJPEG Stream</a></p>"
                "<p><a href=\"" MOTION_PATH "\">Motion Status (JSON)</a></p>"
                "<p>Motion: " + String(motionDetected ? "DETECTED" : "clear") +
                " (" + String(motionScore, 1) + "% changed)</p>"
                "</body></html>";
  server.send(200, "text/html", html);
}

// ============================================================
// Setup & Loop
// ============================================================

void setup() {
  Serial.begin(115200);
  delay(1000);
  Serial.println("\n=== GigaSenseHub Camera Node ===");
  Serial.println("Motion detection: frame differencing (80x60 grayscale)");

  // Allocate motion detection buffers in PSRAM
  refFrame = (uint8_t *)ps_calloc(DETECT_PIXELS, 1);
  curFrame = (uint8_t *)ps_calloc(DETECT_PIXELS, 1);
  if (!refFrame || !curFrame) {
    Serial.println("ERROR: Failed to allocate motion buffers in PSRAM");
    while (true) { delay(1000); }
  }
  Serial.printf("Motion buffers: %d bytes each in PSRAM\n", DETECT_PIXELS);

  WiFi.softAP(AP_SSID, AP_PASSWORD, AP_CHANNEL);
  Serial.print("AP started — IP: ");
  Serial.println(WiFi.softAPIP());

  initCamera();

  server.on("/", handleRoot);
  server.on(STREAM_PATH, HTTP_GET, handleStream);
  server.on(MOTION_PATH, HTTP_GET, handleMotion);
  server.on(MOTION_PATH, HTTP_POST, handleMotionConfig);
  server.begin();
  Serial.println("HTTP server started");
}

static unsigned long lastDetect = 0;

void loop() {
  server.handleClient();

  // Run motion detection ~4 times per second (250ms interval)
  // This doesn't interfere with streaming — the camera has 2 framebuffers.
  if (millis() - lastDetect >= 250) {
    lastDetect = millis();
    runMotionDetection();
  }
}
