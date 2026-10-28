# EK Instrumentation — PM & Gas ESP32 Firmware Tool

Windows PowerShell one-click compiler and flasher untuk **dua alat fisik berbeda**, keduanya memakai **ESP32 klasik** (ESP32-WROOM-32 / ESP32 DevKit V1 / `ESP32 Dev Module`). Firmware tidak ditujukan untuk ESP32-S2, S3, C3, atau proyek Muon.

| Firmware | Perangkat | Sensor utama |
|---|---|---|
| PM | Monitoring Partikulat | 2 × PMS5003T + SHT31 |
| Gas | Monitoring Gas AQI | MH-Z19C + MiCS-4514 + SHT31 |

Firmware mempertahankan dashboard offline melalui hotspot ESP, scan Wi-Fi, konfigurasi IP laptop, microSD, dan sinkronisasi Supabase/LAN dari kode proyek sebelumnya. **Jangan menggabungkan firmware kedua perangkat.**

## 1. Instalasi satu perintah (Windows)

Buat repository **publik** dengan nama berikut, kemudian upload *seluruh isi* folder ini ke root repository:

**`https://github.com/efendiyhy/pmgas-esp32-firmware`**

Setelah file berhasil di-push, buka **Windows PowerShell** biasa (tidak harus Administrator), lalu jalankan:

```powershell
irm https://raw.githubusercontent.com/efendiyhy/pmgas-esp32-firmware/main/install.ps1 | iex
```

> `irm | iex` akan mengeksekusi skrip dari repository GitHub. Gunakan hanya pada repository yang Anda kendalikan dan periksa skripnya sebelum menjalankan. Jika repository memakai nama lain, ubah variabel `$Repository` pada awal `install.ps1` dan sesuaikan URL perintah ini.

Installer akan:

1. Mengunduh ZIP terbaru dari branch `main`.
2. Memasang tool ke `%LOCALAPPDATA%\EKInstrumentation\PMGasFirmwareTool` (menjaga `config/devices.csv` dan log yang ada).
3. Membuka menu PowerShell: **PM / Gas**.
4. Menggunakan Arduino CLI yang sudah ada, atau mengunduh Arduino CLI resmi Windows 64-bit.
5. Memasang ESP32 board core **3.3.12** bila belum ada core 3.x, serta library **Adafruit SHT31 Library** dan **Adafruit BusIO** bila belum ada.
6. Mengompilasi firmware ESP32 terpilih.
7. Menanyakan konfirmasi untuk lanjut ke proses flashing.
8. Menampilkan COM yang terdeteksi dan membiarkan pengguna memilih port.
9. Memeriksa **chip ESP32 klasik dan MAC address** melalui `esptool.exe` dari ESP32 board package. Jika verifikasi gagal atau chip tidak cocok, flashing dibatalkan.
10. Memeriksa registrasi MAC perangkat, meminta Anda mengetik **PM** atau **GAS** sebelum flashing, lalu meng-upload.

Pertama kali perlu internet untuk mengunduh tool, board package, dan library. **Proses flashing tidak otomatis tanpa konfirmasi** dan bisa gagal jika kabel/driver/port bermasalah.

## 2. Jalankan lagi tanpa mengunduh

Buka PowerShell:

```powershell
powershell.exe -NoProfile -ExecutionPolicy Bypass -File "$env:LOCALAPPDATA\EKInstrumentation\PMGasFirmwareTool\flash.ps1"
```

Atau buka `start_flash.cmd` dari clone repository untuk menjalankan pada terminal lokal.

Opsi langsung:

```powershell
# Hanya kompilasi alat PM, tidak menyentuh ESP32
powershell.exe -NoProfile -ExecutionPolicy Bypass -File "$env:LOCALAPPDATA\EKInstrumentation\PMGasFirmwareTool\flash.ps1" -Firmware pm -CompileOnly

# Kompilasi alat Gas, pilih COM5 setelah kompilasi dan konfirmasi
powershell.exe -NoProfile -ExecutionPolicy Bypass -File "$env:LOCALAPPDATA\EKInstrumentation\PMGasFirmwareTool\flash.ps1" -Firmware gas -Port COM5
```

> COM dipilih secara manual; konektor CH340/CP210x tidak dapat mengidentifikasi apakah firmware yang terpasang sebelumnya adalah PM atau Gas.

## 3. Keamanan pemilihan alat

- **Chip check**: `esptool --chip esp32 ... read_mac` menolak keluarga chip selain ESP32 biasa sebelum upload.
- **MAC check**: perangkat yang sudah pernah berhasil di-flash didaftarkan di `config/devices.csv`; jika perangkat PM berikutnya dipilih sebagai Gas, alat akan **diblokir**.
- **Perangkat baru**: pilih alat fisik, cocokkan port dan MAC, dan ketik literal `PM`/`GAS` sebelum upload. Deteksi chip/MAC tidak bisa mengetahui sensor yang dipasang secara fisik.
- **Tidak melakukan erase flash penuh**: upload sketch standar. Meskipun demikian, tetap backup data/config penting sebelum upgrade firmware dan jangan cabut kabel saat flashing.
- Jika sengaja mengganti fungsi board, edit mapping MAC di `config/devices.csv` setelah memastikan perangkat fisiknya. File tersebut **tidak dipush ke GitHub**.

## 4. Wiring dan konfigurasi awal

**ESP32 biasa**: FQBN `esp32:esp32:esp32` (ESP32 Dev Module).

Konfigurasi SPI microSD pada kode: **CS=GPIO5, SCK=GPIO18, MISO=GPIO19, MOSI=GPIO23**. Cek pin sensor, tegangan, pembagi tegangan, dan wiring sesuai masing-masing alat sebelum upload. Library USB Host **EspUsbHost tidak digunakan**.

| Alat | Hotspot awal | Alamat dashboard |
|---|---|---|
| PM | `PM-MONITOR-SETUP` | `http://192.168.4.1` |
| Gas | `AQI-SETUP` | `http://192.168.4.1` |

Password hotspot awal dari firmware lama: `12345678`. **Ganti sebelum pemakaian di lokasi umum**. Pengaturan Wi-Fi tersimpan pada ESP32. IP laptop dapat dimasukkan dalam dashboard perangkat, kemudian ESP dan laptop harus berada di jaringan Wi-Fi/LAN yang sama. Pencatatan sensor tetap berjalan tanpa Wi-Fi.

### Supabase dan keamanan

Firmware menyertakan URL project serta **publishable key** Supabase yang sudah ada pada sketch referensi. Pastikan Row Level Security dan izin INSERT sesuai kebutuhan. **Jangan menyimpan `service_role` atau secret API key dalam firmware / repository publik.** Jika Anda memakai konfigurasi Supabase lain, ubah endpoint dan publishable key pada *sketch yang tepat* sebelum push.

## 5. Struktur repository

```text
pmgas-esp32-firmware/
├── install.ps1                   # Diambil via irm | iex
├── flash.ps1                     # Compile, deteksi perangkat, upload
├── start_flash.cmd               # Untuk clone lokal
├── README.md
├── .gitignore
├── config/
│   └── devices.example.csv       # Contoh format registry MAC
├── firmware/
│   ├── PM_ESP32_CLASSIC/
│   │   ├── PM_ESP32_CLASSIC.ino
│   │   └── EK_Dashboard.h
│   └── GAS_ESP32_CLASSIC/
│       ├── GAS_ESP32_CLASSIC.ino
│       └── EK_Dashboard.h
└── .github/workflows/compile.yml
```

Folder `tools/`, `build/`, `logs/` dan `config/devices.csv` lokal diabaikan oleh Git. Simpan perangkat rahasia / informasi kredensial khusus di luar repository publik.

## 6. Menyiapkan repository dan push

Di terminal pada folder hasil ekstraksi ZIP:

```powershell
git init
git branch -M main
git add .
git commit -m "Initial PM and Gas ESP32 firmware installer"
git remote add origin https://github.com/efendiyhy/pmgas-esp32-firmware.git
git push -u origin main
```

**Jangan jalankan `git push --force`.** Jika repo telah berisi commit, clone atau sinkronkan history repo dahulu.

## 7. Troubleshooting

| Kendala | Solusi |
|---|---|
| `No COM port` | Pastikan kabel USB data, driver CH340/CP210x, Windows Device Manager dan izin port |
| Chip verification gagal | Tutup Arduino Serial Monitor, cek COM, driver, USB, atau tahan BOOT saat deteksi |
| Chip bukan ESP32 klasik | Board S2/S3/C3/P4 tidak didukung — pilih hardware yang benar |
| Library tidak ada | Jalankan ulang tanpa `-SkipSetup`; koneksi internet diperlukan saat memasang dependensi |
| Supabase tidak menyimpan data | Cek endpoint, RLS, publishable key, dan apakah ESP terhubung internet |
| Tidak menerima data di laptop | Cek IP laptop, port 8787, firewall dan jaringan sama |
| `No pyvenv.cfg` | Installer ini tidak mengandalkan Python sistem; pakai `esptool.exe` dari core Espressif |
| `#` / BOM gagal pada `irm | iex` | `install.ps1` pada repo ini ditulis dalam UTF-8 **tanpa BOM** |

Log kompilasi/pemasangan: `%LOCALAPPDATA%\EKInstrumentation\PMGasFirmwareTool\logs`.

## 8. Hapus tool firmware di laptop

Buka PowerShell dan jalankan:

```powershell
Remove-Item -LiteralPath "$env:LOCALAPPDATA\EKInstrumentation\PMGasFirmwareTool" -Recurse -Force
```

Ini menghapus tool, log, build, dan registrasi MAC **di laptop saja**. Firmware yang sudah terpasang pada ESP32 tidak ikut dihapus. Arduino CLI global / core yang dipasang di profil Arduino juga tidak otomatis dihapus.

## 9. Status pengujian

Repository ini disusun dari **sketch PM/Gas REV4 yang tersedia**. Struktur berkas, kode JavaScript dashboard, dan skrip telah diperiksa secara statis di lingkungan Linux. **Kompilasi sesungguhnya dengan ESP32 core 3.3.12, pembacaan chip/MAC, upload USB, serta end-to-end Windows belum dijalankan dalam lingkungan ini.** Jalankan `-CompileOnly` lebih dahulu ketika pertama menguji.

Sumber CLI resmi: [Arduino CLI](https://docs.arduino.cc/arduino-cli/) · [Espressif Arduino-ESP32](https://espressif.github.io/arduino-esp32/) · [esptool](https://docs.espressif.com/projects/esptool/en/latest/).
