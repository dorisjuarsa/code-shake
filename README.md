# Coca-Cola Bottle Shake Game & Battery Profiler 🍾⚡

Interactive IoT & Game platform using **Waveshare ESP32-S3-Touch-LCD-1.83** with Bluetooth Low Energy (BLE), real-time IMU motion tracking, power management (AXP2101 PMIC), and a desktop battery profiler suite.

> 📘 **Manual Book Penggunaan Hardware**: Untuk panduan lengkap pengoperasian botol, tombol fisik, kalibrasi sensor, dan manajemen daya, silakan baca [**MANUAL_BOOK.md**](MANUAL_BOOK.md).

---

## 📌 Project Overview

This project turns an interactive Coca-Cola bottle equipped with an ESP32-S3 into a wireless motion controller:
- **ESP32-S3 Firmware (`firmware/firmware.ino`)**:
  - **IMU**: QMI8658 6-axis motion sensor with peak-to-peak omnidirectional shake detection.
  - **Display**: ST7789P 1.83" LCD (240x280) showing animated bottle, shake counters, calibration sliders, and telemetry.
  - **Power Management**: AXP2101 PMIC with battery voltage/percentage readings, deep sleep mode, and IMU shake-to-wake.
  - **Pinout**: Bundled directly via `firmware/pin_config.h` for out-of-the-box build support.
  - **Wireless**: NimBLE Bluetooth Low Energy server broadcasting real-time shake packets and battery status with instant advertising watchdog auto-reconnect.
- **Python Game & Battery Profiler (`game/main_gui.py`)**:
  - High-performance desktop tool built with **PyQt6** and **Bleak** (native Linux BlueZ).
  - Single auto-connect engine for wireless bottles.
  - Live RSSI (signal strength in dBm) monitor with status badges.
  - Live discharge & shake intensity curves rendered using silky-smooth **Catmull-Rom cubic Bezier splines**.
  - One-click session recording to CSV (`game/logs/battery_test_*.csv`) for battery endurance benchmarking.

---

## 🚀 Quick Start (Python Game Monitor)

### 1. Requirements
- Linux (Ubuntu/Debian tested) with BlueZ Bluetooth stack
- Python 3.10+
- PulseAudio / PipeWire (for audio cues)

### 2. Run Application (1-Click)
```bash
# Masuk ke folder game dan jalankan launcher
cd game
chmod +x run_game.sh
./run_game.sh
```
Script `run_game.sh` akan secara otomatis membuat virtual environment `.venv` dan menginstal semua dependensi dari `requirements.txt` jika belum terpasang.

---

## 🛠️ Arduino Firmware Setup

1. Buka file **`firmware/firmware.ino`** di **Arduino IDE 2.x**.
2. Instal library yang dibutuhkan sesuai panduan di [`libraries/README.md`](libraries/README.md).
3. Konfigurasi Board di menu **Tools**:
   - **Board**: `ESP32S3 Dev Module`
   - **USB CDC On Boot**: `Enabled`
   - **Flash Size**: `16MB (128Mb)`
   - **PSRAM**: `OPI PSRAM`
4. Tekan tombol **Upload** (`Ctrl + U`).

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
COKE_SHAKE/
├── firmware/               # Firmware ESP32-S3 Arduino
│   ├── firmware.ino        # Kode program utama botol
│   └── pin_config.h        # Pinout Waveshare ST7789P & CST816T
├── game/                   # Game Monitor & Profiler Python
│   ├── main_gui.py         # GUI PyQt6 & Bleak
│   ├── run_game.sh         # Launcher script otomatis
│   ├── requirements.txt    # Dependensi Python
│   ├── sounds/             # Sound effects (.wav)
│   └── logs/               # Log data sesi baterai (.csv)
├── libraries/              # Panduan & file library pendukung Arduino
│   ├── README.md           # Panduan instalasi library
│   └── Arduino_DriveBus.zip# Library Waveshare DriveBus
├── docs/                   # Aset gambar & dokumentasi
│   └── coke_monitor_preview.png
├── MANUAL_BOOK.md          # Buku panduan lengkap pengoperasian alat (Hardware)
├── .gitignore
└── README.md
```
