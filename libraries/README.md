# 📚 Arduino Libraries for Waveshare ESP32-S3-Touch-LCD-1.83

Untuk memprogram controller botol Coca-Cola ini, pastikan board ESP32 sudah terpasang dan semua dependensi library berikut tersedia di Arduino IDE Anda:

---

## 1. Pin Configuration (Mandiri)
File `pin_config.h` sudah disertakan langsung di dalam folder [`firmware/`](../firmware/pin_config.h). Anda **tidak perlu** menginstal library terpisah untuk konfigurasi pin LCD dan touch!

---

## 2. Library yang Tersedia di Library Manager Arduino IDE
Buka Arduino IDE -> **Tools** -> **Manage Libraries...** (`Ctrl + Shift + I`), lalu cari dan instal:

1. **`GFX Library for Arduino`** (oleh *Moon On Our Nation*)
2. **`NimBLE-Arduino`** (oleh *h2zero*)
3. **`SensorLib`** (oleh *lewis he*)
4. **`XPowersLib`** (oleh *lewis he*)

---

## 3. Library Tambahan (Waveshare DriveBus)
Library **`Arduino_DriveBus`** disediakan langsung di folder ini dalam bentuk zip:
* **File:** [`libraries/Arduino_DriveBus.zip`](Arduino_DriveBus.zip)
* **Cara pasang:**
  1. Buka Arduino IDE.
  2. Pilih menu **Sketch** -> **Include Library** -> **Add .ZIP Library...**
  3. Pilih file `Arduino_DriveBus.zip`.

---

## 4. Konfigurasi Board (Arduino IDE)
* **Board:** `ESP32S3 Dev Module`
* **Port:** (Pilih port USB botol Anda)
* **USB CDC On Boot:** `Enabled` (Sangat penting agar Serial Monitor terbaca)
* **Flash Size:** `16MB (128Mb)`
* **Partition Scheme:** `16M Flash (3MB APP/9.9MB FATFS)` atau default
* **PSRAM:** `OPI PSRAM`
* **Upload Speed:** `921600`
