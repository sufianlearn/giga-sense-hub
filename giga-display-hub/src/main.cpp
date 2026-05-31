// GigaSenseHub — Home Security Dashboard
// LVGL 9 on Arduino GIGA R1 + Display Shield (800x480)
// Live ESP32-S3 camera feed + GIGA BMI270 IMU sensor data

#include <Arduino.h>
#include <WiFi.h>
#include <Arduino_H7_Video.h>
#include <Arduino_GigaDisplayTouch.h>
#include <Arduino_BMI270_BMM150.h>
BoschSensorClass myIMU(Wire1);
#include <lvgl.h>
#include "SDRAM.h"
#include "protocol.h"
#include "tjpgd.h"

Arduino_H7_Video display(800, 480, GigaDisplayShield);
Arduino_GigaDisplayTouch touchCtrl;

// --- Colors ---
#define C_BG          lv_color_hex(0x0D1117)
#define C_CARD        lv_color_hex(0x161B22)
#define C_ACCENT      lv_color_hex(0x58A6FF)
#define C_GREEN       lv_color_hex(0x3FB950)
#define C_RED         lv_color_hex(0xF85149)
#define C_ORANGE      lv_color_hex(0xD29922)
#define C_TEXT        lv_color_hex(0xE6EDF3)
#define C_TEXT_DIM    lv_color_hex(0x8B949E)
#define C_BORDER      lv_color_hex(0x30363D)

// --- State ---
WiFiClient streamClient;
bool streamConnected = false;
bool wifiConnected = false;
unsigned long lastFpsCalc = 0;
unsigned int frameCount = 0;
float currentFps = 0;

float accelX = 0, accelY = 0, accelZ = 0;
float gyroX = 0, gyroY = 0, gyroZ = 0;
bool imuReady = false;
bool imuOnWire1 = false;

bool frontDoorLocked = true;
bool backDoorLocked = true;
bool garageLocked = true;
bool windowLiving = false;
bool windowBedroom = false;
bool windowKitchen = false;

#define JPEG_BUF_SIZE (32 * 1024)
static uint8_t jpegBuf[JPEG_BUF_SIZE];
static int jpegLen = 0;

// --- Video frame buffer in SDRAM ---
#define CAM_W FRAME_WIDTH
#define CAM_H FRAME_HEIGHT
static uint16_t *camFrameBuf = nullptr;
static lv_image_dsc_t camImgDsc;
static lv_obj_t *camImg = nullptr;

// --- LVGL UI elements ---
static lv_obj_t *lbl_fps;
static lv_obj_t *lbl_stream_status;
static lv_obj_t *sw_front_door, *sw_back_door, *sw_garage;
static lv_obj_t *led_front, *led_back, *led_garage;
static lv_obj_t *led_win_living, *led_win_bedroom, *led_win_kitchen;
static lv_obj_t *lbl_win_living, *lbl_win_bedroom, *lbl_win_kitchen;
static lv_obj_t *lbl_accel, *lbl_gyro, *lbl_motion, *lbl_uptime;
static lv_obj_t *lbl_status_bar;

// --- JPEG decode into SDRAM framebuffer ---

struct JpegSession { const uint8_t *data; int len; int pos; };

static unsigned int tjpg_input(JDEC *jd, uint8_t *buf, unsigned int n) {
  JpegSession *s = (JpegSession *)jd->device;
  int rem = s->len - s->pos;
  if ((int)n > rem) n = rem;
  if (buf) memcpy(buf, s->data + s->pos, n);
  s->pos += n;
  return n;
}

static int tjpg_output(JDEC *jd, void *bmp, JRECT *r) {
  uint16_t *px = (uint16_t *)bmp;
  if (!camFrameBuf) return 0;
  for (int y = r->top; y <= r->bottom; y++) {
    for (int x = r->left; x <= r->right; x++) {
      if ((unsigned)x < CAM_W && (unsigned)y < CAM_H) {
        camFrameBuf[y * CAM_W + x] = *px;
      }
      px++;
    }
  }
  return 1;
}

#ifndef TJPGD_WORKSPACE_SIZE
#define TJPGD_WORKSPACE_SIZE 3500
#endif
static uint8_t tjWork[TJPGD_WORKSPACE_SIZE];

void decodeFrame() {
  JpegSession sess = { jpegBuf, jpegLen, 0 };
  JDEC jd;
  if (jd_prepare(&jd, tjpg_input, tjWork, TJPGD_WORKSPACE_SIZE, &sess) == JDR_OK) {
    jd_decomp(&jd, tjpg_output, 0);
    if (camImg) {
      lv_image_set_src(camImg, &camImgDsc);
      lv_obj_invalidate(camImg);
    }
  }
}

// --- Lock callback ---
static void lock_cb(lv_event_t *e) {
  lv_obj_t *sw = (lv_obj_t *)lv_event_get_target(e);
  bool on = lv_obj_has_state(sw, LV_STATE_CHECKED);
  lv_obj_t *led = nullptr;
  if (sw == sw_front_door) { frontDoorLocked = on; led = led_front; }
  else if (sw == sw_back_door) { backDoorLocked = on; led = led_back; }
  else if (sw == sw_garage) { garageLocked = on; led = led_garage; }
  if (led) {
    lv_led_set_color(led, on ? C_GREEN : C_RED);
    lv_led_set_brightness(led, on ? 200 : 255);
  }
}

// --- UI Helpers ---

static lv_obj_t* mkCard(lv_obj_t *p, int w, int h) {
  lv_obj_t *c = lv_obj_create(p);
  lv_obj_set_size(c, w, h);
  lv_obj_set_style_bg_color(c, C_CARD, 0);
  lv_obj_set_style_bg_opa(c, LV_OPA_COVER, 0);
  lv_obj_set_style_border_color(c, C_BORDER, 0);
  lv_obj_set_style_border_width(c, 1, 0);
  lv_obj_set_style_radius(c, 8, 0);
  lv_obj_set_style_pad_all(c, 10, 0);
  lv_obj_clear_flag(c, LV_OBJ_FLAG_SCROLLABLE);
  return c;
}

static lv_obj_t* mkTitle(lv_obj_t *p, const char *t) {
  lv_obj_t *l = lv_label_create(p);
  lv_label_set_text(l, t);
  lv_obj_set_style_text_color(l, C_ACCENT, 0);
  return l;
}

static lv_obj_t* mkDim(lv_obj_t *p, const char *t) {
  lv_obj_t *l = lv_label_create(p);
  lv_label_set_text(l, t);
  lv_obj_set_style_text_color(l, C_TEXT_DIM, 0);
  return l;
}

static lv_obj_t* mkRow(lv_obj_t *p, int h) {
  lv_obj_t *r = lv_obj_create(p);
  lv_obj_set_size(r, lv_pct(100), h);
  lv_obj_set_style_bg_opa(r, LV_OPA_TRANSP, 0);
  lv_obj_set_style_border_width(r, 0, 0);
  lv_obj_set_style_pad_all(r, 0, 0);
  lv_obj_set_flex_flow(r, LV_FLEX_FLOW_ROW);
  lv_obj_set_flex_align(r, LV_FLEX_ALIGN_SPACE_BETWEEN, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
  lv_obj_clear_flag(r, LV_OBJ_FLAG_SCROLLABLE);
  return r;
}

static void mkDoorRow(lv_obj_t *p, const char *name, lv_obj_t **sw_out, lv_obj_t **led_out, bool locked) {
  lv_obj_t *row = mkRow(p, 34);
  *led_out = lv_led_create(row);
  lv_led_set_color(*led_out, locked ? C_GREEN : C_RED);
  lv_led_set_brightness(*led_out, locked ? 200 : 255);
  lv_obj_set_size(*led_out, 12, 12);
  lv_obj_t *lbl = lv_label_create(row);
  lv_label_set_text(lbl, name);
  lv_obj_set_style_text_color(lbl, C_TEXT, 0);
  lv_obj_set_flex_grow(lbl, 1);
  *sw_out = lv_switch_create(row);
  if (locked) lv_obj_add_state(*sw_out, LV_STATE_CHECKED);
  lv_obj_set_style_bg_color(*sw_out, C_RED, 0);
  lv_obj_set_style_bg_color(*sw_out, C_GREEN, LV_PART_INDICATOR | LV_STATE_CHECKED);
  lv_obj_add_event_cb(*sw_out, lock_cb, LV_EVENT_VALUE_CHANGED, nullptr);
}

static void mkWinRow(lv_obj_t *p, const char *name, lv_obj_t **led_out, lv_obj_t **lbl_out, bool open) {
  lv_obj_t *row = mkRow(p, 26);
  lv_obj_set_style_pad_column(row, 8, 0);
  lv_obj_set_flex_align(row, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
  *led_out = lv_led_create(row);
  lv_led_set_color(*led_out, open ? C_ORANGE : C_GREEN);
  lv_led_set_brightness(*led_out, 200);
  lv_obj_set_size(*led_out, 10, 10);
  lv_obj_t *n = lv_label_create(row);
  lv_label_set_text(n, name);
  lv_obj_set_style_text_color(n, C_TEXT, 0);
  *lbl_out = lv_label_create(row);
  lv_label_set_text(*lbl_out, open ? "OPEN" : "CLOSED");
  lv_obj_set_style_text_color(*lbl_out, open ? C_ORANGE : C_GREEN, 0);
}

// --- Build UI ---

void buildUI() {
  lv_obj_t *scr = lv_scr_act();
  lv_obj_set_style_bg_color(scr, C_BG, 0);

  // Top bar
  lv_obj_t *top = lv_obj_create(scr);
  lv_obj_set_size(top, 800, 34);
  lv_obj_set_pos(top, 0, 0);
  lv_obj_set_style_bg_color(top, C_CARD, 0);
  lv_obj_set_style_bg_opa(top, LV_OPA_COVER, 0);
  lv_obj_set_style_border_width(top, 0, 0);
  lv_obj_set_style_radius(top, 0, 0);
  lv_obj_set_style_pad_hor(top, 12, 0);
  lv_obj_clear_flag(top, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_set_flex_flow(top, LV_FLEX_FLOW_ROW);
  lv_obj_set_flex_align(top, LV_FLEX_ALIGN_SPACE_BETWEEN, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);

  lv_obj_t *title = lv_label_create(top);
  lv_label_set_text(title, LV_SYMBOL_HOME "  GigaSenseHub Security");
  lv_obj_set_style_text_color(title, C_TEXT, 0);

  lbl_status_bar = lv_label_create(top);
  lv_label_set_text(lbl_status_bar, LV_SYMBOL_WIFI " Connecting...");
  lv_obj_set_style_text_color(lbl_status_bar, C_ORANGE, 0);

  // === LEFT COLUMN ===

  // Camera card
  lv_obj_t *cc = mkCard(scr, 370, 288);
  lv_obj_set_pos(cc, 6, 40);
  lv_obj_set_flex_flow(cc, LV_FLEX_FLOW_COLUMN);
  lv_obj_set_style_pad_gap(cc, 4, 0);

  lv_obj_t *ch = mkRow(cc, 18);
  mkTitle(ch, LV_SYMBOL_VIDEO "  Camera 1");
  lbl_fps = lv_label_create(ch);
  lv_label_set_text(lbl_fps, "-- fps");
  lv_obj_set_style_text_color(lbl_fps, C_GREEN, 0);

  // Live video image from SDRAM buffer
  camFrameBuf = (uint16_t *)SDRAM.malloc(CAM_W * CAM_H * sizeof(uint16_t));
  if (camFrameBuf) {
    memset(camFrameBuf, 0, CAM_W * CAM_H * sizeof(uint16_t));
    camImgDsc.header.w = CAM_W;
    camImgDsc.header.h = CAM_H;
    camImgDsc.header.cf = LV_COLOR_FORMAT_RGB565;
    camImgDsc.header.stride = CAM_W * 2;
    camImgDsc.data_size = CAM_W * CAM_H * 2;
    camImgDsc.data = (const uint8_t *)camFrameBuf;

    camImg = lv_image_create(cc);
    lv_image_set_src(camImg, &camImgDsc);
    lv_obj_set_style_radius(camImg, 4, 0);
    lv_obj_set_style_clip_corner(camImg, true, 0);
    Serial.println("Camera canvas: SDRAM OK");
  } else {
    lv_obj_t *ph = lv_label_create(cc);
    lv_label_set_text(ph, "Camera buffer alloc failed");
    lv_obj_set_style_text_color(ph, C_RED, 0);
    Serial.println("Camera canvas: SDRAM FAIL");
  }

  lbl_stream_status = lv_label_create(cc);
  lv_label_set_text(lbl_stream_status, "Waiting for stream...");
  lv_obj_set_style_text_color(lbl_stream_status, C_TEXT_DIM, 0);

  // IMU Sensor card (GIGA's own BMI270)
  lv_obj_t *sc = mkCard(scr, 370, 128);
  lv_obj_set_pos(sc, 6, 334);
  lv_obj_set_flex_flow(sc, LV_FLEX_FLOW_COLUMN);
  lv_obj_set_style_pad_gap(sc, 3, 0);

  mkTitle(sc, LV_SYMBOL_GPS "  GIGA IMU (BMI270)");
  lbl_accel  = mkDim(sc, "Accel: --");
  lbl_gyro   = mkDim(sc, "Gyro:  --");
  lbl_motion = mkDim(sc, "Motion: Idle");
  lbl_uptime = mkDim(sc, "Uptime: 0s");

  // === RIGHT COLUMN ===

  // Door locks
  lv_obj_t *lc = mkCard(scr, 412, 178);
  lv_obj_set_pos(lc, 382, 40);
  lv_obj_set_flex_flow(lc, LV_FLEX_FLOW_COLUMN);
  lv_obj_set_style_pad_gap(lc, 6, 0);
  mkTitle(lc, LV_SYMBOL_CLOSE "  Door Locks");
  mkDoorRow(lc, "Front Door", &sw_front_door, &led_front, frontDoorLocked);
  mkDoorRow(lc, "Back Door",  &sw_back_door,  &led_back,  backDoorLocked);
  mkDoorRow(lc, "Garage",     &sw_garage,     &led_garage, garageLocked);

  // Windows
  lv_obj_t *wc = mkCard(scr, 412, 144);
  lv_obj_set_pos(wc, 382, 224);
  lv_obj_set_flex_flow(wc, LV_FLEX_FLOW_COLUMN);
  lv_obj_set_style_pad_gap(wc, 4, 0);
  mkTitle(wc, LV_SYMBOL_WARNING "  Window Sensors");
  mkWinRow(wc, "Living Room", &led_win_living,  &lbl_win_living,  windowLiving);
  mkWinRow(wc, "Bedroom",     &led_win_bedroom, &lbl_win_bedroom, windowBedroom);
  mkWinRow(wc, "Kitchen",     &led_win_kitchen, &lbl_win_kitchen, windowKitchen);

  // System card
  lv_obj_t *sysc = mkCard(scr, 412, 92);
  lv_obj_set_pos(sysc, 382, 374);
  lv_obj_set_flex_flow(sysc, LV_FLEX_FLOW_COLUMN);
  lv_obj_set_style_pad_gap(sysc, 4, 0);
  mkTitle(sysc, LV_SYMBOL_SETTINGS "  System");
  lv_obj_t *ar = mkRow(sysc, 26);
  lv_obj_set_style_pad_column(ar, 8, 0);
  lv_obj_set_flex_align(ar, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
  lv_obj_t *al = lv_led_create(ar);
  lv_led_set_color(al, C_GREEN);
  lv_led_set_brightness(al, 200);
  lv_obj_set_size(al, 10, 10);
  lv_obj_t *at = lv_label_create(ar);
  lv_label_set_text(at, "System Armed - All Active");
  lv_obj_set_style_text_color(at, C_GREEN, 0);
}

// --- WiFi ---
void connectWiFi() {
  WiFi.begin(AP_SSID, AP_PASSWORD);
  int att = 0;
  while (WiFi.status() != WL_CONNECTED && att < 30) { delay(500); att++; }
  wifiConnected = (WiFi.status() == WL_CONNECTED);
  if (wifiConnected) { Serial.print("WiFi OK: "); Serial.println(WiFi.localIP()); }
}

// --- Stream ---
bool connectStream() {
  if (streamClient.connected()) return true;
  IPAddress ip; ip.fromString(ESP32_IP);
  if (!streamClient.connect(ip, STREAM_PORT)) return false;
  streamClient.print("GET " STREAM_PATH " HTTP/1.1\r\nHost: " ESP32_IP "\r\nConnection: keep-alive\r\n\r\n");
  unsigned long t = millis() + 5000;
  while (millis() < t) {
    if (streamClient.available()) {
      if (streamClient.readStringUntil('\n').startsWith("--" MJPEG_BOUNDARY)) {
        streamConnected = true; return true;
      }
    }
  }
  streamClient.stop(); return false;
}

bool readFrame() {
  if (!streamClient.connected()) { streamConnected = false; return false; }
  int clen = -1;
  unsigned long t = millis() + 3000;
  while (millis() < t) {
    if (!streamClient.available()) { delay(1); continue; }
    String l = streamClient.readStringUntil('\n'); l.trim();
    if (l.length() == 0) break;
    if (l.startsWith("Content-Length:")) clen = l.substring(15).toInt();
  }
  if (clen <= 0 || clen > JPEG_BUF_SIZE) return false;
  int rd = 0; t = millis() + 3000;
  while (rd < clen && millis() < t) {
    if (streamClient.available()) {
      int g = streamClient.read(jpegBuf + rd, min((int)streamClient.available(), clen - rd));
      if (g > 0) rd += g;
    } else delay(1);
  }
  if (rd != clen) return false;
  jpegLen = clen;
  t = millis() + 1000;
  while (millis() < t) {
    if (streamClient.available()) {
      String l = streamClient.readStringUntil('\n'); l.trim();
      if (l.startsWith("--" MJPEG_BOUNDARY)) break;
    } else delay(1);
  }
  return true;
}

// --- Update UI ---
void updateUI() {
  char buf[64];
  snprintf(buf, sizeof(buf), "%.1f fps", currentFps);
  lv_label_set_text(lbl_fps, buf);

  lv_label_set_text(lbl_stream_status, streamConnected ? LV_SYMBOL_PLAY " LIVE" : "Offline");
  lv_obj_set_style_text_color(lbl_stream_status, streamConnected ? C_GREEN : C_RED, 0);

  if (wifiConnected) {
    snprintf(buf, sizeof(buf), LV_SYMBOL_WIFI " %s | %s",
             WiFi.localIP().toString().c_str(), streamConnected ? "LIVE" : "NO STREAM");
    lv_label_set_text(lbl_status_bar, buf);
    lv_obj_set_style_text_color(lbl_status_bar, streamConnected ? C_GREEN : C_ORANGE, 0);
  }

  // GIGA IMU data — always update labels
  snprintf(buf, sizeof(buf), "Accel: X:%.2f  Y:%.2f  Z:%.2f g", accelX, accelY, accelZ);
  lv_label_set_text(lbl_accel, buf);
  snprintf(buf, sizeof(buf), "Gyro:  X:%.1f  Y:%.1f  Z:%.1f dps", gyroX, gyroY, gyroZ);
  lv_label_set_text(lbl_gyro, buf);

  float totalAccel = abs(accelX) + abs(accelY) + abs(accelZ);
  float totalGyro = abs(gyroX) + abs(gyroY) + abs(gyroZ);
  const char *motion;
  lv_color_t motionColor;
  if (totalGyro > 100 || totalAccel > 2.0f) {
    motion = "Motion: ALERT!"; motionColor = C_RED;
  } else if (totalGyro > 30 || totalAccel > 1.3f) {
    motion = "Motion: Vibration"; motionColor = C_ORANGE;
  } else {
    motion = imuReady ? "Motion: Idle" : "IMU: Not detected";
    motionColor = imuReady ? C_GREEN : C_RED;
  }
  lv_label_set_text(lbl_motion, motion);
  lv_obj_set_style_text_color(lbl_motion, motionColor, 0);

  unsigned long secs = millis() / 1000;
  snprintf(buf, sizeof(buf), "Uptime: %lum %lus", secs / 60, secs % 60);
  lv_label_set_text(lbl_uptime, buf);

  windowLiving = (totalAccel > 1.5f);
  windowKitchen = (abs(gyroX) > 50.0f);
  windowBedroom = (abs(gyroY) > 50.0f);

  auto updWin = [](lv_obj_t *led, lv_obj_t *lbl, bool open) {
    lv_led_set_color(led, open ? C_ORANGE : C_GREEN);
    lv_label_set_text(lbl, open ? "OPEN" : "CLOSED");
    lv_obj_set_style_text_color(lbl, open ? C_ORANGE : C_GREEN, 0);
  };
  updWin(led_win_living,  lbl_win_living,  windowLiving);
  updWin(led_win_bedroom, lbl_win_bedroom, windowBedroom);
  updWin(led_win_kitchen, lbl_win_kitchen, windowKitchen);
}

// --- Setup ---
void setup() {
  Serial.begin(115200); delay(1000);
  Serial.println("\n=== GigaSenseHub Security ===");
  display.begin();
  Serial.println("Display OK");
  touchCtrl.begin();
  if (myIMU.begin()) {
    myIMU.setContinuousMode();
    imuReady = true; imuOnWire1 = true;
    Serial.println("IMU OK (Wire1)");
  } else if (IMU.begin()) {
    IMU.setContinuousMode();
    imuReady = true;
    Serial.println("IMU OK (Wire)");
  } else {
    Serial.println("IMU failed");
  }
  buildUI();
  lv_timer_handler();
  connectWiFi();
  delay(2000);
  lastFpsCalc = millis();
  Serial.println("Ready");
}

// --- Loop ---
void loop() {
  unsigned long now = millis();
  lv_timer_handler();

  if (WiFi.status() != WL_CONNECTED) {
    wifiConnected = false; streamConnected = false;
    connectWiFi(); return;
  }
  if (!streamConnected) { connectStream(); if (!streamConnected) { delay(2000); return; } }

  if (streamConnected && readFrame()) {
    if (camFrameBuf) decodeFrame();
    frameCount++;
  }

  if (now - lastFpsCalc >= 1000) {
    currentFps = frameCount * 1000.0f / (now - lastFpsCalc);
    frameCount = 0; lastFpsCalc = now;
    char lb[120];
    snprintf(lb, sizeof(lb), "FPS: %.1f | Stream: %s | IMU:%s A:%.2f,%.2f,%.2f",
             currentFps, streamConnected ? "OK" : "NO",
             imuReady ? "Y" : "N", accelX, accelY, accelZ);
    Serial.println(lb);
  }

  static unsigned long lastIMU = 0;
  if (now - lastIMU >= 50) {
    BoschSensorClass &imu = imuOnWire1 ? myIMU : IMU;
    int accAvail = imu.accelerationAvailable();
    int gyroAvail = imu.gyroscopeAvailable();
    if (accAvail) imu.readAcceleration(accelX, accelY, accelZ);
    if (gyroAvail) imu.readGyroscope(gyroX, gyroY, gyroZ);
    // Debug: log availability once per second
    static unsigned long lastIMUDbg = 0;
    if (now - lastIMUDbg >= 2000) {
      char db[80];
      snprintf(db, sizeof(db), "IMU avail: acc=%d gyro=%d vals=%.2f,%.2f,%.2f",
               accAvail, gyroAvail, accelX, accelY, accelZ);
      Serial.println(db);
      lastIMUDbg = now;
    }
    lastIMU = now;
  }

  static unsigned long lastUI = 0;
  if (now - lastUI >= 500) { updateUI(); lastUI = now; }
}
