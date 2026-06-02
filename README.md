<h1 align="center">GigaSenseHub</h1>

<p align="center">
  <strong>Multi-node wireless security dashboard — 2× ESP32-S3 cameras streaming to an Arduino GIGA R1 running a professional LVGL 9 touchscreen UI</strong>
</p>

<p align="center">
  <a href="https://github.com/sufianlearn/giga-sense-hub/actions"><img src="https://github.com/sufianlearn/giga-sense-hub/actions/workflows/ci.yml/badge.svg" alt="CI"></a>
  <img src="https://img.shields.io/badge/platform-PlatformIO-orange" alt="PlatformIO">
  <img src="https://img.shields.io/badge/MCU-STM32H747%20%2B%20ESP32--S3-blue" alt="MCU">
  <img src="https://img.shields.io/badge/UI-LVGL%209-green" alt="LVGL 9">
  <img src="https://img.shields.io/badge/license-MIT-lightgrey" alt="License">
</p>

<p align="center">
  <img src="assets/lock-screen.jpeg" width="250" />
  <img src="assets/main-screen.jpeg" width="250" />
  <img src="assets/settings-screen.jpeg" width="250" />
</p>

---

## Architecture

```
                          ┌──────────────────────────────────────────────┐
                          │     Arduino GIGA R1 WiFi + Display Shield    │
                          │                                              │
  ┌─────────────────┐     │  ┌──────────┐  ┌──────────┐  ┌───────────┐  │
  │  ESP32-S3 Cam 0 │◄────┤  │ WiFi AP  │  │ LVGL 9   │  │ FlashIAP  │  │
  │  192.168.3.3    │     │  │ Ch 6     │  │ 800×480  │  │ Settings  │  │
  │  MJPEG /stream  │     │  └──────────┘  └──────────┘  └───────────┘  │
  └─────────────────┘     │  ┌──────────┐  ┌──────────┐  ┌───────────┐  │
  ┌─────────────────┐     │  │ Node     │  │ TJpgDec  │  │ BMI270    │  │
  │  ESP32-S3 Cam 1 │◄────┤  │ Scanner  │  │ Decoder  │  │ IMU       │  │
  │  192.168.3.2    │     │  └──────────┘  └──────────┘  └───────────┘  │
  │  MJPEG /stream  │     │                                              │
  └─────────────────┘     └──────────────────────────────────────────────┘
         WiFi STA                        WiFi AP (192.168.3.1)
```

The GIGA runs as a **WiFi Access Point**. Both ESP32-S3 nodes connect as stations. The GIGA auto-discovers nodes via IP probe + `/info` JSON, switches between cameras on-tap, and streams MJPEG video decoded in real-time.

## Features

| Category | Details |
|----------|---------|
| **Multi-Camera** | 2× XIAO ESP32-S3 Sense nodes, live switch via ↻ button, dual grid view |
| **Touch UI** | 4 pages — Login, Dashboard, Settings, Event Log — LVGL 9 on 800×480 |
| **Security** | SHA-256 hashed PIN (mbed TLS), HMAC-SHA256 node authentication |
| **OTA Updates** | HTTP firmware upload to ESP32 nodes from the GIGA |
| **Power Mgmt** | 3-level sleep: Active → Light Sleep (60s) → Deep Sleep (5min) |
| **FreeRTOS** | 3 pinned tasks on ESP32: HTTP server, motion detection, housekeeping |
| **Motion Detect** | Frame-differencing on ESP32 (80×60 grayscale, EMA reference) |
| **Event Log** | Circular buffer with timestamps and severity (info/alert/error) |
| **Persistent Settings** | FlashIAP on STM32H747 with CRC32 integrity checks |
| **CI/CD** | GitHub Actions: matrix builds, size report, 18 native unit tests |
| **Hardware Docs** | KiCad schematic, BOM, pinout tables, power budget |

## Hardware

| Board | Qty | Role |
|-------|-----|------|
| [Arduino GIGA R1 WiFi](https://store.arduino.cc/products/giga-r1-wifi) + [Display Shield](https://store.arduino.cc/products/giga-display-shield) | 1 | AP + dashboard hub |
| [XIAO ESP32-S3 Sense](https://wiki.seeedstudio.com/xiao_esp32s3_getting_started/) | 2 | Camera nodes (STA) |

**No wiring between boards** — WiFi only. USB-C power each.

## Quick Start

### Prerequisites

- [PlatformIO CLI](https://platformio.org/install/cli)
- USB-C cables (3×)

### Build & Flash

```bash
# Clone
git clone https://github.com/sufianlearn/giga-sense-hub.git
cd giga-sense-hub

# Flash ESP32 Node 0 (plug into USB first)
cd esp32-sensor-node
pio run -e seeed_xiao_esp32s3 -t upload

# Flash ESP32 Node 1 (swap USB to second board)
pio run -e node1 -t upload

# Flash GIGA (may need double-tap reset for DFU mode)
cd ../giga-display-hub
pio run -t upload
```

### First Boot

1. Power all 3 boards via USB
2. GIGA creates AP `GigaSenseHub` (password: `gsense2024`)
3. Both ESP32 nodes auto-connect as stations
4. Enter PIN **1234** on the touchscreen to unlock
5. Live camera stream starts on the dashboard

## Project Structure

```
giga-sense-hub/
├── esp32-sensor-node/           # ESP32-S3 camera node firmware
│   ├── src/main.cpp             #   FreeRTOS tasks, MJPEG, motion, OTA, power mgmt
│   ├── platformio.ini           #   Two envs: node0 + node1
│   └── test/test_native/        #   18 Unity tests (protocol, CRC32, motion algo)
├── giga-display-hub/            # GIGA R1 display hub firmware
│   ├── src/main.cpp             #   LVGL UI, node scanner, stream client, IMU
│   ├── src/storage.h            #   FlashIAP settings (SHA-256 PIN, CRC32)
│   └── src/eventlog.h           #   Circular event buffer
├── shared/
│   └── protocol.h               #   Network config, endpoints, security constants
├── hardware/
│   ├── giga-sense-hub.kicad_sch #   KiCad 8 system schematic
│   └── README.md                #   BOM, pinout, power budget
├── .github/workflows/ci.yml     #   GitHub Actions CI pipeline
└── assets/                      #   Screenshots
```

## Network

| Setting | Value |
|---------|-------|
| SSID | `GigaSenseHub` |
| Password | `gsense2024` |
| GIGA AP IP | `192.168.3.1` |
| Node 0 IP | `192.168.3.3` |
| Node 1 IP | `192.168.3.2` |
| Stream | `http://<node_ip>/stream` |
| Node Info | `http://<node_ip>/info` |
| Motion | `http://<node_ip>/motion` |

## Security

- **PIN**: Stored as SHA-256 hash in flash — never plaintext after boot
- **HMAC Auth**: Sensitive endpoints (OTA upload, config) require `X-GSH-Auth` header with HMAC-SHA256 token + nonce for replay protection
- **Auto-Lock**: Screen locks after configurable idle timeout

## Performance

| Metric | Value |
|--------|-------|
| Video | 15 fps @ 320×240 QVGA |
| Motion Detection | 4 Hz (80×60 grayscale) |
| IMU | 20 Hz (BMI270) |
| GIGA RAM | ~46% |
| GIGA Flash | ~70% |
| ESP32 RAM | ~17% |
| ESP32 Flash | ~26% |
| Unit Tests | 18 (native, <1s) |

## Dependencies

| ESP32-S3 | GIGA R1 |
|----------|---------|
| ESP32 Camera Driver | LVGL 9.2 |
| WebServer | Arduino_H7_Video |
| ArduinoOTA | Arduino_GigaDisplayTouch |
| mbedtls (HMAC) | Arduino_BMI270_BMM150 |
| FreeRTOS (built-in) | TJpgDec (vendored) |
| | mbedtls (SHA-256, built-in) |
| | FlashIAP (mbed, built-in) |

## Running Tests

```bash
cd esp32-sensor-node
pio test -e native --filter test_native -v
```

Tests cover protocol constants, CRC32 correctness, and motion detection algorithm (thresholds, EMA update, edge cases).

## License

MIT
