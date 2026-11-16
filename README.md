# Coca-Cola Bottle Shake Game & Battery Profiler 🍾⚡

Interactive IoT & Game platform using **Waveshare ESP32-S3-Touch-LCD-1.83** with Bluetooth Low Energy (BLE), real-time IMU motion tracking, power management (AXP2101 PMIC), and a desktop battery profiler suite.

---

## 📌 Project Overview

This project turns an interactive Coca-Cola bottle equipped with an ESP32-S3 into a wireless motion controller:
- **ESP32-S3 Firmware (`COKE_SHAKE.ino`)**:
  - **IMU**: QMI8658 6-axis motion sensor with peak-to-peak omnidirectional shake detection.
  - **Display**: ST7789P 1.83" LCD (240x280) showing animated bottle, shake counters, calibration sliders, and telemetry.
  - **Power Management**: AXP2101 PMIC with battery voltage/percentage readings, deep sleep mode, and IMU shake-to-wake.
  - **Wireless**: NimBLE Bluetooth Low Energy server broadcasting real-time shake packets and battery status.
- **Python Profiler & Test Bench (`main_gui.py`)**:
  - High-performance desktop tool built with **PyQt6** and **Bleak** (native Linux BlueZ).
  - Single auto-connect engine for wireless bottles.
  - Live RSSI (signal strength in dBm) monitor with status badges.
  - Live discharge & shake intensity curves rendered using silky-smooth **Catmull-Rom cubic Bezier splines**.
  - One-click session recording to CSV (`logs/battery_test_*.csv`) for battery endurance benchmarking.
- **Web Bluetooth Client (`GameDummy/index.html`)**:
  - Browser-based game interface connecting directly via Web Bluetooth API.

---

## 🚀 Quick Start (Python Profiler)

### 1. Requirements
- Linux (Ubuntu/Debian tested) with BlueZ Bluetooth stack
- Python 3.10+
- PulseAudio / PipeWire (for audio cues)

### 2. Setup Environment
```bash
# Clone the repository
git clone git@github.com:dorisjuarsa/code-shake.git
cd code-shake

# Create virtual environment and install dependencies
python3 -m venv .venv
source .venv/bin/activate
pip install -r requirements.txt
```

### 3. Run Application
```bash
chmod +x run_game.sh
./run_game.sh
```

---

## 📡 BLE Protocol Specification

- **Service UUID**: `19b10000-e8f2-537e-4f6c-d104768a1214`
- **Characteristic UUID**: `19b10001-e8f2-537e-4f6c-d104768a1214`
- **Payload Format**:
  ```text
  SHAKE,<packet_id>,<intensity>,<battery_pct>
  ```
  - `packet_id`: Incremental counter per shake event.
  - `intensity`: Motion force / intensity score ($0 - 100$).
  - `battery_pct`: Battery percentage reported by PMIC ($0 - 100\%$).

---

## 📁 Repository Structure

```text
├── COKE_SHAKE.ino             # Main ESP32-S3 Arduino firmware
├── main_gui.py                # PyQt6 BLE test bench & battery profiler
├── run_game.sh                # Launcher script
├── requirements.txt           # Python library dependencies
├── GameDummy/
│   └── index.html             # Web Bluetooth interface
├── sounds/                    # Audio assets (.wav)
├── logs/                      # Session battery logs (.csv)
├── coke_monitor_preview.png   # Preview snapshot
└── README.md
```
