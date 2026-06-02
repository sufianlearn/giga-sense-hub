# GigaSenseHub Hardware Design

## System Architecture

```
  ┌─────────────────────────────────────────────────┐
  │            USB-C (5V)                            │
  │               │                                  │
  │  ┌────────────┴────────────────────┐             │
  │  │    ARDUINO GIGA R1 WiFi         │             │
  │  │    STM32H747XI (M7+M4)          │             │
  │  │                                 │             │
  │  │  ┌─────────┐  ┌──────────────┐  │             │
  │  │  │ WiFi AP │  │ SDRAM (8MB)  │  │             │
  │  │  │CYW4343W │  │ LVGL + JPEG  │  │             │
  │  │  │  Ch 6   │  │ framebuffers │  │             │
  │  │  └────┬────┘  └──────────────┘  │             │
  │  │       │        ┌──────────────┐ │  ┌────────────────────┐
  │  │       │        │ FlashIAP     │ │  │ GIGA Display Shield│
  │  │       │        │ Settings     │ │  │                    │
  │  │       │        │ @0x081E0000  │ │  │ TFT 800x480 RGB565│
  │  │       │        └──────────────┘ ├──┤ Touch: GT911 (I2C)│
  │  │       │                         │  │ IMU: BMI270 (SPI) │
  │  │       │                         │  │ Mic: MP34DT06J    │
  │  └───────┼─────────────────────────┘  └────────────────────┘
  │           │
  │     802.11n (WPA2-PSK)
  │     SSID: GigaSenseHub
  │     Net: 192.168.3.0/24
  │           │
  │    ┌──────┴──────┐
  │    │             │
  │    ▼             ▼
  │ ┌──────────────────┐  ┌──────────────────┐
  │ │XIAO ESP32-S3     │  │XIAO ESP32-S3     │
  │ │Sense — Node 0    │  │Sense — Node 1    │
  │ │                  │  │                  │
  │ │ CPU: LX7 240MHz  │  │ CPU: LX7 240MHz  │
  │ │ PSRAM: 8MB OPI   │  │ PSRAM: 8MB OPI   │
  │ │ Camera: OV2640   │  │ Camera: OV2640   │
  │ │ QVGA 320x240     │  │ QVGA 320x240     │
  │ │                  │  │                  │
  │ │ HTTP Endpoints:  │  │ HTTP Endpoints:  │
  │ │  /stream (MJPEG) │  │  /stream (MJPEG) │
  │ │  /motion (JSON)  │  │  /motion (JSON)  │
  │ │  /info   (JSON)  │  │  /info   (JSON)  │
  │ │  /ota/*  (update)│  │  /ota/*  (update)│
  │ │                  │  │                  │
  │ │ Power States:    │  │ Power States:    │
  │ │  Active: 240MHz  │  │  Active: 240MHz  │
  │ │  Light:   80MHz  │  │  Light:   80MHz  │
  │ │  Deep: timer 30s │  │  Deep: timer 30s │
  │ │                  │  │                  │
  │ │ USB-C (5V)       │  │ USB-C (5V)       │
  │ └──────────────────┘  └──────────────────┘
  └─────────────────────────────────────────────────┘
```

## Bill of Materials

| Ref | Component | Qty | Description |
|-----|-----------|-----|-------------|
| U1 | Arduino GIGA R1 WiFi | 1 | Hub controller — STM32H747XI dual-core, CYW4343W WiFi |
| U2 | Arduino GIGA Display Shield | 1 | 800x480 TFT + GT911 touch + BMI270 IMU |
| U3 | Seeed XIAO ESP32-S3 Sense | 1 | Camera node 0 — OV2640 + 8MB PSRAM |
| U4 | Seeed XIAO ESP32-S3 Sense | 1 | Camera node 1 — OV2640 + 8MB PSRAM |
| J1-J3 | USB-C cable | 3 | Power + programming (one per board) |

## Pin Mapping

### GIGA R1 ↔ Display Shield (40-pin connector)
- RGB565 parallel: D0-D15 + HSYNC/VSYNC/DE/CLK
- Touch I2C: SDA (PB7), SCL (PB6), INT (PB5)
- IMU SPI: MOSI/MISO/SCK/CS + INT1/INT2
- Microphone PDM: CLK + DATA

### ESP32-S3 Sense Camera (DVP 8-bit)
| Signal | GPIO |
|--------|------|
| XCLK | 10 |
| SIOD (SDA) | 40 |
| SIOC (SCL) | 39 |
| Y9-Y2 | 48,11,12,14,16,18,17,15 |
| VSYNC | 38 |
| HREF | 47 |
| PCLK | 13 |

## Network Protocol

```
GIGA (AP: 192.168.3.1)
  │
  ├── Scan: GET /info → {"nodeId":N, "powerState":"...", ...}
  │         Interval: 10s (only when not streaming)
  │         Timeout: 500ms connect, 800ms response
  │
  ├── Stream: GET /stream → multipart/x-mixed-replace
  │           Boundary: "frame"
  │           Resolution: 320x240 JPEG
  │           Target FPS: 15
  │
  ├── Motion: GET /motion → {"detected":bool, "score":float, ...}
  │           Interval: 5s (only when not streaming)
  │
  └── OTA: POST /ota/upload (multipart firmware binary)
           GET  /ota/status → {"progress":int, "error":"..."}
```

## Power Budget

| Component | Active | Light Sleep | Deep Sleep |
|-----------|--------|-------------|------------|
| GIGA R1 + Display | ~800mA @ 5V | — | — |
| ESP32 Node (each) | ~180mA @ 5V | ~80mA | ~10µA |
| **Total (3 boards)** | **~1.16A** | **~960mA** | **~800mA** |

Transition thresholds:
- Active → Light: 60s no motion, no stream client
- Light → Deep: 5min no motion, no stream, no OTA
- Deep → Active: Timer wakeup every 30s
