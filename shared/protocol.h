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

// --- Stream Configuration ---
#define FRAME_WIDTH    320
#define FRAME_HEIGHT   240
#define JPEG_QUALITY   12   // 0-63, lower = better quality
#define TARGET_FPS     15

// --- MJPEG Boundary ---
#define MJPEG_BOUNDARY "frame"

#endif // GIGA_SENSE_PROTOCOL_H
