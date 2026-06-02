// ESP32-S3 Sense — Wireless Camera Node (Multi-Node STA Mode)
// Connects to GIGA R1 AP as a station, serves MJPEG + motion + OTA.
// NODE_ID set via build flag (-DNODE_ID=0 or -DNODE_ID=1).
// Board: Seeed Studio XIAO ESP32-S3 Sense

#include <Arduino.h>
#include <WiFi.h>
#include <WebServer.h>
#include <ArduinoOTA.h>
#include <Update.h>
#include "esp_camera.h"
#include "esp_sleep.h"
#include "esp_wifi.h"
#include "mbedtls/md.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"
#include "protocol.h"

#ifndef NODE_ID
  #define NODE_ID 0
#endif

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
static uint8_t *refFrame = nullptr;
static uint8_t *curFrame = nullptr;
static bool     motionDetected = false;
static float    motionScore    = 0.0f;
static uint32_t motionCount    = 0;
static unsigned long lastMotionTime = 0;
static unsigned long lastDetectTime = 0;
static int      pixelThreshold = MOTION_THRESHOLD_DEFAULT;
static int      percentThreshold = MOTION_PERCENT_DEFAULT;
static bool     motionEnabled  = true;
static const int DETECT_PIXELS = MOTION_WIDTH * MOTION_HEIGHT;

// ============================================================
// OTA State
// ============================================================
static bool     otaInProgress = false;
static int      otaProgress   = 0;
static String   otaError      = "";
static uint32_t otaLastUpdate = 0;

// ============================================================
// Power Management State
// ============================================================
static int      powerState       = POWER_STATE_ACTIVE;
static unsigned long lastActivityTime = 0;  // Last motion or stream client activity
static bool     streamClientActive = false; // True while a client is streaming
static int      wakeReason       = 0;       // esp_sleep_get_wakeup_cause() at boot
static RTC_DATA_ATTR int bootCount = 0;     // Persists across deep sleep

// ============================================================
// FreeRTOS Task Handles & Sync
// ============================================================
static TaskHandle_t httpTaskHandle     = nullptr;
static TaskHandle_t motionTaskHandle   = nullptr;
static TaskHandle_t housekeepTaskHandle = nullptr;
static SemaphoreHandle_t cameraMutex   = nullptr;  // Guards esp_camera_fb_get
static volatile uint32_t httpTaskWdog    = 0;  // Watchdog counters
static volatile uint32_t motionTaskWdog  = 0;
static volatile uint32_t housekeepWdog   = 0;

// ============================================================
// HMAC-SHA256 Authentication
// ============================================================
// Generate HMAC-SHA256 hex string from a message
static void hmacSha256Hex(const char *msg, char *outHex) {
  uint8_t hash[32];
  mbedtls_md_context_t ctx;
  mbedtls_md_init(&ctx);
  mbedtls_md_setup(&ctx, mbedtls_md_info_from_type(MBEDTLS_MD_SHA256), 1);
  mbedtls_md_hmac_starts(&ctx, (const unsigned char *)HMAC_SECRET_KEY, strlen(HMAC_SECRET_KEY));
  mbedtls_md_hmac_update(&ctx, (const unsigned char *)msg, strlen(msg));
  mbedtls_md_hmac_finish(&ctx, hash);
  mbedtls_md_free(&ctx);
  for (int i = 0; i < 32; i++) {
    sprintf(outHex + i * 2, "%02x", hash[i]);
  }
  outHex[64] = '\0';
}

// Verify HMAC token from request header
static bool verifyHmacAuth(const char *path) {
  if (!server.hasHeader(AUTH_HEADER) || !server.hasHeader(AUTH_NONCE_HEADER)) {
    return false;
  }
  String token = server.header(AUTH_HEADER);
  String nonce = server.header(AUTH_NONCE_HEADER);
  // Reconstruct expected HMAC: HMAC(key, "nonce:path")
  String msg = nonce + ":" + String(path);
  char expected[65];
  hmacSha256Hex(msg.c_str(), expected);
  return token.equals(expected);
}

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

static bool jpegToGrayscale(camera_fb_t *fb, uint8_t *outBuf) {
  uint8_t *rgb = (uint8_t *)ps_malloc(FRAME_WIDTH * FRAME_HEIGHT * 3);
  if (!rgb) return false;

  bool ok = fmt2rgb888(fb->buf, fb->len, PIXFORMAT_JPEG, rgb);
  if (!ok) { free(rgb); return false; }

  int scaleX = FRAME_WIDTH / MOTION_WIDTH;
  int scaleY = FRAME_HEIGHT / MOTION_HEIGHT;

  for (int oy = 0; oy < MOTION_HEIGHT; oy++) {
    for (int ox = 0; ox < MOTION_WIDTH; ox++) {
      uint32_t sum = 0;
      int count = 0;
      for (int dy = 0; dy < scaleY; dy++) {
        for (int dx = 0; dx < scaleX; dx++) {
          int sx = ox * scaleX + dx;
          int sy = oy * scaleY + dy;
          int idx = (sy * FRAME_WIDTH + sx) * 3;
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
  if (!motionEnabled || otaInProgress) return;

  camera_fb_t *fb = esp_camera_fb_get();
  if (!fb) return;

  bool ok = jpegToGrayscale(fb, curFrame);
  esp_camera_fb_return(fb);
  if (!ok) return;

  if (refFrame[0] == 0 && refFrame[1] == 0 && refFrame[2] == 0) {
    memcpy(refFrame, curFrame, DETECT_PIXELS);
    return;
  }

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

  for (int i = 0; i < DETECT_PIXELS; i++) {
    refFrame[i] = (uint8_t)((refFrame[i] * 243 + curFrame[i] * 13) >> 8);
  }

  lastDetectTime = millis();
}

// ============================================================
// OTA Setup
// ============================================================

void setupOTA() {
  char hostname[32];
  snprintf(hostname, sizeof(hostname), "%s-%d", OTA_HOSTNAME_PREFIX, NODE_ID);
  ArduinoOTA.setHostname(hostname);
  ArduinoOTA.setPort(OTA_PORT);

  ArduinoOTA.onStart([]() {
    otaInProgress = true;
    otaProgress = 0;
    otaError = "";
    String type = (ArduinoOTA.getCommand() == U_FLASH) ? "firmware" : "filesystem";
    Serial.printf("OTA Start: %s\n", type.c_str());
  });

  ArduinoOTA.onEnd([]() {
    otaInProgress = false;
    otaProgress = 100;
    Serial.println("\nOTA Complete — rebooting");
  });

  ArduinoOTA.onProgress([](unsigned int progress, unsigned int total) {
    otaProgress = (progress * 100) / total;
    // Print progress every 10%
    if (millis() - otaLastUpdate > 500) {
      Serial.printf("OTA: %u%%\r", otaProgress);
      otaLastUpdate = millis();
    }
  });

  ArduinoOTA.onError([](ota_error_t error) {
    otaInProgress = false;
    switch (error) {
      case OTA_AUTH_ERROR:    otaError = "Auth Failed";    break;
      case OTA_BEGIN_ERROR:   otaError = "Begin Failed";   break;
      case OTA_CONNECT_ERROR: otaError = "Connect Failed"; break;
      case OTA_RECEIVE_ERROR: otaError = "Receive Failed"; break;
      case OTA_END_ERROR:     otaError = "End Failed";     break;
      default:                otaError = "Unknown";        break;
    }
    Serial.printf("OTA Error[%u]: %s\n", error, otaError.c_str());
  });

  ArduinoOTA.begin();
  Serial.printf("OTA ready on port %d (hostname: %s-%d)\n", OTA_PORT, OTA_HOSTNAME_PREFIX, NODE_ID);
}

// ============================================================
// HTTP Handlers
// ============================================================

void handleStream() {
  WiFiClient client = server.client();
  streamClientActive = true;
  lastActivityTime = millis();  // Reset idle timer

  String header = "HTTP/1.1 200 OK\r\n"
                  "Content-Type: multipart/x-mixed-replace;boundary=" MJPEG_BOUNDARY "\r\n"
                  "\r\n";
  client.print(header);

  unsigned long frameInterval = 1000 / TARGET_FPS;

  while (client.connected()) {
    unsigned long frameStart = millis();

    // During OTA, pause streaming to free bandwidth
    if (otaInProgress) {
      delay(100);
      continue;
    }

    camera_fb_t *fb = nullptr;
    if (xSemaphoreTake(cameraMutex, pdMS_TO_TICKS(200)) == pdTRUE) {
      fb = esp_camera_fb_get();
      xSemaphoreGive(cameraMutex);
    }
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
  // Stream client disconnected
  streamClientActive = false;
}

void handleMotion() {
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
  // Verify HMAC auth for config changes
  if (!verifyHmacAuth(MOTION_PATH)) {
    server.send(403, "application/json", "{\"error\":\"HMAC auth failed\"}");
    return;
  }
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
    memset(refFrame, 0, DETECT_PIXELS);
    motionDetected = false;
    motionScore = 0;
    motionCount = 0;
  }

  Serial.printf("Motion config: pixel=%d, percent=%d%%, enabled=%s\n",
    pixelThreshold, percentThreshold, motionEnabled ? "yes" : "no");

  handleMotion();
}

void handleOtaStatus() {
  char hostname[32];
  snprintf(hostname, sizeof(hostname), "%s-%d", OTA_HOSTNAME_PREFIX, NODE_ID);
  char json[256];
  snprintf(json, sizeof(json),
    "{\"ready\":true,"
    "\"inProgress\":%s,"
    "\"progress\":%d,"
    "\"error\":\"%s\","
    "\"hostname\":\"%s\","
    "\"port\":%d,"
    "\"nodeId\":%d,"
    "\"freeHeap\":%lu,"
    "\"uptime\":%lu}",
    otaInProgress ? "true" : "false",
    otaProgress,
    otaError.c_str(),
    hostname,
    OTA_PORT,
    NODE_ID,
    (unsigned long)ESP.getFreeHeap(),
    millis() / 1000);

  server.send(200, "application/json", json);
}

// HTTP-based OTA upload endpoint (alternative to ArduinoOTA mDNS)
void handleOtaUpload() {
  HTTPUpload &upload = server.upload();

  if (upload.status == UPLOAD_FILE_START) {
    otaInProgress = true;
    otaProgress = 0;
    otaError = "";
    Serial.printf("HTTP OTA Start: %s\n", upload.filename.c_str());
    if (!Update.begin(UPDATE_SIZE_UNKNOWN)) {
      otaError = "Begin failed";
      Serial.println(otaError);
    }
  } else if (upload.status == UPLOAD_FILE_WRITE) {
    if (Update.write(upload.buf, upload.currentSize) != upload.currentSize) {
      otaError = "Write failed";
      Serial.println(otaError);
    }
    otaProgress = (Update.progress() * 100) / Update.size();
  } else if (upload.status == UPLOAD_FILE_END) {
    if (Update.end(true)) {
      otaProgress = 100;
      Serial.printf("HTTP OTA Success: %u bytes\n", upload.totalSize);
    } else {
      otaError = "End failed";
      Serial.println(otaError);
    }
    otaInProgress = false;
  }
}

void handleOtaUploadDone() {
  // Verify HMAC auth for OTA (critical endpoint)
  if (!verifyHmacAuth(OTA_UPLOAD_PATH)) {
    server.send(403, "application/json", "{\"error\":\"HMAC auth failed\"}");
    return;
  }
  if (otaError.length() > 0) {
    char json[128];
    snprintf(json, sizeof(json), "{\"success\":false,\"error\":\"%s\"}", otaError.c_str());
    server.send(500, "application/json", json);
  } else {
    server.send(200, "application/json", "{\"success\":true,\"message\":\"Rebooting...\"}");
    delay(500);
    ESP.restart();
  }
}

void handleRoot() {
  String html = "<html><body>"
                "<h1>GigaSenseHub Camera Node " + String(NODE_ID) + "</h1>"
                "<p>IP: " + WiFi.localIP().toString() + "</p>"
                "<p><a href=\"" STREAM_PATH "\">MJPEG Stream</a></p>"
                "<p><a href=\"" MOTION_PATH "\">Motion Status (JSON)</a></p>"
                "<p><a href=\"" NODE_INFO_PATH "\">Node Info (JSON)</a></p>"
                "<p><a href=\"" OTA_STATUS_PATH "\">OTA Status (JSON)</a></p>"
                "<p>Motion: " + String(motionDetected ? "DETECTED" : "clear") +
                " (" + String(motionScore, 1) + "% changed)</p>"
                "<hr><h2>OTA Firmware Update</h2>"
                "<form method='POST' action='" OTA_UPLOAD_PATH "' enctype='multipart/form-data'>"
                "<input type='file' name='firmware' accept='.bin'>"
                "<input type='submit' value='Upload'>"
                "</form>"
                "</body></html>";
  server.send(200, "text/html", html);
}

void handleNodeInfo() {
  // Any /info request counts as activity (GIGA is scanning us)
  lastActivityTime = millis();
  char json[512];
  const char *pwrStr = powerState == POWER_STATE_LIGHT ? "light" :
                       powerState == POWER_STATE_DEEP  ? "deep" : "active";
  snprintf(json, sizeof(json),
    "{\"nodeId\":%d,"
    "\"firmware\":\"%s\","
    "\"ip\":\"%s\","
    "\"mac\":\"%s\","
    "\"rssi\":%d,"
    "\"freeHeap\":%lu,"
    "\"uptime\":%lu,"
    "\"motionEnabled\":%s,"
    "\"motionCount\":%lu,"
    "\"powerState\":\"%s\","
    "\"cpuMhz\":%d,"
    "\"bootCount\":%d,"
    "\"tasks\":{\"http\":%lu,\"motion\":%lu,\"housekeep\":%lu}}",
    NODE_ID,
    FW_VERSION,
    WiFi.localIP().toString().c_str(),
    WiFi.macAddress().c_str(),
    WiFi.RSSI(),
    (unsigned long)ESP.getFreeHeap(),
    millis() / 1000,
    motionEnabled ? "true" : "false",
    (unsigned long)motionCount,
    pwrStr,
    getCpuFrequencyMhz(),
    bootCount,
    httpTaskWdog, motionTaskWdog, housekeepWdog);
  server.send(200, "application/json", json);
}

// ============================================================
// FreeRTOS Tasks
// ============================================================

// HTTP Server Task — Core 1, Priority 2
// Handles web requests and ArduinoOTA
void httpTask(void *param) {
  (void)param;
  Serial.println("[httpTask] Started on core " + String(xPortGetCoreID()));
  for (;;) {
    server.handleClient();
    ArduinoOTA.handle();
    httpTaskWdog++;
    vTaskDelay(1);  // Yield to other tasks (1 tick = 1ms)
  }
}

// Motion Detection Task — Core 0, Priority 1
// Runs frame differencing at ~4Hz
void motionTask(void *param) {
  (void)param;
  Serial.println("[motionTask] Started on core " + String(xPortGetCoreID()));
  for (;;) {
    if (motionEnabled && !otaInProgress && !streamClientActive) {
      if (xSemaphoreTake(cameraMutex, pdMS_TO_TICKS(100)) == pdTRUE) {
        runMotionDetection();
        xSemaphoreGive(cameraMutex);
        if (motionDetected) {
          lastActivityTime = millis();
          powerState = POWER_STATE_ACTIVE;
        }
      }
    }
    motionTaskWdog++;
    vTaskDelay(pdMS_TO_TICKS(250));  // 4Hz
  }
}

// Housekeeping Task — Core 0, Priority 1
// WiFi reconnection + power management state machine
void housekeepTask(void *param) {
  (void)param;
  Serial.println("[housekeepTask] Started on core " + String(xPortGetCoreID()));
  unsigned long lastReconnect = 0;
  for (;;) {
    // WiFi reconnection
    if (WiFi.status() != WL_CONNECTED && millis() - lastReconnect > 5000) {
      Serial.println("WiFi lost — reconnecting...");
      WiFi.begin(AP_SSID, AP_PASSWORD);
      lastReconnect = millis();
    }

    // Power management state machine
    unsigned long idle = millis() - lastActivityTime;

    if (powerState == POWER_STATE_ACTIVE) {
      if (!streamClientActive && idle >= IDLE_DEEP_SLEEP_MS && !otaInProgress) {
        Serial.printf("Deep sleep: idle %lums\n", idle);
        Serial.flush();
        esp_sleep_enable_timer_wakeup(DEEP_SLEEP_WAKE_US);
        esp_deep_sleep_start();
      }
      else if (!streamClientActive && idle >= IDLE_LIGHT_SLEEP_MS && !otaInProgress) {
        if (powerState != POWER_STATE_LIGHT) {
          Serial.printf("Light sleep mode: idle %lums\n", idle);
          powerState = POWER_STATE_LIGHT;
          setCpuFrequencyMhz(80);
        }
      }
    }
    else if (powerState == POWER_STATE_LIGHT) {
      if (motionDetected || streamClientActive) {
        Serial.println("Waking from light sleep");
        powerState = POWER_STATE_ACTIVE;
        setCpuFrequencyMhz(240);
        lastActivityTime = millis();
      }
    }

    housekeepWdog++;
    vTaskDelay(pdMS_TO_TICKS(1000));  // 1Hz
  }
}

// ============================================================
// Setup & Loop
// ============================================================

void setup() {
  Serial.begin(115200);
  delay(1000);
  Serial.printf("\n=== GigaSenseHub Camera Node %d ===\n", NODE_ID);
  Serial.println("Mode: STA (connecting to GIGA AP)");
  Serial.printf("Firmware: %s (built %s %s)\n", FW_VERSION, __DATE__, __TIME__);

  // Allocate motion detection buffers in PSRAM
  refFrame = (uint8_t *)ps_calloc(DETECT_PIXELS, 1);
  curFrame = (uint8_t *)ps_calloc(DETECT_PIXELS, 1);
  if (!refFrame || !curFrame) {
    Serial.println("ERROR: Failed to allocate motion buffers in PSRAM");
    while (true) { delay(1000); }
  }
  Serial.printf("Motion buffers: %d bytes each in PSRAM\n", DETECT_PIXELS);

  // Connect to GIGA R1 Access Point as station
  char hostname[32];
  snprintf(hostname, sizeof(hostname), "%s-%d", OTA_HOSTNAME_PREFIX, NODE_ID);
  WiFi.setHostname(hostname);
  WiFi.mode(WIFI_STA);
  WiFi.begin(AP_SSID, AP_PASSWORD);
  Serial.printf("Connecting to AP '%s'...\n", AP_SSID);

  int attempts = 0;
  while (WiFi.status() != WL_CONNECTED && attempts < 30) {
    delay(500);
    Serial.print(".");
    attempts++;
  }
  if (WiFi.status() == WL_CONNECTED) {
    Serial.printf("\nWiFi connected — IP: %s (RSSI: %d)\n",
      WiFi.localIP().toString().c_str(), WiFi.RSSI());
  } else {
    Serial.println("\nWiFi connect FAILED — continuing without network");
  }

  initCamera();
  setupOTA();

  server.on("/", handleRoot);
  server.on(STREAM_PATH, HTTP_GET, handleStream);
  server.on(MOTION_PATH, HTTP_GET, handleMotion);
  server.on(MOTION_PATH, HTTP_POST, handleMotionConfig);
  server.on(NODE_INFO_PATH, HTTP_GET, handleNodeInfo);
  server.on(OTA_STATUS_PATH, HTTP_GET, handleOtaStatus);
  server.on(OTA_UPLOAD_PATH, HTTP_POST, handleOtaUploadDone, handleOtaUpload);
  // Collect auth headers for HMAC verification
  const char *authHeaders[] = {AUTH_HEADER, AUTH_NONCE_HEADER};
  server.collectHeaders(authHeaders, 2);
  server.begin();
  Serial.println("HTTP server started");

  // Power management init
  bootCount++;
  wakeReason = esp_sleep_get_wakeup_cause();
  lastActivityTime = millis();
  powerState = POWER_STATE_ACTIVE;
  Serial.printf("Boot #%d | Wake reason: %d\n", bootCount, wakeReason);

  // Create camera mutex (stream and motion both use camera)
  cameraMutex = xSemaphoreCreateMutex();

  // Spawn FreeRTOS tasks
  //   httpTask:      Core 1, prio 2, 8KB stack (handles stream + OTA)
  //   motionTask:    Core 0, prio 1, 8KB stack (JPEG decode needs RAM)
  //   housekeepTask: Core 0, prio 1, 4KB stack (WiFi + power mgmt)
  xTaskCreatePinnedToCore(httpTask,      "http",      8192, nullptr, 2, &httpTaskHandle,      1);
  xTaskCreatePinnedToCore(motionTask,    "motion",    8192, nullptr, 1, &motionTaskHandle,    0);
  xTaskCreatePinnedToCore(housekeepTask, "housekeep", 4096, nullptr, 1, &housekeepTaskHandle, 0);

  Serial.println("FreeRTOS tasks spawned: http(C1) motion(C0) housekeep(C0)");
}

static unsigned long lastWdogPrint = 0;

void loop() {
  // Main loop is now a watchdog monitor — all work is in FreeRTOS tasks
  if (millis() - lastWdogPrint >= 10000) {
    Serial.printf("[watchdog] http=%lu motion=%lu housekeep=%lu | heap=%lu\n",
      httpTaskWdog, motionTaskWdog, housekeepWdog,
      (unsigned long)ESP.getFreeHeap());
    httpTaskWdog = 0;
    motionTaskWdog = 0;
    housekeepWdog = 0;
    lastWdogPrint = millis();
  }
  vTaskDelay(pdMS_TO_TICKS(1000));
}
