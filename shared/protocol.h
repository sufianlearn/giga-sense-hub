#ifndef GIGA_SENSE_PROTOCOL_H
#define GIGA_SENSE_PROTOCOL_H

// --- Network Configuration ---
#define AP_SSID        "GigaSenseHub"
#define AP_PASSWORD    "sensehub32"
#define AP_CHANNEL     1

// ESP32 AP address (fixed when running as AP)
#define ESP32_IP       "192.168.4.1"

// --- HTTP Endpoints ---
#define STREAM_PORT    80
#define STREAM_PATH    "/stream"
#define MOTION_PATH    "/motion"

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

#endif // GIGA_SENSE_PROTOCOL_H
