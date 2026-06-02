#ifndef GIGA_SENSE_PROTOCOL_H
#define GIGA_SENSE_PROTOCOL_H

// ============================================================
// GigaSenseHub Protocol — Multi-Node Architecture
// ============================================================
//
// Network topology:
//   GIGA R1 (AP)  ←— WiFi —→  ESP32-S3 Node 1 (STA)
//                  ←— WiFi —→  ESP32-S3 Node 2 (STA)
//
// The GIGA runs as WiFi Access Point.
// ESP32 camera nodes connect as stations.
// Each node registers via GET /register on the GIGA (future),
// or the GIGA discovers them by scanning DHCP leases.
//

// --- Network Configuration ---
#define AP_SSID        "GigaSenseHub"
#define AP_PASSWORD    "sensehub32"
#define AP_CHANNEL     1

// GIGA AP IP (mbed WiFi default AP address)
#define GIGA_AP_IP     "192.168.3.1"

// --- Node Configuration ---
#define MAX_NODES      2         // Max camera nodes supported
#define NODE_BASE_PORT 80        // Each node serves on port 80

// ESP32 nodes connect to the GIGA AP as STA clients.
// They get DHCP addresses starting from 192.168.4.2.
// Each node identifies itself with a unique NODE_ID (0-based).

// --- HTTP Endpoints (served by each ESP32 node) ---
#define STREAM_PORT    80
#define STREAM_PATH    "/stream"
#define MOTION_PATH    "/motion"
#define NODE_INFO_PATH "/info"    // Node identification + capabilities

// --- OTA Configuration ---
#define OTA_HOSTNAME_PREFIX "gigasense-cam"  // hostname: gigasense-cam-0, gigasense-cam-1
#define OTA_PORT           3232
#define OTA_STATUS_PATH    "/ota/status"
#define OTA_UPLOAD_PATH    "/ota/upload"
#define FW_VERSION         "2.0.0"

// --- Stream Configuration ---
#define FRAME_WIDTH    320
#define FRAME_HEIGHT   240
#define JPEG_QUALITY   12   // 0-63, lower = better quality
#define TARGET_FPS     15

// --- Motion Detection ---
#define MOTION_WIDTH   80       // Downscaled for detection
#define MOTION_HEIGHT  60
#define MOTION_THRESHOLD_DEFAULT 15  // Per-pixel diff threshold
#define MOTION_PERCENT_DEFAULT   5   // % of pixels that must change

// --- MJPEG Boundary ---
#define MJPEG_BOUNDARY "frame"

// --- Power Management ---
#define IDLE_LIGHT_SLEEP_MS   60000   // 60s no motion → light sleep (WiFi stays)
#define IDLE_DEEP_SLEEP_MS   300000   // 5min no motion + no stream → deep sleep
#define DEEP_SLEEP_WAKE_US  30000000  // Wake every 30s from deep sleep to check motion
#define POWER_STATE_ACTIVE    0
#define POWER_STATE_LIGHT     1
#define POWER_STATE_DEEP      2

// --- Security ---
// HMAC-SHA256 shared secret for node authentication
// Both GIGA and ESP32 nodes use this to sign/verify HTTP requests
// In production, this should be per-device and provisioned during setup
#define HMAC_SECRET_KEY    "gsh_k3y_2026!s3cure"
#define AUTH_HEADER        "X-GSH-Auth"    // HTTP header for HMAC token
#define AUTH_NONCE_HEADER  "X-GSH-Nonce"   // Replay protection

#endif // GIGA_SENSE_PROTOCOL_H
