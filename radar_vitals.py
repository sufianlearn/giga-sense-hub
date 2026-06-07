#!/usr/bin/env python3
"""
RadarVitals — Contactless Vital Signs Monitor

Reads phase-difference data from PSoC6 + BGT60TR13C 60GHz FMCW radar,
extracts breathing rate and heart rate via DSP, and serves JSON over HTTP
for the GIGA R1 WiFi dashboard to consume.

Firmware outputs [V] lines at ~10Hz with phase differences per range bin.
Phase differences encode chest-wall displacement from breathing/heartbeat:
  - Breathing: 0.1–0.5 Hz (6–30 BPM)
  - Heart rate: 0.8–2.0 Hz (48–120 BPM)

HTTP endpoint: GET /vitals → JSON with all vital signs data
"""

import argparse
import collections
import json
import math
import re
import signal
import sys
import threading
import time
from http.server import HTTPServer, BaseHTTPRequestHandler

try:
    import serial
    import serial.tools.list_ports
except ImportError:
    print("ERROR: pyserial required. pip install pyserial")
    sys.exit(1)

try:
    import numpy as np
    from scipy import signal as scipy_signal
except ImportError:
    print("ERROR: numpy and scipy required. pip install numpy scipy")
    sys.exit(1)


# --- Config ---
SERIAL_BAUD = 115200
KITPROG3_VID = 0x04B4
KITPROG3_PID = 0xF155
HTTP_PORT = 8089
GIGA_SERIAL_PORT = '/dev/cu.usbmodem1301'  # GIGA R1 WiFi serial
GIGA_BAUD = 115200
GIGA_UPDATE_INTERVAL = 1.0  # seconds between updates to GIGA

# DSP parameters
SAMPLE_RATE = 10.0        # Hz (firmware outputs at ~10Hz)
WINDOW_SECONDS = 30       # seconds of data to keep for analysis
WINDOW_SIZE = int(SAMPLE_RATE * WINDOW_SECONDS)  # 300 samples
MIN_ANALYSIS_SAMPLES = 80  # ~8 seconds before first estimate

# Bandpass filter bands
BREATH_LOW = 0.1    # Hz → 6 BPM
BREATH_HIGH = 0.5   # Hz → 30 BPM
HEART_LOW = 0.8     # Hz → 48 BPM
HEART_HIGH = 2.0    # Hz → 120 BPM

# Range bin selection
NUM_BINS = 6
RANGE_RES_M = 3e8 / (2 * 460e6)  # ~0.326 m per bin


class VitalSignsDSP:
    """Extracts breathing rate and heart rate from radar phase data."""

    def __init__(self):
        self.phase_history = [collections.deque(maxlen=WINDOW_SIZE) for _ in range(NUM_BINS)]
        self.timestamp_history = collections.deque(maxlen=WINDOW_SIZE)
        self.sample_count = 0
        self.lock = threading.Lock()

        # Results
        self.breathing_bpm = 0.0
        self.heart_bpm = 0.0
        self.breathing_waveform = []
        self.heart_waveform = []
        self.presence = False
        self.target_bin = 0
        self.target_distance_m = 0.0
        self.target_magnitude = 0.0
        self.confidence_breath = 0.0
        self.confidence_heart = 0.0
        self.last_update_ms = 0

        # Design bandpass filters (Butterworth, 4th order)
        self._design_filters()

    def _design_filters(self):
        """Pre-design bandpass filters for breathing and heart rate."""
        nyq = SAMPLE_RATE / 2.0

        # Breathing filter: 0.1–0.5 Hz
        if BREATH_HIGH < nyq and BREATH_LOW > 0:
            self.breath_sos = scipy_signal.butter(
                4, [BREATH_LOW / nyq, BREATH_HIGH / nyq], btype='band', output='sos'
            )
        else:
            self.breath_sos = None

        # Heart rate filter: 0.8–2.0 Hz
        if HEART_HIGH < nyq and HEART_LOW > 0:
            self.heart_sos = scipy_signal.butter(
                4, [HEART_LOW / nyq, HEART_HIGH / nyq], btype='band', output='sos'
            )
        else:
            self.heart_sos = None

    def add_sample(self, timestamp_ms, phase_diffs, max_bin, max_mag):
        """Add a new phase sample from the radar."""
        with self.lock:
            self.timestamp_history.append(timestamp_ms)
            for i, pd in enumerate(phase_diffs):
                self.phase_history[i].append(pd)
            self.sample_count += 1
            self.target_bin = max_bin
            self.target_magnitude = max_mag
            self.target_distance_m = max_bin * RANGE_RES_M
            self.last_update_ms = timestamp_ms

            # Presence detection: magnitude above threshold
            self.presence = max_mag > 5.0  # Tune this threshold

            # Run analysis every ~1 second (every 10 samples)
            if self.sample_count % 10 == 0 and self.sample_count >= MIN_ANALYSIS_SAMPLES:
                self._analyze()

    def _analyze(self):
        """Run vital signs DSP analysis on accumulated phase data."""
        # Pick the range bin with highest average magnitude (most likely target)
        # Use target_bin from firmware as starting point
        best_bin = self.target_bin
        if best_bin < 1:
            best_bin = 1
        if best_bin >= NUM_BINS:
            best_bin = NUM_BINS - 1

        # Get phase data for target bin
        phase_data = np.array(self.phase_history[best_bin])
        n = len(phase_data)
        if n < MIN_ANALYSIS_SAMPLES:
            return

        # Cumulative phase (integrate phase differences) for displacement signal
        cum_phase = np.cumsum(phase_data)

        # Remove DC offset / slow drift with highpass (detrend)
        cum_phase = cum_phase - np.mean(cum_phase)

        # --- Breathing Rate ---
        if self.breath_sos is not None:
            breath_signal = scipy_signal.sosfiltfilt(self.breath_sos, cum_phase)
            self.breathing_waveform = breath_signal[-100:].tolist()  # last 10 seconds

            # Find dominant frequency via autocorrelation
            breath_bpm, breath_conf = self._estimate_rate(
                breath_signal, BREATH_LOW, BREATH_HIGH
            )
            if breath_conf > 0.15:
                self.breathing_bpm = breath_bpm
                self.confidence_breath = breath_conf

        # --- Heart Rate ---
        if self.heart_sos is not None:
            heart_signal = scipy_signal.sosfiltfilt(self.heart_sos, cum_phase)
            self.heart_waveform = heart_signal[-100:].tolist()  # last 10 seconds

            heart_bpm, heart_conf = self._estimate_rate(
                heart_signal, HEART_LOW, HEART_HIGH
            )
            if heart_conf > 0.1:
                self.heart_bpm = heart_bpm
                self.confidence_heart = heart_conf

    def _estimate_rate(self, sig, f_low, f_high):
        """Estimate dominant frequency in band using FFT peak detection."""
        n = len(sig)
        if n < 32:
            return 0.0, 0.0

        # Zero-pad for better frequency resolution
        nfft = max(512, n * 2)
        windowed = sig * np.hanning(n)
        spectrum = np.abs(np.fft.rfft(windowed, n=nfft))
        freqs = np.fft.rfftfreq(nfft, d=1.0 / SAMPLE_RATE)

        # Mask to band of interest
        mask = (freqs >= f_low) & (freqs <= f_high)
        if not np.any(mask):
            return 0.0, 0.0

        band_spectrum = spectrum[mask]
        band_freqs = freqs[mask]

        # Find peak
        peak_idx = np.argmax(band_spectrum)
        peak_freq = band_freqs[peak_idx]
        peak_mag = band_spectrum[peak_idx]

        # Confidence: ratio of peak to mean (signal-to-noise proxy)
        mean_mag = np.mean(band_spectrum)
        confidence = (peak_mag / mean_mag - 1.0) / 5.0 if mean_mag > 0 else 0
        confidence = min(max(confidence, 0.0), 1.0)

        bpm = peak_freq * 60.0
        return round(bpm, 1), round(confidence, 3)

    def get_vitals(self):
        """Return current vital signs as a dict."""
        with self.lock:
            return {
                "presence": self.presence,
                "target_bin": self.target_bin,
                "target_distance_m": round(self.target_distance_m, 2),
                "target_magnitude": round(self.target_magnitude, 2),
                "breathing_bpm": self.breathing_bpm,
                "breathing_confidence": self.confidence_breath,
                "heart_bpm": self.heart_bpm,
                "heart_confidence": self.confidence_heart,
                "breathing_waveform": [round(v, 6) for v in self.breathing_waveform[-50:]],
                "heart_waveform": [round(v, 6) for v in self.heart_waveform[-50:]],
                "samples_collected": self.sample_count,
                "firmware_timestamp_ms": self.last_update_ms,
                "sample_rate_hz": SAMPLE_RATE,
            }


class VitalsHTTPHandler(BaseHTTPRequestHandler):
    """Serves vital signs data over HTTP."""

    dsp = None  # Set by server setup

    def do_GET(self):
        if self.path == '/vitals' or self.path == '/':
            data = self.dsp.get_vitals()
            body = json.dumps(data).encode()
            self.send_response(200)
            self.send_header('Content-Type', 'application/json')
            self.send_header('Access-Control-Allow-Origin', '*')
            self.send_header('Content-Length', str(len(body)))
            self.end_headers()
            self.wfile.write(body)
        else:
            self.send_error(404)

    def log_message(self, format, *args):
        pass  # Suppress HTTP logging


def find_kitprog_port():
    """Auto-detect KitProg3 serial port."""
    ports = serial.tools.list_ports.comports()
    for p in ports:
        if p.vid == KITPROG3_VID and p.pid == KITPROG3_PID:
            return p.device
    # Fallback
    for p in ports:
        if 'usbmodem' in p.device and '1103' in p.device:
            return p.device
    return None


def serial_reader(port, dsp, running_event, giga_port=None):
    """Read serial data from PSoC6 and feed to DSP. Optionally push to GIGA."""
    pattern = re.compile(
        r'\[V\]\s+(\d+)'         # timestamp
        r'((?:\s+-?[\d.]+){6})'  # 6 phase diffs
        r'\s+(\d+)'              # max_bin
        r'\s+([\d.]+)'           # max_mag
    )

    giga_ser = None
    if giga_port:
        try:
            giga_ser = serial.Serial(giga_port, GIGA_BAUD, timeout=0.1)
            print(f"  GIGA serial: connected to {giga_port}")
        except serial.SerialException as e:
            print(f"  GIGA serial: failed to connect ({e}) — dashboard won't update")

    last_giga_update = 0

    try:
        ser = serial.Serial(port, SERIAL_BAUD, timeout=1)
        time.sleep(0.5)
        ser.reset_input_buffer()
        print(f"  Serial: connected to {port}")

        # Make sure we're not in settings mode — send ESC
        ser.write(b'\x1b')
        time.sleep(0.5)
        ser.reset_input_buffer()

        buffer = ""
        v_count = 0
        info_count = 0

        while running_event.is_set():
            if ser.in_waiting:
                data = ser.read(ser.in_waiting)
                text = data.decode('utf-8', errors='replace')
                # Strip ANSI escape codes
                text = re.sub(r'\x1b\[[^a-zA-Z]*[a-zA-Z]', '', text)
                buffer += text

                while '\n' in buffer:
                    line, buffer = buffer.split('\n', 1)
                    line = line.strip()

                    if line.startswith('[V]'):
                        m = pattern.match(line)
                        if m:
                            ts = int(m.group(1))
                            phases = [float(x) for x in m.group(2).split()]
                            max_bin = int(m.group(3))
                            max_mag = float(m.group(4))
                            dsp.add_sample(ts, phases, max_bin, max_mag)
                            v_count += 1

                            # Push to GIGA serial every GIGA_UPDATE_INTERVAL
                            now = time.time()
                            if giga_ser and now - last_giga_update >= GIGA_UPDATE_INTERVAL:
                                last_giga_update = now
                                vitals = dsp.get_vitals()
                                # Compact format: [RV] presence breath_bpm breath_conf heart_bpm heart_conf dist_m mag
                                giga_line = (
                                    f"[RV] {1 if vitals['presence'] else 0} "
                                    f"{vitals['breathing_bpm']:.1f} {vitals['breathing_confidence']:.2f} "
                                    f"{vitals['heart_bpm']:.1f} {vitals['heart_confidence']:.2f} "
                                    f"{vitals['target_distance_m']:.2f} {vitals['target_magnitude']:.1f}\n"
                                )
                                try:
                                    giga_ser.write(giga_line.encode())
                                except serial.SerialException:
                                    pass  # GIGA disconnected, non-fatal

                            if v_count % 100 == 0:
                                vitals = dsp.get_vitals()
                                br = vitals['breathing_bpm']
                                hr = vitals['heart_bpm']
                                bc = vitals['breathing_confidence']
                                hc = vitals['heart_confidence']
                                pres = "YES" if vitals['presence'] else "no"
                                print(f"  [{v_count}] Presence: {pres} | "
                                      f"Breath: {br:.1f} BPM ({bc:.0%}) | "
                                      f"Heart: {hr:.1f} BPM ({hc:.0%}) | "
                                      f"Dist: {vitals['target_distance_m']:.2f}m")

                    elif line.startswith('[INFO]'):
                        info_count += 1
                        if info_count <= 3:
                            print(f"  Radar: {line}")
            else:
                time.sleep(0.02)

    except serial.SerialException as e:
        print(f"  Serial error: {e}")
    except Exception as e:
        print(f"  Reader error: {e}")
    finally:
        if 'ser' in dir() and ser.is_open:
            ser.close()


def main():
    parser = argparse.ArgumentParser(description="RadarVitals — Contactless Vital Signs Monitor")
    parser.add_argument('--port', '-p', default=None, help='Serial port (auto-detect)')
    parser.add_argument('--http-port', type=int, default=HTTP_PORT, help=f'HTTP port (default: {HTTP_PORT})')
    parser.add_argument('--giga-port', default=None, help=f'GIGA serial port for dashboard push (default: {GIGA_SERIAL_PORT})')
    parser.add_argument('--no-giga', action='store_true', help='Disable GIGA serial push')
    args = parser.parse_args()

    # Find serial port
    port = args.port or find_kitprog_port()
    if not port:
        print("ERROR: No KitProg3 device found. Connect CY8CKIT-062S2-AI via USB.")
        sys.exit(1)

    # Initialize DSP
    dsp = VitalSignsDSP()

    # Set up HTTP server
    VitalsHTTPHandler.dsp = dsp
    httpd = HTTPServer(('0.0.0.0', args.http_port), VitalsHTTPHandler)
    httpd_thread = threading.Thread(target=httpd.serve_forever, daemon=True)

    # Running flag
    running = threading.Event()
    running.set()

    def shutdown(sig, frame):
        print("\n  Shutting down...")
        running.clear()
        httpd.shutdown()

    signal.signal(signal.SIGINT, shutdown)
    signal.signal(signal.SIGTERM, shutdown)

    print()
    print("=" * 60)
    print("  RadarVitals — Contactless Vital Signs Monitor")
    print(f"  Radar: {port}")
    print(f"  HTTP:  http://0.0.0.0:{args.http_port}/vitals")
    print(f"  DSP:   {SAMPLE_RATE}Hz sample rate, {WINDOW_SECONDS}s window")
    print(f"  Bands: Breath {BREATH_LOW}-{BREATH_HIGH}Hz, Heart {HEART_LOW}-{HEART_HIGH}Hz")
    print("  Press Ctrl+C to stop")
    print("=" * 60)
    print()

    # Start HTTP server
    httpd_thread.start()
    print(f"  HTTP server running on port {args.http_port}")

    # Start serial reader (blocks until stopped)
    giga_port = None
    if not args.no_giga:
        giga_port = args.giga_port or GIGA_SERIAL_PORT
    serial_reader(port, dsp, running, giga_port=giga_port)

    print("  Done.")


if __name__ == '__main__':
    main()
