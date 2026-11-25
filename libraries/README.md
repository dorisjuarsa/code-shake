# 📚 Arduino Libraries for Waveshare ESP32-S3-Touch-LCD-1.83

Folder ini berisi **seluruh library Arduino pendukung** yang digunakan untuk mengompilasi dan mengunggah kode program firmware botol Coca-Cola (`firmware/firmware.ino`).

Semua file library telah disediakan dalam bentuk `.zip` siap pakai di folder ini sehingga tidak perlu mencari atau mengunduh satu per satu secara manual.

---

## 📦 Daftar File Library yang Disediakan

| File ZIP | Nama Library & Versi | Pembuat / Sumber | Deskripsi & Fungsi |
| :--- | :--- | :--- | :--- |
| [**`ALL_LIBRARIES.zip`**](ALL_LIBRARIES.zip) | **Paket Komplit (5-in-1)** | *Semua Library* | **Rekomendasi!** Berisi seluruh 5 folder library dalam 1 file ZIP untuk instalasi instan. |
| [**`Arduino_DriveBus.zip`**](Arduino_DriveBus.zip) | `Arduino_DriveBus` | *Waveshare* | Driver bus hardware I2C/SPI dan controller touch CST816T. |
| [**`GFX_Library_for_Arduino.zip`**](GFX_Library_for_Arduino.zip) | `GFX Library for Arduino` (v1.4.9) | *Moon On Our Nation* | Engine grafis layar LCD ST7789P 240x284 px. |
| [**`NimBLE-Arduino.zip`**](NimBLE-Arduino.zip) | `NimBLE-Arduino` (v2.5.1) | *h2zero* | Stack Bluetooth Low Energy (BLE) hemat daya & ultra responsif. |
| [**`SensorLib.zip`**](SensorLib.zip) | `SensorLib` (v0.1.6) | *Lewis He* | Driver IMU 6-axis QMI8658 (akselerometer & giroskop). |
| [**`XPowersLib.zip`**](XPowersLib.zip) | `XPowersLib` (v0.3.3) | *Lewis He* | Driver Power Management IC (PMIC) AXP2101 baterai lithium. |

---

## 🔍 Pemetaan Lengkap Header (`#include`) pada `firmware.ino`

Berikut penjelasan asal-usul setiap baris `#include` yang ada di `firmware/firmware.ino`:

| Baris Kode `#include` | Sumber / Asal File | Status Tindakan |
| :--- | :--- | :--- |
| `#include "Arduino_DriveBus_Library.h"` | Dari [**`Arduino_DriveBus.zip`**](Arduino_DriveBus.zip) | **Wajib diinstal** (tersedia di folder ini) |
| `#include "Arduino_GFX_Library.h"` | Dari [**`GFX_Library_for_Arduino.zip`**](GFX_Library_for_Arduino.zip) | **Wajib diinstal** (tersedia di folder ini) |
| `#include "SensorQMI8658.hpp"` | Dari [**`SensorLib.zip`**](SensorLib.zip) | **Wajib diinstal** (tersedia di folder ini) |
| `#include "XPowersLib.h"` | Dari [**`XPowersLib.zip`**](XPowersLib.zip) | **Wajib diinstal** (tersedia di folder ini) |
| `#include <NimBLEDevice.h>` | Dari [**`NimBLE-Arduino.zip`**](NimBLE-Arduino.zip) | **Wajib diinstal** (tersedia di folder ini) |
| `#include <NimBLEServer.h>` | Dari [**`NimBLE-Arduino.zip`**](NimBLE-Arduino.zip) | **Wajib diinstal** (tersedia di folder ini) |
| `#include "pin_config.h"` | File lokal di folder `firmware/pin_config.h` | **Otomatis ada** (sudah ada di repo project) |
| `#include "driver/gpio.h"` | Bawaan resmi ESP32 Board Core | **Otomatis ada** (tidak perlu install apapun) |
| `#include "driver/rtc_io.h"` | Bawaan resmi ESP32 Board Core | **Otomatis ada** (tidak perlu install apapun) |
| `#include "esp_sleep.h"` | Bawaan resmi ESP32 Board Core | **Otomatis ada** (tidak perlu install apapun) |
| `#include <Arduino.h>` | Bawaan resmi ESP32 Board Core | **Otomatis ada** (tidak perlu install apapun) |
| `#include <Preferences.h>` | Bawaan resmi ESP32 Board Core | **Otomatis ada** (tidak perlu install apapun) |
| `#include <Wire.h>` | Bawaan resmi ESP32 Board Core | **Otomatis ada** (tidak perlu install apapun) |

> 💡 **Kesimpulan:** Dari 13 baris `#include`, **hanya 5 library eksternal** yang perlu dipasang. Semuanya sudah dikemas dalam [**`ALL_LIBRARIES.zip`**](ALL_LIBRARIES.zip).

---

## 🚀 Panduan Cara Instalasi Library

Pilih salah satu dari 2 metode instalasi berikut:

### Metode 1: Ekstrak Langsung (Paling Cepat & Direkomendasikan) ⚡
1. Unduh / buka file [**`ALL_LIBRARIES.zip`**](ALL_LIBRARIES.zip).
2. Ekstrak seluruh 5 folder di dalamnya langsung ke direktori `libraries` milik Arduino:
   - **Windows**: `C:\Users\<Nama_User>\Documents\Arduino\libraries\`
   - **macOS**: `/Users/<Nama_User>/Documents/Arduino/libraries/`
   - **Linux**: `/home/<Nama_User>/Arduino/libraries/`
3. Restart / buka kembali Arduino IDE Anda. Semua library langsung siap digunakan!

---

### Metode 2: Melalui Menu Arduino IDE (Add .ZIP Library) 📂
Jika ingin menginstal satu per satu melalui antarmuka Arduino IDE:
1. Buka Arduino IDE.
2. Buka menu: **Sketch** -> **Include Library** -> **Add .ZIP Library...**
3. Pilih salah satu file `.zip` (misal `Arduino_DriveBus.zip`).
4. Ulangi langkah di atas untuk 4 file ZIP lainnya (`GFX_Library_for_Arduino.zip`, `NimBLE-Arduino.zip`, `SensorLib.zip`, dan `XPowersLib.zip`).

---

## ⚙️ Persyaratan Board Package ESP32 (Wajib Versi 3.x)

1. Buka Arduino IDE -> **Tools** -> **Board** -> **Boards Manager...**
2. Cari package: **`esp32` by Espressif Systems**.
3. **Versi Wajib**: Pilih versi **`3.0.0` atau yang lebih baru** (telah teruji sangat stabil pada versi `3.0.x` s.d. `3.3.x`).
   > [!WARNING]
   > Jangan gunakan versi lama `2.0.x` karena fungsi backlight LCD pada firmware ini menggunakan API PWM modern ESP32 Core v3 (`ledcAttach`).

---

## 🛠️ Konfigurasi Menu Tools di Arduino IDE

Sebelum menekan tombol **Upload** (`Ctrl + U`), pastikan pengaturan di menu **Tools** sudah sesuai:

| Parameter | Nilai Pengaturan | Keterangan Penting |
| :--- | :--- | :--- |
| **Board** | `ESP32S3 Dev Module` | Tipe modul mikrokontroler |
| **Port** | *(Pilih port COM / ttyACM botol)* | Terdeteksi saat kabel USB-C tersambung |
| **USB CDC On Boot** | **`Enabled`** | **Wajib di-enable** agar Serial Monitor dan debugging terbaca |
| **Flash Size** | `16MB (128Mb)` | Kapasitas flash memori Waveshare |
| **Partition Scheme** | `16M Flash (3MB APP/9.9MB FATFS)` atau `Default 4MB` | Skema partisi memori |
| **PSRAM** | **`OPI PSRAM`** | **Wajib di-set OPI** untuk buffer layar dan grafis |
| **Upload Speed** | `921600` | Kecepatan upload firmware |
