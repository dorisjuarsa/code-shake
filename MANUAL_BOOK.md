# 📖 Panduan Pengoperasian Alat (Manual Book Hardware)
## Interactive Coca-Cola Bottle Motion Controller 🍾⚡

---

## ⚡ Panduan Ringkas (Quick Start Guide)

| Langkah | Aksi | Keterangan |
| :--- | :--- | :--- |
| **1. Nyalakan** | Tahan tombol **PWR** (samping) 1 detik, atau colok charger USB-C. | Jika botol dalam mode tidur (*Sleep*), **cukup kocok botol** atau tekan tombol **BOOT** 1x. |
| **2. Konek ke Game** | Nyalakan game di laptop/PC. | Botol otomatis terkoneksi via Bluetooth. Status di layar berubah jadi hijau: `BLE CONNECTED`. |
| **3. Cara Bermain** | Kocok botol secara bolak-balik (maju-mundur / atas-bawah) sejauh ~30 cm. | Setiap kocokan valid akan langsung terhitung dan terkirim ke game. |
| **4. Hemat Baterai** | Tekan tombol **BOOT** 1x (klik & lepas). | Layar LCD mati untuk hemat daya, namun sensor kocokan tetap **100% aktif**. Tekan lagi untuk menyalakan layar. |
| **5. Ganti Player** | Masuk `[ SETTINGS MENU ]` -> Halaman 1 -> Ketuk tombol ID. | Beralih antara `BOTTLE-01` (Player 1) dan `BOTTLE-02` (Player 2). Tekan `HOME` untuk simpan. |
| **6. Kalibrasi Kepekaan**| Masuk Settings -> Halaman 4 (*Shake Calibration*). | Atur slider **FORCE** (kekuatan hentakan) dan **SPEED** (jeda getaran antar kocokan). |
| **7. Matikan Total** | Tahan tombol **PWR** selama 2–3 detik, atau pilih `[ POWER OFF ]` di Settings Halaman 6. | Mode mati total (*0 mA*). Baterai awet dan tidak berkurang saat disimpan. |
| **8. Pengecasan** | Colok kabel USB Type-C ke charger 5V standar. | Indikator persentase baterai dapat dipantau di pojok kanan atas layar. |

---

## 🔘 Tombol Fisik & Fungsinya

1. **Tombol BOOT (GPIO 0)**:
   - **Tekan 1x**: Mematikan layar LCD (*Screen Off*) untuk hemat baterai saat gameplay, atau menyalakan layar kembali (*Screen On*).
2. **Tombol PWR / PEKey (Tombol Samping)**:
   - **Tekan Singkat**: Diabaikan (mencegah botol mati tidak sengaja saat digenggam/dikocok).
   - **Tahan 2–3 Detik**: Mematikan daya alat secara total (*Shutdown 0 mA*).
3. **Port USB Type-C**:
   - Untuk mengisi daya baterai lithium dan update/upload firmware.

---

## 📱 Ringkasan 7 Halaman Pengaturan (Settings Menu)

Akses melalui tombol **`[ SETTINGS MENU ]`** di layar utama:
- **Navigasi Bawah**: `< PREV` (mundur), `HOME` (simpan & kembali ke utama), `NEXT >` (maju).
- **Halaman 1 (Device Identity)**: Tukar ID `BOTTLE-01` <-> `BOTTLE-02` dan tombol `[ SEND TEST SHAKE ]` untuk uji sinyal.
- **Halaman 2 (Counter ID)**: Lihat dan reset nomor urut paket kocokan lewat numpad sentuh.
- **Halaman 3 (Brightness)**: Slider geser untuk mengatur kecerahan layar (PWM 5–255).
- **Halaman 4 (Calibration & Oscilloscope)**: Grafik getaran real-time, slider **FORCE** (0.10g–2.00g), slider **SPEED** (5ms–100ms), dan indikator *VALID SHAKE*.
- **Halaman 5 (Sleep & Power Set)**: Pengatur durasi mati otomatis (*Sleep Timeout*) dan profil daya (*Fast Standby* vs *Ultra Sleep*).
- **Halaman 6 (Power Control)**: Tombol `[ SLEEP NOW ]` (mode tidur) dan `[ POWER OFF ]` (mati total 0 mA).
- **Halaman 7 (Touch Canvas)**: Kanvas gambar untuk mengetes responsivitas layar sentuh.

---

## ❓ Solusi Masalah Singkat (Troubleshooting)

- **Alat tidak menyala**: Colok kabel USB-C atau tahan tombol PWR samping selama 1–2 detik.
- **Kocokan tidak terdeteksi**: Buka Settings Halaman 4, turunkan nilai slider **FORCE** (misal ke `0.25g - 0.35g`).
- **Kocokan terhitung dobel**: Buka Settings Halaman 4, naikkan nilai slider **SPEED** (misal ke `40ms - 60ms`).
- **Game tidak mendeteksi botol**: Pastikan Bluetooth PC aktif dan ID botol di Settings Halaman 1 sesuai (`BOTTLE-01` / `BOTTLE-02`). Tekan `[ SEND TEST SHAKE ]` untuk uji coba.
- **Layar mendadak mati**: Tekan tombol **BOOT** 1x untuk membangunkan layar.

> [!CAUTION]
> **Peringatan**: Botol ini berisi rangkaian elektronik dan baterai lithium. **Dilarang mengisi botol dengan air atau cairan apapun.**
