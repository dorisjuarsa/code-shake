# 📚 Arduino Libraries for Waveshare ESP32-S3-Touch-LCD-1.83

Untuk memprogram controller botol Coca-Cola ini, pastikan board package ESP32 dan semua dependensi library berikut tersedia di Arduino IDE Anda:

---

## 1. Board Package ESP32 (Wajib Versi 3.x)
Buka **Tools** -> **Board** -> **Boards Manager...**, cari **`esp32` by Espressif Systems**:
* **Versi Wajib:** **`3.0.0` atau lebih baru** (saat ini teruji sangat stabil di versi `3.3.11`).
* *Catatan:* Jangan gunakan versi lama `2.0.x` karena fungsi kontrol backlight LCD menggunakan API modern ESP32 Core v3 (`ledcAttach`).

---

## 2. Pin Configuration (Mandiri)
File `pin_config.h` sudah disertakan langsung di dalam folder [`firmware/`](../firmware/pin_config.h). Anda **tidak perlu** menginstal library terpisah untuk konfigurasi pin LCD dan touch!

---

## 3. Library Resmi dari Arduino Library Manager
Semua library berikut adalah **library resmi 100% standar tanpa modifikasi file internal**. Buka **Tools** -> **Manage Libraries...** (`Ctrl + Shift + I`), lalu cari dan instal:

1. **`NimBLE-Arduino`** (oleh *h2zero*)
   - **Versi:** `2.0.0` atau lebih baru (teruji di `v2.5.1`).
2. **`GFX Library for Arduino`** (oleh *Moon On Our Nation*)
   - **Versi:** `1.4.9` atau lebih baru.
3. **`SensorLib`** (oleh *Lewis He*)
   - **Versi:** `0.1.6` atau lebih baru.
4. **`XPowersLib`** (oleh *Lewis He*)
   - **Versi:** `0.3.3` atau lebih baru.

---

## 4. Library Khusus Waveshare (DriveBus)
Khusus untuk library bus driver layar sentuh Waveshare **`Arduino_DriveBus`**, kami sediakan langsung di folder ini:
* **File:** [`libraries/Arduino_DriveBus.zip`](Arduino_DriveBus.zip)
* **Cara pasang:**
  1. Buka Arduino IDE.
  2. Pilih menu **Sketch** -> **Include Library** -> **Add .ZIP Library...**
  3. Pilih file `Arduino_DriveBus.zip`.

---

## 5. Konfigurasi Menu Tools di Arduino IDE
* **Board:** `ESP32S3 Dev Module`
* **Port:** (Pilih port USB botol Anda)
* **USB CDC On Boot:** `Enabled` *(Penting agar output Serial terbaca)*
* **Flash Size:** `16MB (128Mb)`
* **Partition Scheme:** `16M Flash (3MB APP/9.9MB FATFS)` atau default
* **PSRAM:** `OPI PSRAM`
* **Upload Speed:** `921600`
