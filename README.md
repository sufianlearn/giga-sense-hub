# GigaSenseHub

A wireless home security dashboard powered by two microcontrollers. An ESP32-S3 streams live video over WiFi, while an Arduino GIGA R1 renders a professional LVGL 9 touchscreen UI with camera feed, door lock controls, window sensors, and real-time IMU data.

```
┌──────────────────────────┐       WiFi (AP)       ┌──────────────────────────┐
│  XIAO ESP32-S3 Sense     │◄──────────────────────│  Arduino GIGA R1 WiFi    │
│  (Camera Node)           │  Station connects     │  + GIGA Display Shield   │
│                          │  to ESP32's AP        │  (Security Dashboard)    │
│  - OV2640 Camera         │                       │                          │
│  - MJPEG @ 320x240 15fps │──── /stream ────────► │  - 800x480 Touch LCD     │
│  - WiFi Access Point     │                       │  - LVGL 9 UI             │
│                          │                       │  - BMI270 IMU            │
└──────────────────────────┘                       │  - TJpgDec JPEG decode   │
                                                   └──────────────────────────┘
```

## Dashboard Features

| Feature | Description |
|---------|-------------|
| **Live Camera Feed** | MJPEG stream decoded in real-time, displayed via LVGL image widget backed by SDRAM buffer |
| **Door Locks** | 3 interactive toggle switches (Front Door, Back Door, Garage) with LED status indicators |
| **Window Sensors** | 3 window status indicators driven by IMU vibration detection (Living Room, Bedroom, Kitchen) |
| **IMU Data** | Real-time accelerometer and gyroscope readings from the GIGA's onboard BMI270 |
| **Motion Detection** | Automatic alert states (Idle / Vibration / ALERT!) based on IMU thresholds |
| **System Status** | WiFi connection, stream status, FPS counter, uptime display |
| **Touch Control** | Full touch interaction via GT911 capacitive touch controller |

## Dashboard Layout

```
┌──────────────────────────────────────────────────────────────┐
│  🏠 GigaSenseHub Security                    📶 192.168.4.2 │
├────────────────────────┬─────────────────────────────────────┤
│  📹 Camera 1    12 fps │  ✕ Door Locks                      │
│ ┌──────────────────┐   │  ● Front Door        [====]        │
│ │                  │   │  ● Back Door         [====]        │
│ │   Live Video     │   │  ● Garage            [====]        │
│ │   320 x 240      │   ├─────────────────────────────────────┤
│ │                  │   │  ⚠ Window Sensors                  │
│ └──────────────────┘   │  ● Living Room       CLOSED        │
│  ▶ LIVE                │  ● Bedroom           CLOSED        │
├────────────────────────┤  ● Kitchen           CLOSED        │
│  📍 GIGA IMU (BMI270)  ├─────────────────────────────────────┤
│  Accel: 0.01 -0.01 1.0 │  ⚙ System                         │
│  Gyro:  0.0  0.0   0.0 │  ● System Armed - All Active      │
│  Motion: Idle           │                                    │
│  Uptime: 5m 32s         │                                    │
└────────────────────────┴─────────────────────────────────────┘
```

## Project Structure

```
giga-sense-hub/
├── shared/
│   └── protocol.h                  # Network config, endpoints, stream params
├── esp32-sensor-node/
│   ├── platformio.ini              # PlatformIO config (espressif32)
│   └── src/
│       └── main.cpp                # WiFi AP, MJPEG stream, sensor REST API
├── giga-display-hub/
│   ├── platformio.ini              # PlatformIO config (ststm32 + LVGL 9)
│   └── src/
│       ├── main.cpp                # LVGL dashboard, JPEG decode, IMU, WiFi
│       ├── lv_conf.h               # LVGL config selector
│       ├── lv_conf_9.h             # LVGL 9 config (fonts, widgets, memory)
│       ├── lv_conf_8.h             # LVGL 8 config (fallback)
│       ├── tjpgd.c / tjpgd.h      # Tiny JPEG Decoder (direct source)
│       └── tjpgdcnf.h             # TJpgDec config (RGB565, fast decode)
├── .env                            # WiFi credentials (git-ignored)
├── .gitignore
└── README.md
```

## Hardware

| Component | Role | Interface |
|-----------|------|-----------|
| [Arduino GIGA R1 WiFi](https://store.arduino.cc/products/giga-r1-wifi) | Security dashboard hub | USB-C |
| [GIGA Display Shield](https://store.arduino.cc/products/giga-display-shield) | 800x480 capacitive touchscreen | Shield (I2C + DSI) |
| [XIAO ESP32-S3 Sense](https://wiki.seeedstudio.com/xiao_esp32s3_getting_started/) | Wireless camera node | USB-C |

No wiring between boards — they communicate over WiFi. Each board only needs USB power.

## Architecture

### ESP32-S3 Sense (Camera Node)

Runs as a **WiFi Access Point** (`GigaSenseHub` / `sensehub32`) and serves:

- **`/stream`** — MJPEG video stream (QVGA 320x240, ~15 fps, quality 12)

The streaming handler runs in a blocking loop per client, sending JPEG frames with rate limiting. The ESP32 is a pure camera node with no sensor processing.

### GIGA R1 (Security Dashboard)

Connects as a **WiFi station** to the ESP32's AP and runs the LVGL 9 UI:

1. **Display**: `Arduino_H7_Video` initializes the 480x800 portrait panel with LVGL, handling 270-degree rotation to landscape 800x480
2. **Video**: MJPEG frames are received over HTTP, decoded with TJpgDec into an SDRAM-backed RGB565 buffer, and displayed via `lv_image` widget
3. **IMU**: BMI270 accelerometer/gyroscope read in continuous mode on `Wire1` (Wire is used by the touch controller GT911)
4. **Touch**: `Arduino_GigaDisplayTouch` provides LVGL touch input callbacks
5. **UI Updates**: Dashboard labels refresh every 500ms; video frames decode as fast as they arrive

### Key Technical Decisions

| Decision | Reason |
|----------|--------|
| TJpgDec source included directly | Avoids pulling in SD.h dependency from the TJpg_Decoder Arduino library |
| IMU on Wire1 | Wire is shared with GT911 touch controller; concurrent I2C access caused zero readings |
| SDRAM for video buffer | 320x240x2 = 150KB too large for STM32H7's 512KB SRAM; SDRAM has 8MB |
| ESP32 is camera-only | No sensor processing on ESP32; all sensing done on the GIGA's BMI270 IMU |
| LVGL image widget (not canvas) | Canvas requires LVGL memory pool allocation; image widget uses external SDRAM buffer directly |

## Setup

### Prerequisites

- [PlatformIO CLI](https://platformio.org/install/cli) or PlatformIO IDE
- USB cables for both boards

### Build & Flash

**1. Flash the ESP32-S3 Sense:**

```bash
cd esp32-sensor-node
pio run -t upload
```

**2. Flash the GIGA R1:**

The GIGA uses DFU upload — you need to **double-tap the reset button** to enter bootloader mode before uploading:

```bash
cd giga-display-hub
pio run -t upload    # double-tap reset first!
```

If upload fails with "No DFU capable USB device available", double-tap reset again and retry immediately.

### Run

1. Power on the ESP32-S3 — it starts the WiFi AP immediately
2. Power on the GIGA R1 — it connects to the ESP32's AP and displays the dashboard
3. The live camera feed appears within a few seconds
4. Touch the door lock switches to toggle them
5. Shake the GIGA board to trigger motion alerts and window sensor changes

### Serial Monitor

```bash
# ESP32
cd esp32-sensor-node && pio device monitor

# GIGA
cd giga-display-hub && pio device monitor
```

## Network

| Setting | Value |
|---------|-------|
| SSID | `GigaSenseHub` |
| Password | `sensehub32` |
| ESP32 IP | `192.168.4.1` |
| Stream URL | `http://192.168.4.1/stream` |

## Tuning

| Parameter | File | Default |
|-----------|------|---------|
| Video resolution | `shared/protocol.h` | 320x240 (QVGA) |
| JPEG quality | `shared/protocol.h` | 12 (0-63, lower = better) |
| Target FPS | `shared/protocol.h` | 15 |
| WiFi credentials | `shared/protocol.h` | GigaSenseHub / sensehub32 |
| LVGL memory pool | `giga-display-hub/src/lv_conf_9.h` | 128 KB |
| LVGL fonts enabled | `giga-display-hub/src/lv_conf_9.h` | Montserrat 12-32 |
| Motion alert threshold | `giga-display-hub/src/main.cpp` | >2.0g accel or >100 dps gyro |

## Dependencies

### ESP32 Camera Node

| Library | Version |
|---------|---------|
| ESP32 Camera Driver | Built-in |
| WebServer | Built-in |

### GIGA Display Hub

| Library | Version |
|---------|---------|
| LVGL | ^9 (9.5.0) |
| Arduino_GigaDisplayTouch | ^1 |
| Arduino_BMI270_BMM150 | ^1 |
| Arduino_H7_Video | Built-in |
| TJpg_Decoder (tjpgd) | Vendored source |

## Performance

Measured on hardware:

| Metric | Value |
|--------|-------|
| Video FPS | 10-15 fps |
| Video latency | ~100-200ms |
| IMU sample rate | 20 Hz (50ms interval) |
| UI refresh rate | 2 Hz (500ms interval) |
| GIGA RAM usage | 45% (236 KB / 524 KB) |
| GIGA Flash usage | 68% (535 KB / 786 KB) |

## License

MIT
