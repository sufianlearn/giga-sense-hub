// GigaSenseHub — Multi-Page Home Security App
// LVGL 9 on Arduino GIGA R1 + Display Shield (800x480)
// Pages: Login (PIN) → Dashboard (cameras/locks/sensors) → Settings

#include <Arduino.h>
#include <WiFi.h>
#include <Arduino_H7_Video.h>
#include <Arduino_GigaDisplayTouch.h>
#include <Arduino_BMI270_BMM150.h>
#include <lvgl.h>
#include "SDRAM.h"
#include "protocol.h"
#include "tjpgd.h"
#include "storage.h"
#include "eventlog.h"

Arduino_H7_Video display(800, 480, GigaDisplayShield);
Arduino_GigaDisplayTouch touchCtrl;
BoschSensorClass myIMU(Wire1);

// ============================================================
// Theme Colors
// ============================================================
#define C_BG          lv_color_hex(0x0D1117)
#define C_CARD        lv_color_hex(0x161B22)
#define C_CARD_HOVER  lv_color_hex(0x1C2129)
#define C_ACCENT      lv_color_hex(0x58A6FF)
#define C_GREEN       lv_color_hex(0x3FB950)
#define C_RED         lv_color_hex(0xF85149)
#define C_ORANGE      lv_color_hex(0xD29922)
#define C_TEXT        lv_color_hex(0xE6EDF3)
#define C_TEXT_DIM    lv_color_hex(0x8B949E)
#define C_BORDER      lv_color_hex(0x30363D)
#define C_PIN_BTN     lv_color_hex(0x21262D)
#define C_PIN_BTN_PR  lv_color_hex(0x30363D)

// ============================================================
// App State
// ============================================================
enum AppPage { PAGE_LOGIN, PAGE_DASHBOARD, PAGE_SETTINGS, PAGE_LOG };
AppPage currentPage = PAGE_LOGIN;

// Settings are now persistent via storage.h (FlashIAP).
// storageSettings() returns a reference to the RAM-cached PersistentSettings.
#define settings storageSettings()

// WiFi / Stream
WiFiClient streamClient;
WiFiClient motionClient;
bool streamConnected = false;
bool wifiConnected = false;
unsigned long lastFpsCalc = 0;
unsigned int frameCount = 0;
float currentFps = 0;

// IMU
float accelX = 0, accelY = 0, accelZ = 0;
float gyroX = 0, gyroY = 0, gyroZ = 0;
bool imuReady = false;
bool imuOnWire1 = false;

// Door / Window state (doors now persisted via storage.h)
// doorLocked[] is accessed via settings.doorLocked[]
const char *doorNames[3] = {"Front Door", "Back Door", "Garage"};
bool windowOpen[3] = {false, false, false};
const char *windowNames[3] = {"Living Room", "Bedroom", "Kitchen"};

// Auto-lock
unsigned long lastActivity = 0;

// JPEG
#define JPEG_BUF_SIZE (32 * 1024)
static uint8_t jpegBuf[JPEG_BUF_SIZE];
static int jpegLen = 0;

// Video buffer
#define CAM_W FRAME_WIDTH
#define CAM_H FRAME_HEIGHT
static uint16_t *camFrameBuf = nullptr;
static lv_image_dsc_t camImgDsc;

// ============================================================
// LVGL Screen Objects
// ============================================================
static lv_obj_t *scrLogin = nullptr;
static lv_obj_t *scrDashboard = nullptr;
static lv_obj_t *scrSettings = nullptr;
static lv_obj_t *scrLog = nullptr;

// Login page
static lv_obj_t *pinDots[4];
static lv_obj_t *lblLoginError;
static char pinEntry[5] = "";
static int pinPos = 0;

// Dashboard page
static lv_obj_t *camImg = nullptr;
static lv_obj_t *lblFps;
static lv_obj_t *lblStreamStatus;
static lv_obj_t *swDoor[3];
static lv_obj_t *ledDoor[3];
static lv_obj_t *ledWin[3];
static lv_obj_t *lblWin[3];
static lv_obj_t *lblAccel, *lblGyro, *lblMotion, *lblUptime;
static lv_obj_t *lblCamMotion;  // Camera-based motion from ESP32
static lv_obj_t *lblStatusBar;

// Settings page
static lv_obj_t *lblSettingsWifi;
static lv_obj_t *sliderMotion, *lblMotionVal;
static lv_obj_t *sliderAutoLock, *lblAutoLockVal;
static lv_obj_t *sliderFps, *lblFpsVal;
static lv_obj_t *sliderQuality, *lblQualityVal;
static lv_obj_t *lblSysUptime, *lblSysMem, *lblSysFw;

// PIN change in settings
static lv_obj_t *taNewPin;

// Activity Log page
static lv_obj_t *logList = nullptr;
static lv_obj_t *lblLogCount;

// Camera motion detection state (polled from ESP32)
static bool     camMotionDetected = false;
static float    camMotionScore    = 0.0f;
static uint32_t camMotionCount    = 0;
static unsigned long lastCamPoll  = 0;

// Multi-node registry
struct CameraNode {
  bool        active;
  IPAddress   ip;
  int         nodeId;
  char        firmware[16];
  int         rssi;
  bool        motionDetected;
  float       motionScore;
  WiFiClient  streamClient;
  bool        streamConnected;
};
static CameraNode nodes[MAX_NODES];
static int activeNodeCount = 0;
static int activeNodeIdx   = 0;  // Currently displayed node (for stream)
static unsigned long lastNodeScan = 0;

// ============================================================
// JPEG Decoder
// ============================================================
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
  for (int y = r->top; y <= r->bottom; y++)
    for (int x = r->left; x <= r->right; x++) {
      if ((unsigned)x < CAM_W && (unsigned)y < CAM_H) {
        uint16_t c = *px;
        camFrameBuf[y * CAM_W + x] = (c >> 8) | (c << 8);
      }
      px++;
    }
  return 1;
}

#ifndef TJPGD_WORKSPACE_SIZE
#define TJPGD_WORKSPACE_SIZE 3500
#endif
static uint8_t tjWork[TJPGD_WORKSPACE_SIZE];

void decodeFrame() {
  JpegSession sess = {jpegBuf, jpegLen, 0};
  JDEC jd;
  if (jd_prepare(&jd, tjpg_input, tjWork, TJPGD_WORKSPACE_SIZE, &sess) == JDR_OK) {
    jd_decomp(&jd, tjpg_output, 0);
    if (camImg) {
      lv_image_set_src(camImg, &camImgDsc);
      lv_obj_invalidate(camImg);
    }
  }
}

// ============================================================
// UI Helpers
// ============================================================
static lv_obj_t *mkCard(lv_obj_t *p, int w, int h) {
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

static lv_obj_t *mkTitle(lv_obj_t *p, const char *t) {
  lv_obj_t *l = lv_label_create(p);
  lv_label_set_text(l, t);
  lv_obj_set_style_text_color(l, C_ACCENT, 0);
  return l;
}

static lv_obj_t *mkRow(lv_obj_t *p, int h) {
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

static void touchActivity() { lastActivity = millis(); }

// ============================================================
// PAGE 1: LOGIN
// ============================================================

static void pinAppend(char c) {
  if (pinPos >= 4) return;
  pinEntry[pinPos++] = c;
  pinEntry[pinPos] = '\0';

  for (int i = 0; i < 4; i++) {
    lv_obj_set_style_bg_color(pinDots[i], i < pinPos ? C_ACCENT : C_BORDER, 0);
    lv_obj_set_style_bg_opa(pinDots[i], LV_OPA_COVER, 0);
  }

  if (pinPos == 4) {
    if (strcmp(pinEntry, settings.pin) == 0) {
      lv_label_set_text(lblLoginError, "");
      pinPos = 0;
      pinEntry[0] = '\0';
      for (int i = 0; i < 4; i++)
        lv_obj_set_style_bg_color(pinDots[i], C_BORDER, 0);

      lastActivity = millis();
      currentPage = PAGE_DASHBOARD;
      lv_screen_load_anim(scrDashboard, LV_SCR_LOAD_ANIM_MOVE_LEFT, 300, 0, false);
      eventInfo("User logged in");
      Serial.println("Login OK -> Dashboard");
    } else {
      lv_label_set_text(lblLoginError, "Wrong PIN");
      lv_obj_set_style_text_color(lblLoginError, C_RED, 0);
      eventAlert("Failed login attempt");
      for (int i = 0; i < 4; i++)
        lv_obj_set_style_bg_color(pinDots[i], C_RED, 0);

      pinPos = 0;
      pinEntry[0] = '\0';
      lv_timer_t *t = lv_timer_create([](lv_timer_t *timer) {
        for (int i = 0; i < 4; i++)
          lv_obj_set_style_bg_color(pinDots[i], C_BORDER, 0);
        lv_label_set_text(lblLoginError, "");
        lv_timer_delete(timer);
      }, 800, nullptr);
      (void)t;
    }
  }
}

static void pinClear() {
  pinPos = 0;
  pinEntry[0] = '\0';
  for (int i = 0; i < 4; i++)
    lv_obj_set_style_bg_color(pinDots[i], C_BORDER, 0);
  lv_label_set_text(lblLoginError, "");
}

static void pinBtnCb(lv_event_t *e) {
  touchActivity();
  const char *txt = lv_label_get_text(lv_obj_get_child((lv_obj_t *)lv_event_get_target(e), 0));
  if (strcmp(txt, LV_SYMBOL_BACKSPACE) == 0) {
    if (pinPos > 0) {
      pinPos--;
      pinEntry[pinPos] = '\0';
      lv_obj_set_style_bg_color(pinDots[pinPos], C_BORDER, 0);
    }
  } else if (strcmp(txt, "C") == 0) {
    pinClear();
  } else {
    pinAppend(txt[0]);
  }
}

static lv_obj_t *mkPinBtn(lv_obj_t *parent, const char *txt, int w, int h) {
  lv_obj_t *btn = lv_obj_create(parent);
  lv_obj_set_size(btn, w, h);
  lv_obj_set_style_bg_color(btn, C_PIN_BTN, 0);
  lv_obj_set_style_bg_color(btn, C_PIN_BTN_PR, LV_STATE_PRESSED);
  lv_obj_set_style_bg_opa(btn, LV_OPA_COVER, 0);
  lv_obj_set_style_radius(btn, 12, 0);
  lv_obj_set_style_border_width(btn, 0, 0);
  lv_obj_clear_flag(btn, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_add_flag(btn, LV_OBJ_FLAG_CLICKABLE);

  lv_obj_t *lbl = lv_label_create(btn);
  lv_label_set_text(lbl, txt);
  lv_obj_set_style_text_color(lbl, C_TEXT, 0);
  lv_obj_center(lbl);

  lv_obj_add_event_cb(btn, pinBtnCb, LV_EVENT_CLICKED, nullptr);
  return btn;
}

void buildLoginPage() {
  scrLogin = lv_obj_create(nullptr);
  lv_obj_set_style_bg_color(scrLogin, C_BG, 0);

  // Title
  lv_obj_t *title = lv_label_create(scrLogin);
  lv_label_set_text(title, LV_SYMBOL_HOME "  GigaSenseHub");
  lv_obj_set_style_text_color(title, C_TEXT, 0);
  lv_obj_align(title, LV_ALIGN_TOP_MID, 0, 40);

  lv_obj_t *subtitle = lv_label_create(scrLogin);
  lv_label_set_text(subtitle, "Enter PIN to unlock");
  lv_obj_set_style_text_color(subtitle, C_TEXT_DIM, 0);
  lv_obj_align(subtitle, LV_ALIGN_TOP_MID, 0, 70);

  // PIN dots
  lv_obj_t *dotRow = lv_obj_create(scrLogin);
  lv_obj_set_size(dotRow, 160, 30);
  lv_obj_set_style_bg_opa(dotRow, LV_OPA_TRANSP, 0);
  lv_obj_set_style_border_width(dotRow, 0, 0);
  lv_obj_set_style_pad_all(dotRow, 0, 0);
  lv_obj_set_flex_flow(dotRow, LV_FLEX_FLOW_ROW);
  lv_obj_set_flex_align(dotRow, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
  lv_obj_set_style_pad_column(dotRow, 16, 0);
  lv_obj_align(dotRow, LV_ALIGN_TOP_MID, 0, 105);
  lv_obj_clear_flag(dotRow, LV_OBJ_FLAG_SCROLLABLE);

  for (int i = 0; i < 4; i++) {
    pinDots[i] = lv_obj_create(dotRow);
    lv_obj_set_size(pinDots[i], 20, 20);
    lv_obj_set_style_radius(pinDots[i], LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_bg_color(pinDots[i], C_BORDER, 0);
    lv_obj_set_style_bg_opa(pinDots[i], LV_OPA_COVER, 0);
    lv_obj_set_style_border_color(pinDots[i], C_ACCENT, 0);
    lv_obj_set_style_border_width(pinDots[i], 2, 0);
    lv_obj_clear_flag(pinDots[i], LV_OBJ_FLAG_SCROLLABLE);
  }

  // Error label
  lblLoginError = lv_label_create(scrLogin);
  lv_label_set_text(lblLoginError, "");
  lv_obj_set_style_text_color(lblLoginError, C_RED, 0);
  lv_obj_align(lblLoginError, LV_ALIGN_TOP_MID, 0, 140);

  // Numpad
  lv_obj_t *pad = lv_obj_create(scrLogin);
  lv_obj_set_size(pad, 240, 310);
  lv_obj_set_style_bg_opa(pad, LV_OPA_TRANSP, 0);
  lv_obj_set_style_border_width(pad, 0, 0);
  lv_obj_set_style_pad_all(pad, 0, 0);
  lv_obj_set_style_pad_row(pad, 8, 0);
  lv_obj_set_style_pad_column(pad, 8, 0);
  lv_obj_set_flex_flow(pad, LV_FLEX_FLOW_ROW_WRAP);
  lv_obj_set_flex_align(pad, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
  lv_obj_align(pad, LV_ALIGN_TOP_MID, 0, 165);
  lv_obj_clear_flag(pad, LV_OBJ_FLAG_SCROLLABLE);

  const char *keys[] = {"1","2","3","4","5","6","7","8","9","C","0",LV_SYMBOL_BACKSPACE};
  for (int i = 0; i < 12; i++) {
    mkPinBtn(pad, keys[i], 68, 62);
  }
}

// ============================================================
// PAGE 2: DASHBOARD
// ============================================================

static void lockCb(lv_event_t *e) {
  touchActivity();
  lv_obj_t *sw = (lv_obj_t *)lv_event_get_target(e);
  bool on = lv_obj_has_state(sw, LV_STATE_CHECKED);
  for (int i = 0; i < 3; i++) {
    if (sw == swDoor[i]) {
      settings.doorLocked[i] = on;
      lv_led_set_color(ledDoor[i], on ? C_GREEN : C_RED);
      lv_led_set_brightness(ledDoor[i], on ? 200 : 255);
      { char lb[48]; snprintf(lb, sizeof(lb), "%s %s", doorNames[i], on ? "LOCKED" : "UNLOCKED");
        eventInfo(lb); }
      // Persist door lock state
      settings.crc32 = _storage_detail::settingsCrc(settings);
      storageSave(settings);
    }
  }
}

static void gotoSettingsCb(lv_event_t *e) {
  touchActivity();
  currentPage = PAGE_SETTINGS;
  lv_screen_load_anim(scrSettings, LV_SCR_LOAD_ANIM_MOVE_LEFT, 300, 0, false);
  Serial.println("Dashboard -> Settings");
}

static void refreshLogList();  // forward declaration

static void gotoLogCb(lv_event_t *e) {
  touchActivity();
  currentPage = PAGE_LOG;
  refreshLogList();
  lv_screen_load_anim(scrLog, LV_SCR_LOAD_ANIM_MOVE_LEFT, 300, 0, false);
  Serial.println("Dashboard -> Activity Log");
}

static void logoutCb(lv_event_t *e) {
  touchActivity();
  currentPage = PAGE_LOGIN;
  pinClear();
  lv_screen_load_anim(scrLogin, LV_SCR_LOAD_ANIM_MOVE_RIGHT, 300, 0, false);
  eventInfo("User logged out");
  Serial.println("Dashboard -> Login (logout)");
}

static void mkDoorRow(lv_obj_t *p, int idx) {
  lv_obj_t *row = mkRow(p, 34);
  ledDoor[idx] = lv_led_create(row);
  lv_led_set_color(ledDoor[idx], settings.doorLocked[idx] ? C_GREEN : C_RED);
  lv_led_set_brightness(ledDoor[idx], settings.doorLocked[idx] ? 200 : 255);
  lv_obj_set_size(ledDoor[idx], 12, 12);

  lv_obj_t *lbl = lv_label_create(row);
  lv_label_set_text(lbl, doorNames[idx]);
  lv_obj_set_style_text_color(lbl, C_TEXT, 0);
  lv_obj_set_flex_grow(lbl, 1);

  swDoor[idx] = lv_switch_create(row);
  if (settings.doorLocked[idx]) lv_obj_add_state(swDoor[idx], LV_STATE_CHECKED);
  lv_obj_set_style_bg_color(swDoor[idx], C_RED, 0);
  lv_obj_set_style_bg_color(swDoor[idx], C_GREEN, LV_PART_INDICATOR | LV_STATE_CHECKED);
  lv_obj_add_event_cb(swDoor[idx], lockCb, LV_EVENT_VALUE_CHANGED, nullptr);
}

static void mkWinRow(lv_obj_t *p, int idx) {
  lv_obj_t *row = mkRow(p, 26);
  lv_obj_set_style_pad_column(row, 8, 0);
  lv_obj_set_flex_align(row, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);

  ledWin[idx] = lv_led_create(row);
  lv_led_set_color(ledWin[idx], C_GREEN);
  lv_led_set_brightness(ledWin[idx], 200);
  lv_obj_set_size(ledWin[idx], 10, 10);

  lv_obj_t *n = lv_label_create(row);
  lv_label_set_text(n, windowNames[idx]);
  lv_obj_set_style_text_color(n, C_TEXT, 0);

  lblWin[idx] = lv_label_create(row);
  lv_label_set_text(lblWin[idx], "CLOSED");
  lv_obj_set_style_text_color(lblWin[idx], C_GREEN, 0);
}

void buildDashboardPage() {
  scrDashboard = lv_obj_create(nullptr);
  lv_obj_set_style_bg_color(scrDashboard, C_BG, 0);

  // --- Top bar ---
  lv_obj_t *top = lv_obj_create(scrDashboard);
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

  lv_obj_t *titleLbl = lv_label_create(top);
  lv_label_set_text(titleLbl, LV_SYMBOL_HOME "  GigaSenseHub");
  lv_obj_set_style_text_color(titleLbl, C_TEXT, 0);

  lblStatusBar = lv_label_create(top);
  lv_label_set_text(lblStatusBar, LV_SYMBOL_WIFI " Connecting...");
  lv_obj_set_style_text_color(lblStatusBar, C_ORANGE, 0);

  // Nav buttons container
  lv_obj_t *navRow = lv_obj_create(top);
  lv_obj_set_size(navRow, LV_SIZE_CONTENT, 30);
  lv_obj_set_style_bg_opa(navRow, LV_OPA_TRANSP, 0);
  lv_obj_set_style_border_width(navRow, 0, 0);
  lv_obj_set_style_pad_all(navRow, 0, 0);
  lv_obj_set_style_pad_column(navRow, 6, 0);
  lv_obj_set_flex_flow(navRow, LV_FLEX_FLOW_ROW);
  lv_obj_clear_flag(navRow, LV_OBJ_FLAG_SCROLLABLE);

  // Settings button (large touch target)
  lv_obj_t *btnSettings = lv_obj_create(navRow);
  lv_obj_set_size(btnSettings, 60, 30);
  lv_obj_set_style_bg_color(btnSettings, C_PIN_BTN, 0);
  lv_obj_set_style_bg_color(btnSettings, C_PIN_BTN_PR, LV_STATE_PRESSED);
  lv_obj_set_style_bg_opa(btnSettings, LV_OPA_COVER, 0);
  lv_obj_set_style_radius(btnSettings, 6, 0);
  lv_obj_set_style_border_width(btnSettings, 0, 0);
  lv_obj_clear_flag(btnSettings, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_add_flag(btnSettings, LV_OBJ_FLAG_CLICKABLE);
  lv_obj_t *sIcon = lv_label_create(btnSettings);
  lv_label_set_text(sIcon, LV_SYMBOL_SETTINGS);
  lv_obj_set_style_text_color(sIcon, C_TEXT_DIM, 0);
  lv_obj_center(sIcon);
  lv_obj_add_event_cb(btnSettings, gotoSettingsCb, LV_EVENT_CLICKED, nullptr);

  // Log button (large touch target)
  lv_obj_t *btnLog = lv_obj_create(navRow);
  lv_obj_set_size(btnLog, 60, 30);
  lv_obj_set_style_bg_color(btnLog, C_PIN_BTN, 0);
  lv_obj_set_style_bg_color(btnLog, C_PIN_BTN_PR, LV_STATE_PRESSED);
  lv_obj_set_style_bg_opa(btnLog, LV_OPA_COVER, 0);
  lv_obj_set_style_radius(btnLog, 6, 0);
  lv_obj_set_style_border_width(btnLog, 0, 0);
  lv_obj_clear_flag(btnLog, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_add_flag(btnLog, LV_OBJ_FLAG_CLICKABLE);
  lv_obj_t *logIcon = lv_label_create(btnLog);
  lv_label_set_text(logIcon, LV_SYMBOL_LIST);
  lv_obj_set_style_text_color(logIcon, C_TEXT_DIM, 0);
  lv_obj_center(logIcon);
  lv_obj_add_event_cb(btnLog, gotoLogCb, LV_EVENT_CLICKED, nullptr);

  // Lock/logout button (large touch target)
  lv_obj_t *btnLock = lv_obj_create(navRow);
  lv_obj_set_size(btnLock, 60, 30);
  lv_obj_set_style_bg_color(btnLock, C_PIN_BTN, 0);
  lv_obj_set_style_bg_color(btnLock, C_PIN_BTN_PR, LV_STATE_PRESSED);
  lv_obj_set_style_bg_opa(btnLock, LV_OPA_COVER, 0);
  lv_obj_set_style_radius(btnLock, 6, 0);
  lv_obj_set_style_border_width(btnLock, 0, 0);
  lv_obj_clear_flag(btnLock, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_add_flag(btnLock, LV_OBJ_FLAG_CLICKABLE);
  lv_obj_t *lIcon = lv_label_create(btnLock);
  lv_label_set_text(lIcon, LV_SYMBOL_POWER);
  lv_obj_set_style_text_color(lIcon, C_RED, 0);
  lv_obj_center(lIcon);
  lv_obj_add_event_cb(btnLock, logoutCb, LV_EVENT_CLICKED, nullptr);

  // === LEFT COLUMN ===

  // Camera card
  lv_obj_t *cc = mkCard(scrDashboard, 370, 288);
  lv_obj_set_pos(cc, 6, 40);
  lv_obj_set_flex_flow(cc, LV_FLEX_FLOW_COLUMN);
  lv_obj_set_style_pad_gap(cc, 4, 0);

  lv_obj_t *ch = mkRow(cc, 18);
  mkTitle(ch, LV_SYMBOL_VIDEO "  Camera 1");
  lblFps = lv_label_create(ch);
  lv_label_set_text(lblFps, "-- fps");
  lv_obj_set_style_text_color(lblFps, C_GREEN, 0);

  // Video image
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
  }

  lblStreamStatus = lv_label_create(cc);
  lv_label_set_text(lblStreamStatus, "Connecting...");
  lv_obj_set_style_text_color(lblStreamStatus, C_TEXT_DIM, 0);

  // IMU card
  lv_obj_t *sc = mkCard(scrDashboard, 370, 128);
  lv_obj_set_pos(sc, 6, 334);
  lv_obj_set_flex_flow(sc, LV_FLEX_FLOW_COLUMN);
  lv_obj_set_style_pad_gap(sc, 3, 0);

  mkTitle(sc, LV_SYMBOL_GPS "  GIGA IMU (BMI270)");
  lblAccel  = lv_label_create(sc); lv_label_set_text(lblAccel, "Accel: --");
  lv_obj_set_style_text_color(lblAccel, C_TEXT_DIM, 0);
  lblGyro   = lv_label_create(sc); lv_label_set_text(lblGyro, "Gyro: --");
  lv_obj_set_style_text_color(lblGyro, C_TEXT_DIM, 0);
  lblMotion = lv_label_create(sc); lv_label_set_text(lblMotion, "Motion: --");
  lv_obj_set_style_text_color(lblMotion, C_GREEN, 0);
  lblCamMotion = lv_label_create(sc); lv_label_set_text(lblCamMotion, "Camera: --");
  lv_obj_set_style_text_color(lblCamMotion, C_TEXT_DIM, 0);
  lblUptime = lv_label_create(sc); lv_label_set_text(lblUptime, "Uptime: 0s");
  lv_obj_set_style_text_color(lblUptime, C_TEXT_DIM, 0);

  // === RIGHT COLUMN ===

  // Door locks
  lv_obj_t *lc = mkCard(scrDashboard, 412, 178);
  lv_obj_set_pos(lc, 382, 40);
  lv_obj_set_flex_flow(lc, LV_FLEX_FLOW_COLUMN);
  lv_obj_set_style_pad_gap(lc, 6, 0);
  mkTitle(lc, LV_SYMBOL_CLOSE "  Door Locks");
  for (int i = 0; i < 3; i++) mkDoorRow(lc, i);

  // Windows
  lv_obj_t *wc = mkCard(scrDashboard, 412, 144);
  lv_obj_set_pos(wc, 382, 224);
  lv_obj_set_flex_flow(wc, LV_FLEX_FLOW_COLUMN);
  lv_obj_set_style_pad_gap(wc, 4, 0);
  mkTitle(wc, LV_SYMBOL_WARNING "  Window Sensors");
  for (int i = 0; i < 3; i++) mkWinRow(wc, i);

  // System card
  lv_obj_t *sysc = mkCard(scrDashboard, 412, 92);
  lv_obj_set_pos(sysc, 382, 374);
  lv_obj_set_flex_flow(sysc, LV_FLEX_FLOW_COLUMN);
  lv_obj_set_style_pad_gap(sysc, 4, 0);
  mkTitle(sysc, LV_SYMBOL_SETTINGS "  System");
  lv_obj_t *ar = mkRow(sysc, 26);
  lv_obj_set_style_pad_column(ar, 8, 0);
  lv_obj_set_flex_align(ar, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
  lv_obj_t *al = lv_led_create(ar);
  lv_led_set_color(al, C_GREEN); lv_led_set_brightness(al, 200); lv_obj_set_size(al, 10, 10);
  lv_obj_t *at = lv_label_create(ar);
  lv_label_set_text(at, "System Armed - All Active");
  lv_obj_set_style_text_color(at, C_GREEN, 0);
}

// ============================================================
// PAGE 3: SETTINGS
// ============================================================

static void backToDashCb(lv_event_t *e) {
  touchActivity();
  currentPage = PAGE_DASHBOARD;
  lv_screen_load_anim(scrDashboard, LV_SCR_LOAD_ANIM_MOVE_RIGHT, 300, 0, false);
  Serial.println("Settings -> Dashboard");
}

static void motionSliderCb(lv_event_t *e) {
  touchActivity();
  settings.motionThreshold = lv_slider_get_value((lv_obj_t *)lv_event_get_target(e));
  char buf[16]; snprintf(buf, sizeof(buf), "%d%%", settings.motionThreshold);
  lv_label_set_text(lblMotionVal, buf);
  settings.crc32 = _storage_detail::settingsCrc(settings);
  storageSave(settings);
}

static void autoLockSliderCb(lv_event_t *e) {
  touchActivity();
  settings.autoLockSecs = lv_slider_get_value((lv_obj_t *)lv_event_get_target(e));
  char buf[16]; snprintf(buf, sizeof(buf), "%ds", settings.autoLockSecs);
  lv_label_set_text(lblAutoLockVal, buf);
  settings.crc32 = _storage_detail::settingsCrc(settings);
  storageSave(settings);
}

static void fpsSliderCb(lv_event_t *e) {
  touchActivity();
  settings.targetFps = lv_slider_get_value((lv_obj_t *)lv_event_get_target(e));
  char buf[16]; snprintf(buf, sizeof(buf), "%d fps", settings.targetFps);
  lv_label_set_text(lblFpsVal, buf);
  settings.crc32 = _storage_detail::settingsCrc(settings);
  storageSave(settings);
}

static void qualitySliderCb(lv_event_t *e) {
  touchActivity();
  settings.jpegQuality = lv_slider_get_value((lv_obj_t *)lv_event_get_target(e));
  char buf[16]; snprintf(buf, sizeof(buf), "%d", settings.jpegQuality);
  lv_label_set_text(lblQualityVal, buf);
  settings.crc32 = _storage_detail::settingsCrc(settings);
  storageSave(settings);
}

static void pinChangeCb(lv_event_t *e) {
  touchActivity();
  const char *newPin = lv_textarea_get_text(taNewPin);
  if (strlen(newPin) == 4) {
    strncpy(settings.pin, newPin, 4);
    settings.pin[4] = '\0';
    settings.crc32 = _storage_detail::settingsCrc(settings);
    storageSave(settings);
    lv_textarea_set_text(taNewPin, "");
    eventInfo("PIN changed successfully");
    Serial.print("PIN changed and saved to flash");
  }
}

static lv_obj_t *mkSettingRow(lv_obj_t *parent, const char *label, int sliderMin, int sliderMax,
                               int sliderVal, lv_obj_t **sliderOut, lv_obj_t **valLblOut,
                               lv_event_cb_t cb) {
  lv_obj_t *row = lv_obj_create(parent);
  lv_obj_set_size(row, lv_pct(100), 44);
  lv_obj_set_style_bg_opa(row, LV_OPA_TRANSP, 0);
  lv_obj_set_style_border_width(row, 0, 0);
  lv_obj_set_style_pad_all(row, 0, 0);
  lv_obj_set_style_pad_column(row, 10, 0);
  lv_obj_set_flex_flow(row, LV_FLEX_FLOW_ROW);
  lv_obj_set_flex_align(row, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
  lv_obj_clear_flag(row, LV_OBJ_FLAG_SCROLLABLE);

  lv_obj_t *lbl = lv_label_create(row);
  lv_label_set_text(lbl, label);
  lv_obj_set_style_text_color(lbl, C_TEXT, 0);
  lv_obj_set_size(lbl, 140, LV_SIZE_CONTENT);

  *sliderOut = lv_slider_create(row);
  lv_slider_set_range(*sliderOut, sliderMin, sliderMax);
  lv_slider_set_value(*sliderOut, sliderVal, LV_ANIM_OFF);
  lv_obj_set_size(*sliderOut, 160, 10);
  lv_obj_set_style_bg_color(*sliderOut, C_BORDER, 0);
  lv_obj_set_style_bg_color(*sliderOut, C_ACCENT, LV_PART_INDICATOR);
  lv_obj_set_style_bg_color(*sliderOut, C_ACCENT, LV_PART_KNOB);
  lv_obj_add_event_cb(*sliderOut, cb, LV_EVENT_VALUE_CHANGED, nullptr);

  *valLblOut = lv_label_create(row);
  char buf[16];
  snprintf(buf, sizeof(buf), "%d", sliderVal);
  lv_label_set_text(*valLblOut, buf);
  lv_obj_set_style_text_color(*valLblOut, C_ACCENT, 0);

  return row;
}

void buildSettingsPage() {
  scrSettings = lv_obj_create(nullptr);
  lv_obj_set_style_bg_color(scrSettings, C_BG, 0);

  // Top bar
  lv_obj_t *top = lv_obj_create(scrSettings);
  lv_obj_set_size(top, 800, 34);
  lv_obj_set_pos(top, 0, 0);
  lv_obj_set_style_bg_color(top, C_CARD, 0);
  lv_obj_set_style_bg_opa(top, LV_OPA_COVER, 0);
  lv_obj_set_style_border_width(top, 0, 0);
  lv_obj_set_style_radius(top, 0, 0);
  lv_obj_set_style_pad_hor(top, 12, 0);
  lv_obj_clear_flag(top, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_set_flex_flow(top, LV_FLEX_FLOW_ROW);
  lv_obj_set_flex_align(top, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
  lv_obj_set_style_pad_column(top, 10, 0);

  // Back button (large touch target)
  lv_obj_t *btnBack = lv_obj_create(top);
  lv_obj_set_size(btnBack, 60, 30);
  lv_obj_set_style_bg_color(btnBack, C_PIN_BTN, 0);
  lv_obj_set_style_bg_opa(btnBack, LV_OPA_COVER, 0);
  lv_obj_set_style_radius(btnBack, 4, 0);
  lv_obj_set_style_border_width(btnBack, 0, 0);
  lv_obj_clear_flag(btnBack, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_add_flag(btnBack, LV_OBJ_FLAG_CLICKABLE);
  lv_obj_t *bIcon = lv_label_create(btnBack);
  lv_label_set_text(bIcon, LV_SYMBOL_LEFT);
  lv_obj_set_style_text_color(bIcon, C_TEXT, 0);
  lv_obj_center(bIcon);
  lv_obj_add_event_cb(btnBack, backToDashCb, LV_EVENT_CLICKED, nullptr);

  lv_obj_t *titleLbl = lv_label_create(top);
  lv_label_set_text(titleLbl, LV_SYMBOL_SETTINGS "  Settings");
  lv_obj_set_style_text_color(titleLbl, C_TEXT, 0);

  // === LEFT: Camera & Security Settings ===
  lv_obj_t *leftCard = mkCard(scrSettings, 380, 420);
  lv_obj_set_pos(leftCard, 6, 42);
  lv_obj_set_flex_flow(leftCard, LV_FLEX_FLOW_COLUMN);
  lv_obj_set_style_pad_gap(leftCard, 6, 0);

  mkTitle(leftCard, LV_SYMBOL_VIDEO "  Camera");

  char fpsBuf[16]; snprintf(fpsBuf, sizeof(fpsBuf), "%d fps", settings.targetFps);
  mkSettingRow(leftCard, "Target FPS", 5, 30, settings.targetFps, &sliderFps, &lblFpsVal, fpsSliderCb);
  lv_label_set_text(lblFpsVal, fpsBuf);

  char qualBuf[16]; snprintf(qualBuf, sizeof(qualBuf), "%d", settings.jpegQuality);
  mkSettingRow(leftCard, "JPEG Quality", 5, 63, settings.jpegQuality, &sliderQuality, &lblQualityVal, qualitySliderCb);
  lv_label_set_text(lblQualityVal, qualBuf);

  // Divider
  lv_obj_t *div1 = lv_obj_create(leftCard);
  lv_obj_set_size(div1, lv_pct(100), 1);
  lv_obj_set_style_bg_color(div1, C_BORDER, 0);
  lv_obj_set_style_bg_opa(div1, LV_OPA_COVER, 0);
  lv_obj_set_style_border_width(div1, 0, 0);
  lv_obj_clear_flag(div1, LV_OBJ_FLAG_SCROLLABLE);

  mkTitle(leftCard, LV_SYMBOL_EYE_OPEN "  Security");

  char motBuf[16]; snprintf(motBuf, sizeof(motBuf), "%d%%", settings.motionThreshold);
  mkSettingRow(leftCard, "Motion Sens.", 10, 100, settings.motionThreshold, &sliderMotion, &lblMotionVal, motionSliderCb);
  lv_label_set_text(lblMotionVal, motBuf);

  char alBuf[16]; snprintf(alBuf, sizeof(alBuf), "%ds", settings.autoLockSecs);
  mkSettingRow(leftCard, "Auto-Lock", 30, 600, settings.autoLockSecs, &sliderAutoLock, &lblAutoLockVal, autoLockSliderCb);
  lv_label_set_text(lblAutoLockVal, alBuf);

  // Divider
  lv_obj_t *div2 = lv_obj_create(leftCard);
  lv_obj_set_size(div2, lv_pct(100), 1);
  lv_obj_set_style_bg_color(div2, C_BORDER, 0);
  lv_obj_set_style_bg_opa(div2, LV_OPA_COVER, 0);
  lv_obj_set_style_border_width(div2, 0, 0);
  lv_obj_clear_flag(div2, LV_OBJ_FLAG_SCROLLABLE);

  mkTitle(leftCard, LV_SYMBOL_EDIT "  Change PIN");

  lv_obj_t *pinRow = lv_obj_create(leftCard);
  lv_obj_set_size(pinRow, lv_pct(100), 40);
  lv_obj_set_style_bg_opa(pinRow, LV_OPA_TRANSP, 0);
  lv_obj_set_style_border_width(pinRow, 0, 0);
  lv_obj_set_style_pad_all(pinRow, 0, 0);
  lv_obj_set_style_pad_column(pinRow, 8, 0);
  lv_obj_set_flex_flow(pinRow, LV_FLEX_FLOW_ROW);
  lv_obj_set_flex_align(pinRow, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
  lv_obj_clear_flag(pinRow, LV_OBJ_FLAG_SCROLLABLE);

  taNewPin = lv_textarea_create(pinRow);
  lv_textarea_set_max_length(taNewPin, 4);
  lv_textarea_set_one_line(taNewPin, true);
  lv_textarea_set_password_mode(taNewPin, true);
  lv_textarea_set_placeholder_text(taNewPin, "New PIN");
  lv_obj_set_size(taNewPin, 120, 36);
  lv_obj_set_style_bg_color(taNewPin, C_PIN_BTN, 0);
  lv_obj_set_style_text_color(taNewPin, C_TEXT, 0);
  lv_obj_set_style_border_color(taNewPin, C_BORDER, 0);

  lv_obj_t *btnSavePin = lv_obj_create(pinRow);
  lv_obj_set_size(btnSavePin, 60, 32);
  lv_obj_set_style_bg_color(btnSavePin, C_ACCENT, 0);
  lv_obj_set_style_bg_opa(btnSavePin, LV_OPA_COVER, 0);
  lv_obj_set_style_radius(btnSavePin, 6, 0);
  lv_obj_set_style_border_width(btnSavePin, 0, 0);
  lv_obj_clear_flag(btnSavePin, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_add_flag(btnSavePin, LV_OBJ_FLAG_CLICKABLE);
  lv_obj_t *saveLbl = lv_label_create(btnSavePin);
  lv_label_set_text(saveLbl, "Save");
  lv_obj_set_style_text_color(saveLbl, lv_color_hex(0xFFFFFF), 0);
  lv_obj_center(saveLbl);
  lv_obj_add_event_cb(btnSavePin, pinChangeCb, LV_EVENT_CLICKED, nullptr);

  // === RIGHT: WiFi & System Info ===
  lv_obj_t *rightCard = mkCard(scrSettings, 400, 200);
  lv_obj_set_pos(rightCard, 392, 42);
  lv_obj_set_flex_flow(rightCard, LV_FLEX_FLOW_COLUMN);
  lv_obj_set_style_pad_gap(rightCard, 6, 0);

  mkTitle(rightCard, LV_SYMBOL_WIFI "  Network");

  lblSettingsWifi = lv_label_create(rightCard);
  lv_label_set_text(lblSettingsWifi, "SSID: --\nIP: --\nStatus: --");
  lv_obj_set_style_text_color(lblSettingsWifi, C_TEXT_DIM, 0);

  // System info card
  lv_obj_t *sysCard = mkCard(scrSettings, 400, 210);
  lv_obj_set_pos(sysCard, 392, 250);
  lv_obj_set_flex_flow(sysCard, LV_FLEX_FLOW_COLUMN);
  lv_obj_set_style_pad_gap(sysCard, 6, 0);

  mkTitle(sysCard, LV_SYMBOL_DRIVE "  System Info");

  lblSysFw = lv_label_create(sysCard);
  lv_label_set_text(lblSysFw, "Firmware: GigaSenseHub v1.0");
  lv_obj_set_style_text_color(lblSysFw, C_TEXT_DIM, 0);

  lblSysUptime = lv_label_create(sysCard);
  lv_label_set_text(lblSysUptime, "Uptime: 0s");
  lv_obj_set_style_text_color(lblSysUptime, C_TEXT_DIM, 0);

  lblSysMem = lv_label_create(sysCard);
  lv_label_set_text(lblSysMem, "Free heap: --");
  lv_obj_set_style_text_color(lblSysMem, C_TEXT_DIM, 0);

  lv_obj_t *camInfo = lv_label_create(sysCard);
  lv_label_set_text(camInfo, "Camera: ESP32-S3 Sense (QVGA)");
  lv_obj_set_style_text_color(camInfo, C_TEXT_DIM, 0);

  lv_obj_t *imuInfo = lv_label_create(sysCard);
  lv_label_set_text(imuInfo, "IMU: BMI270 (Wire1)");
  lv_obj_set_style_text_color(imuInfo, C_TEXT_DIM, 0);

  lv_obj_t *dispInfo = lv_label_create(sysCard);
  lv_label_set_text(dispInfo, "Display: 800x480 LVGL 9");
  lv_obj_set_style_text_color(dispInfo, C_TEXT_DIM, 0);
}

// ============================================================
// PAGE 4: ACTIVITY LOG
// ============================================================

static void backFromLogCb(lv_event_t *e) {
  touchActivity();
  currentPage = PAGE_DASHBOARD;
  lv_screen_load_anim(scrDashboard, LV_SCR_LOAD_ANIM_MOVE_RIGHT, 300, 0, false);
}

static void refreshLogList() {
  if (!logList) return;
  // Clear existing children
  lv_obj_clean(logList);

  int n = eventLogCount();
  char timeBuf[16];

  for (int i = 0; i < n; i++) {
    const EventEntry *ev = eventLogGetNewest(i);
    if (!ev) continue;

    eventFormatTime(ev->timestamp, timeBuf, sizeof(timeBuf));

    // Row container
    lv_obj_t *row = lv_obj_create(logList);
    lv_obj_set_size(row, lv_pct(100), 32);
    lv_obj_set_style_bg_opa(row, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(row, 0, 0);
    lv_obj_set_style_pad_all(row, 2, 0);
    lv_obj_set_style_pad_column(row, 8, 0);
    lv_obj_set_flex_flow(row, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(row, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_clear_flag(row, LV_OBJ_FLAG_SCROLLABLE);

    // Severity icon
    lv_obj_t *icon = lv_label_create(row);
    lv_label_set_text(icon, eventSeverityIcon(ev->severity));
    lv_color_t ic;
    switch (ev->severity) {
      case EVT_INFO:  ic = C_GREEN;  break;
      case EVT_WARN:  ic = C_ORANGE; break;
      case EVT_ALERT: ic = C_RED;    break;
      default:        ic = C_TEXT;    break;
    }
    lv_obj_set_style_text_color(icon, ic, 0);
    lv_obj_set_size(icon, 20, LV_SIZE_CONTENT);

    // Timestamp
    lv_obj_t *ts = lv_label_create(row);
    lv_label_set_text(ts, timeBuf);
    lv_obj_set_style_text_color(ts, C_TEXT_DIM, 0);
    lv_obj_set_size(ts, 80, LV_SIZE_CONTENT);

    // Message
    lv_obj_t *msg = lv_label_create(row);
    lv_label_set_text(msg, ev->message);
    lv_obj_set_style_text_color(msg, C_TEXT, 0);
    lv_obj_set_flex_grow(msg, 1);

    // Separator line
    if (i < n - 1) {
      lv_obj_t *sep = lv_obj_create(logList);
      lv_obj_set_size(sep, lv_pct(100), 1);
      lv_obj_set_style_bg_color(sep, C_BORDER, 0);
      lv_obj_set_style_bg_opa(sep, LV_OPA_COVER, 0);
      lv_obj_set_style_border_width(sep, 0, 0);
      lv_obj_clear_flag(sep, LV_OBJ_FLAG_SCROLLABLE);
    }
  }

  // Update count label
  if (lblLogCount) {
    char buf[32];
    snprintf(buf, sizeof(buf), "%d events", n);
    lv_label_set_text(lblLogCount, buf);
  }
}

void buildLogPage() {
  scrLog = lv_obj_create(nullptr);
  lv_obj_set_style_bg_color(scrLog, C_BG, 0);

  // Top bar
  lv_obj_t *top = lv_obj_create(scrLog);
  lv_obj_set_size(top, 800, 34);
  lv_obj_set_pos(top, 0, 0);
  lv_obj_set_style_bg_color(top, C_CARD, 0);
  lv_obj_set_style_bg_opa(top, LV_OPA_COVER, 0);
  lv_obj_set_style_border_width(top, 0, 0);
  lv_obj_set_style_radius(top, 0, 0);
  lv_obj_set_style_pad_hor(top, 12, 0);
  lv_obj_clear_flag(top, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_set_flex_flow(top, LV_FLEX_FLOW_ROW);
  lv_obj_set_flex_align(top, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
  lv_obj_set_style_pad_column(top, 10, 0);

  // Back button
  lv_obj_t *btnBack = lv_obj_create(top);
  lv_obj_set_size(btnBack, 60, 30);
  lv_obj_set_style_bg_color(btnBack, C_PIN_BTN, 0);
  lv_obj_set_style_bg_opa(btnBack, LV_OPA_COVER, 0);
  lv_obj_set_style_radius(btnBack, 4, 0);
  lv_obj_set_style_border_width(btnBack, 0, 0);
  lv_obj_clear_flag(btnBack, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_add_flag(btnBack, LV_OBJ_FLAG_CLICKABLE);
  lv_obj_t *bIcon = lv_label_create(btnBack);
  lv_label_set_text(bIcon, LV_SYMBOL_LEFT);
  lv_obj_set_style_text_color(bIcon, C_TEXT, 0);
  lv_obj_center(bIcon);
  lv_obj_add_event_cb(btnBack, backFromLogCb, LV_EVENT_CLICKED, nullptr);

  lv_obj_t *titleLbl = lv_label_create(top);
  lv_label_set_text(titleLbl, LV_SYMBOL_LIST "  Activity Log");
  lv_obj_set_style_text_color(titleLbl, C_TEXT, 0);

  lblLogCount = lv_label_create(top);
  lv_label_set_text(lblLogCount, "0 events");
  lv_obj_set_style_text_color(lblLogCount, C_TEXT_DIM, 0);
  lv_obj_set_flex_grow(lblLogCount, 1);
  lv_obj_set_style_text_align(lblLogCount, LV_TEXT_ALIGN_RIGHT, 0);

  // Scrollable log list
  lv_obj_t *card = mkCard(scrLog, 788, 430);
  lv_obj_set_pos(card, 6, 40);
  lv_obj_set_flex_flow(card, LV_FLEX_FLOW_COLUMN);
  lv_obj_set_style_pad_gap(card, 2, 0);
  lv_obj_add_flag(card, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_set_scroll_dir(card, LV_DIR_VER);

  logList = card;
}

// ============================================================
// WiFi & Stream
// ============================================================

void connectWiFi() {
  // GIGA runs as Access Point — ESP32 nodes connect to us
  // Check WiFi module presence first
  if (WiFi.status() == WL_NO_MODULE) {
    Serial.println("WiFi module not found!");
    wifiConnected = false;
    return;
  }
  // Configure AP IP before starting (required by mbed WiFi)
  WiFi.config(IPAddress(192, 168, 3, 1));
  // Arduino mbed WiFi uses beginAP() (not softAP)
  int status = WiFi.beginAP(AP_SSID, AP_PASSWORD, AP_CHANNEL);
  Serial.print("WiFi AP status: "); Serial.println(status);
  if (status == WL_AP_LISTENING) {
    wifiConnected = true;
    Serial.print("WiFi AP started — SSID: "); Serial.print(AP_SSID);
    Serial.print(" IP: "); Serial.println(GIGA_AP_IP);
  } else {
    wifiConnected = false;
    Serial.print("WiFi AP FAILED — status: "); Serial.println(status);
  }
  // Initialize nodes
  for (int i = 0; i < MAX_NODES; i++) {
    nodes[i].active = false;
    nodes[i].streamConnected = false;
  }
}

// Scan for camera nodes on the AP network
void scanForNodes() {
  WiFiClient probe;
  probe.setTimeout(1000);  // 1-second connect timeout
  activeNodeCount = 0;
  for (int i = 0; i < MAX_NODES; i++) {
    IPAddress ip(192, 168, 3, 2 + i);
    if (probe.connect(ip, STREAM_PORT)) {
      probe.print("GET " NODE_INFO_PATH " HTTP/1.0\r\nHost: ");
      probe.print(ip); probe.print("\r\n\r\n");
      unsigned long t = millis();
      while (!probe.available() && millis() - t < 1000) delay(10);
      // Read response
      String body = "";
      bool hdr = true;
      while (probe.available()) {
        String line = probe.readStringUntil('\n');
        if (hdr && (line == "\r" || line.length() == 0)) { hdr = false; continue; }
        if (!hdr) body += line;
      }
      probe.stop();

      // Parse nodeId from JSON
      int nidIdx = body.indexOf("\"nodeId\":");
      if (nidIdx >= 0) {
        int nid = body.substring(nidIdx + 9, body.indexOf(',', nidIdx + 9)).toInt();
        if (nid >= 0 && nid < MAX_NODES) {
          nodes[nid].active = true;
          nodes[nid].ip = ip;
          nodes[nid].nodeId = nid;
          activeNodeCount++;

          // Parse RSSI
          int rIdx = body.indexOf("\"rssi\":");
          if (rIdx >= 0) nodes[nid].rssi = body.substring(rIdx + 7, body.indexOf(',', rIdx)).toInt();

          Serial.print("Found node "); Serial.print(nid);
          Serial.print(" at "); Serial.println(ip);
        }
      }
    } else {
      if (nodes[i].active) {
        Serial.print("Node "); Serial.print(i); Serial.println(" lost");
      }
      nodes[i].active = false;
      nodes[i].streamConnected = false;
    }
  }
}

bool connectStream() {
  // Connect to the active node's stream
  if (activeNodeCount == 0) return false;
  if (streamClient.connected()) return true;
  CameraNode &node = nodes[activeNodeIdx];
  if (!node.active) return false;
  if (!streamClient.connect(node.ip, STREAM_PORT)) return false;
  char req[128];
  snprintf(req, sizeof(req), "GET %s HTTP/1.1\r\nHost: %s\r\nConnection: keep-alive\r\n\r\n",
           STREAM_PATH, node.ip.toString().c_str());
  streamClient.print(req);
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

// ============================================================
// UI Update (Dashboard)
// ============================================================

void updateDashboardUI() {
  if (currentPage != PAGE_DASHBOARD) return;
  char buf[64];

  snprintf(buf, sizeof(buf), "%.1f fps", currentFps);
  lv_label_set_text(lblFps, buf);

  lv_label_set_text(lblStreamStatus, streamConnected ? LV_SYMBOL_PLAY " LIVE" : "Offline");
  lv_obj_set_style_text_color(lblStreamStatus, streamConnected ? C_GREEN : C_RED, 0);

  if (wifiConnected) {
    snprintf(buf, sizeof(buf), LV_SYMBOL_WIFI " AP %s | %d nodes | %s",
             GIGA_AP_IP, activeNodeCount,
             streamConnected ? "LIVE" : "NO STREAM");
    lv_label_set_text(lblStatusBar, buf);
    lv_obj_set_style_text_color(lblStatusBar, activeNodeCount > 0 ? C_GREEN : C_ORANGE, 0);
  }

  // IMU
  snprintf(buf, sizeof(buf), "Accel: X:%.2f  Y:%.2f  Z:%.2f g", accelX, accelY, accelZ);
  lv_label_set_text(lblAccel, buf);
  snprintf(buf, sizeof(buf), "Gyro:  X:%.1f  Y:%.1f  Z:%.1f dps", gyroX, gyroY, gyroZ);
  lv_label_set_text(lblGyro, buf);

  float totalAccel = abs(accelX) + abs(accelY) + abs(accelZ);
  float totalGyro = abs(gyroX) + abs(gyroY) + abs(gyroZ);
  float motThresh = settings.motionThreshold / 50.0f;

  const char *motion; lv_color_t mc;
  if (totalGyro > 100 * motThresh || totalAccel > 2.0f * motThresh) {
    motion = "Motion: ALERT!"; mc = C_RED;
    static unsigned long lastMotionAlert = 0;
    if (millis() - lastMotionAlert > 5000) { eventAlert("Motion detected — ALERT!"); lastMotionAlert = millis(); }
  } else if (totalGyro > 30 * motThresh || totalAccel > 1.3f * motThresh) {
    motion = "Motion: Vibration"; mc = C_ORANGE;
  } else {
    motion = imuReady ? "Motion: Idle" : "IMU: Not detected"; mc = imuReady ? C_GREEN : C_RED;
  }
  lv_label_set_text(lblMotion, motion);
  lv_obj_set_style_text_color(lblMotion, mc, 0);

  // Camera motion (from ESP32 frame differencing)
  if (wifiConnected && lastCamPoll > 0) {
    if (camMotionDetected) {
      snprintf(buf, sizeof(buf), "Camera: MOTION (%.1f%% / %lu)", camMotionScore, (unsigned long)camMotionCount);
      lv_label_set_text(lblCamMotion, buf);
      lv_obj_set_style_text_color(lblCamMotion, C_RED, 0);
    } else {
      snprintf(buf, sizeof(buf), "Camera: Clear (%.1f%% / %lu)", camMotionScore, (unsigned long)camMotionCount);
      lv_label_set_text(lblCamMotion, buf);
      lv_obj_set_style_text_color(lblCamMotion, C_GREEN, 0);
    }
  } else {
    lv_label_set_text(lblCamMotion, "Camera: No WiFi");
    lv_obj_set_style_text_color(lblCamMotion, C_TEXT_DIM, 0);
  }

  unsigned long secs = millis() / 1000;
  snprintf(buf, sizeof(buf), "Uptime: %lum %lus", secs / 60, secs % 60);
  lv_label_set_text(lblUptime, buf);

  // Window sensors
  windowOpen[0] = (totalAccel > 1.5f);
  windowOpen[1] = (abs(gyroY) > 50.0f);
  windowOpen[2] = (abs(gyroX) > 50.0f);
  for (int i = 0; i < 3; i++) {
    lv_led_set_color(ledWin[i], windowOpen[i] ? C_ORANGE : C_GREEN);
    lv_label_set_text(lblWin[i], windowOpen[i] ? "OPEN" : "CLOSED");
    lv_obj_set_style_text_color(lblWin[i], windowOpen[i] ? C_ORANGE : C_GREEN, 0);
  }
}

extern "C" char *sbrk(int incr);
static unsigned long freeMemory() {
  char top;
  return (unsigned long)(&top - reinterpret_cast<char*>(sbrk(0)));
}

void updateSettingsUI() {
  if (currentPage != PAGE_SETTINGS) return;
  char buf[128];

  if (wifiConnected) {
    snprintf(buf, sizeof(buf), "SSID: %s\nIP: %s\nStatus: Connected", AP_SSID, WiFi.localIP().toString().c_str());
  } else {
    snprintf(buf, sizeof(buf), "SSID: %s\nIP: --\nStatus: Disconnected", AP_SSID);
  }
  lv_label_set_text(lblSettingsWifi, buf);

  unsigned long secs = millis() / 1000;
  snprintf(buf, sizeof(buf), "Uptime: %luh %lum %lus", secs / 3600, (secs % 3600) / 60, secs % 60);
  lv_label_set_text(lblSysUptime, buf);

  snprintf(buf, sizeof(buf), "Free heap: %lu bytes", (unsigned long)freeMemory());
  lv_label_set_text(lblSysMem, buf);
}

// ============================================================
// Setup
// ============================================================

void setup() {
  Serial.begin(115200);
  delay(1000);
  Serial.println("\n=== GigaSenseHub Security App ===");

  // Load persistent settings from flash (or defaults on first boot)
  storageInit();
  Serial.print("Settings loaded — PIN: "); Serial.print(settings.pin);
  Serial.print(", AutoLock: "); Serial.print(settings.autoLockSecs);
  Serial.println("s");

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

  // Build all pages
  buildLoginPage();
  buildDashboardPage();
  buildSettingsPage();
  buildLogPage();

  // Start on login
  lv_screen_load(scrLogin);
  lv_timer_handler();

  // Connect WiFi AP — don't block the login screen
  // WiFi module needs ~2s after boot; retry in loop() handles it
  connectWiFi();
  lastActivity = millis();
  lastFpsCalc = millis();
  eventInfo("System booted");
  Serial.println("App ready — WiFi connects on dashboard entry");
}

// ============================================================
// Camera Motion Polling (HTTP GET /motion from ESP32)
// ============================================================

static void pollCameraMotion() {
  if (!wifiConnected || activeNodeCount == 0) return;

  // Poll each active node
  for (int n = 0; n < MAX_NODES; n++) {
    if (!nodes[n].active) continue;

    if (motionClient.connect(nodes[n].ip, STREAM_PORT)) {
      char req[64];
      snprintf(req, sizeof(req), "GET %s HTTP/1.0\r\nHost: %s\r\n\r\n",
               MOTION_PATH, nodes[n].ip.toString().c_str());
      motionClient.print(req);

    unsigned long start = millis();
    while (!motionClient.available() && millis() - start < 500) { delay(1); }

    // Skip HTTP headers
    bool bodyReached = false;
    String body = "";
    while (motionClient.available()) {
      String line = motionClient.readStringUntil('\n');
      if (bodyReached) {
        body += line;
      } else if (line == "\r" || line.length() == 0) {
        bodyReached = true;
      }
    }
    motionClient.stop();

    // Parse JSON manually (no ArduinoJson dependency — keep it lean)
    int detIdx = body.indexOf("\"detected\":");
    if (detIdx >= 0) {
      bool prevDetected = nodes[n].motionDetected;
      nodes[n].motionDetected = body.substring(detIdx + 11, detIdx + 15).startsWith("true");

      int scIdx = body.indexOf("\"score\":");
      if (scIdx >= 0) {
        int commaIdx = body.indexOf(',', scIdx + 8);
        if (commaIdx > scIdx) {
          nodes[n].motionScore = body.substring(scIdx + 8, commaIdx).toFloat();
        }
      }

      // Update global state (any node motion = global motion)
      camMotionDetected = false;
      camMotionScore = 0;
      for (int k = 0; k < MAX_NODES; k++) {
        if (nodes[k].active && nodes[k].motionDetected) camMotionDetected = true;
        if (nodes[k].active && nodes[k].motionScore > camMotionScore) camMotionScore = nodes[k].motionScore;
      }

      // Log camera motion events
      if (nodes[n].motionDetected && !prevDetected) {
        char lb[48];
        snprintf(lb, sizeof(lb), "Cam %d motion: %.1f%% changed", n, nodes[n].motionScore);
        eventAlert(lb);
      }
    }
    } // end if connect
  } // end for loop
}

// ============================================================
// Main Loop
// ============================================================

void loop() {
  unsigned long now = millis();
  lv_timer_handler();

  // Node scan — runs on any page (nodes connect asynchronously)
  if (wifiConnected && (now - lastNodeScan >= 30000 || lastNodeScan == 0)) {
    scanForNodes();
    lastNodeScan = now;
  }

  // WiFi Stream — only on dashboard
  if (currentPage == PAGE_DASHBOARD) {

    // Stream from active node
    if (activeNodeCount > 0) {
      if (!streamConnected) {
        connectStream();
      }
      if (streamConnected && readFrame()) {
        if (camFrameBuf) decodeFrame();
        frameCount++;
      }
    }
  }

  // WiFi AP retry if not connected
  static unsigned long lastWiFiRetry = 0;
  if (!wifiConnected && now - lastWiFiRetry > 5000) {
    Serial.print("WiFi retry... module status: ");
    Serial.println(WiFi.status());
    connectWiFi();
    lastWiFiRetry = now;
  }

  // FPS calc
  if (now - lastFpsCalc >= 1000) {
    currentFps = frameCount * 1000.0f / (now - lastFpsCalc);
    frameCount = 0; lastFpsCalc = now;
    char lb[80];
    snprintf(lb, sizeof(lb), "FPS: %.1f | Page: %d | Stream: %s | Nodes: %d | WiFi: %s",
             currentFps, currentPage, streamConnected ? "OK" : "NO",
             activeNodeCount, wifiConnected ? "AP" : "OFF");
    Serial.println(lb);
  }

  // IMU read
  static unsigned long lastIMU = 0;
  if (now - lastIMU >= 50) {
    BoschSensorClass &imu = imuOnWire1 ? myIMU : IMU;
    if (imu.accelerationAvailable()) imu.readAcceleration(accelX, accelY, accelZ);
    if (imu.gyroscopeAvailable()) imu.readGyroscope(gyroX, gyroY, gyroZ);
    lastIMU = now;
  }

  // Poll camera motion endpoint (~every 2 seconds)
  if (now - lastCamPoll >= 2000 && currentPage == PAGE_DASHBOARD) {
    pollCameraMotion();
    lastCamPoll = now;
  }

  // UI updates
  static unsigned long lastUI = 0;
  if (now - lastUI >= 500) {
    updateDashboardUI();
    updateSettingsUI();
    lastUI = now;
  }

  // Auto-lock: use LVGL's own inactivity tracker (immune to blocking calls)
  // lv_display_get_inactive_time returns ms since last touch input
  if (currentPage == PAGE_DASHBOARD || currentPage == PAGE_SETTINGS) {
    uint32_t idleMs = lv_display_get_inactive_time(NULL);
    if (settings.autoLockSecs > 0 && idleMs > (uint32_t)settings.autoLockSecs * 1000) {
      currentPage = PAGE_LOGIN;
      pinClear();
      lv_screen_load_anim(scrLogin, LV_SCR_LOAD_ANIM_FADE_ON, 500, 0, false);
      eventWarn("Auto-lock triggered (inactivity)");
      Serial.println("Auto-lock -> Login");
    }
  }
}
