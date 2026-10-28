# EK Instrumentation — ESP32 Firmware Tool

**Firmware installer untuk alat monitoring PM dan Gas AQI berbasis ESP32.** Unduh, kompilasi, dan unggah firmware melalui Windows PowerShell menggunakan Arduino CLI, tanpa perlu membuka Arduino IDE.

Tool ini mendukung **dua perangkat fisik berbeda**. Pilihan firmware harus disesuaikan dengan alat yang terhubung.

| Firmware | Perangkat | Sensor |
| --- | --- | --- |
| **PM Monitoring** | Monitoring partikulat | 2 × PMS5003T dan SHT31 |
| **Gas AQI Monitoring** | Monitoring gas | MH-Z19C, MiCS-4514, dan SHT31 |

> **Kompatibilitas:** ESP32 klasik (ESP32-WROOM-32 / ESP32 DevKit V1 / *ESP32 Dev Module*). Firmware ini **bukan** untuk ESP32-S2, ESP32-S3, ESP32-C3, atau perangkat Muon.

## Instalasi cepat

**Persyaratan:** Windows 10/11 64-bit, Windows PowerShell 5.1 atau lebih baru, akses internet saat pemasangan pertama, serta kabel USB data dan driver USB-serial (CH340/CP210x) yang sesuai.

1. Hubungkan perangkat ESP32 ke laptop menggunakan kabel USB data.
2. Buka **Windows PowerShell**.
3. Jalankan perintah berikut:

   ```powershell
   irm https://raw.githubusercontent.com/efendiyhy/pmgas-esp32-firmware/main/install.ps1 | iex
   ```

4. Pilih **Monitoring PM** atau **Monitoring Gas** pada menu yang tampil.
5. Pilih **Compile Only** untuk memeriksa kode terlebih dahulu, atau lanjutkan ke upload setelah proses kompilasi selesai.
6. Jika memilih upload, pilih port COM, periksa informasi chip/MAC ESP32, lalu konfirmasi firmware dan perangkat yang akan di-flash.

Installer mengunduh paket terbaru dari repository ini dan menyiapkan Arduino CLI, ESP32 board package, serta library yang diperlukan jika belum tersedia. Tool tersimpan di:

```text
%LOCALAPPDATA%\EKInstrumentation\PMGasFirmwareTool
```

**Keamanan:** perintah `irm | iex` menjalankan kode PowerShell dari internet. Periksa isi [`install.ps1`](install.ps1) sebelum menjalankannya. Proses upload firmware tetap membutuhkan konfirmasi pengguna.

## Fitur

- Kompilasi otomatis menggunakan **Arduino CLI**, tanpa Arduino IDE.
- Pemilihan firmware PM atau Gas melalui terminal.
- Deteksi port COM yang tersedia dan pemilihan port secara manual.
- Verifikasi chip **ESP32 klasik** dan pembacaan MAC address sebelum upload.
- Registrasi MAC perangkat untuk membantu mencegah tertukarnya firmware pada upload berikutnya.
- **Compile Only** untuk menguji kode tanpa mengubah flash ESP32.
- Log proses kompilasi dan upload untuk penelusuran error.
- Pemasangan ulang tool tanpa menghapus registrasi perangkat dan log yang sudah ada.

> **Catatan:** Deteksi chip dan MAC tidak dapat mengetahui sensor yang terpasang. Pastikan secara fisik apakah board yang dipilih merupakan alat PM atau alat Gas sebelum menyetujui upload.

## Menjalankan tool setelah instalasi

Untuk membuka tool tanpa mengunduh ulang repository:

```powershell
powershell.exe -NoProfile -ExecutionPolicy Bypass -File "$env:LOCALAPPDATA\EKInstrumentation\PMGasFirmwareTool\flash.ps1"
```

Contoh pemakaian langsung:

**Kompilasi PM tanpa upload**

```powershell
powershell.exe -NoProfile -ExecutionPolicy Bypass -File "$env:LOCALAPPDATA\EKInstrumentation\PMGasFirmwareTool\flash.ps1" -Firmware pm -CompileOnly
```

**Kompilasi dan upload Gas melalui COM5**

```powershell
powershell.exe -NoProfile -ExecutionPolicy Bypass -File "$env:LOCALAPPDATA\EKInstrumentation\PMGasFirmwareTool\flash.ps1" -Firmware gas -Port COM5
```

Port `COM5` hanya contoh; sesuaikan dengan port USB perangkat Anda. Perintah kedua tetap melakukan verifikasi chip dan meminta konfirmasi sebelum upload. Untuk menjalankan langsung dari folder source, tersedia [`start_flash.cmd`](start_flash.cmd).

## Kompatibilitas dan konfigurasi firmware

- **Board/FQBN:** `esp32:esp32:esp32` — ESP32 Dev Module.
- **ESP32 core:** tool disiapkan untuk keluarga Arduino-ESP32 3.x (versi target **3.3.12** jika perlu instalasi baru).
- **Library sensor:** `Adafruit SHT31 Library` dan `Adafruit BusIO`.
- **Antarmuka perangkat:** UART, I²C, ADC, dan SPI; **tidak menggunakan `EspUsbHost`**.

### Hotspot dan dashboard ESP32

| Perangkat | SSID hotspot | Dashboard lokal |
| --- | --- | --- |
| PM Monitoring | `PM-MONITOR-SETUP` | `http://192.168.4.1` |
| Gas AQI Monitoring | `AQI-SETUP` | `http://192.168.4.1` |

Password hotspot bawaan dari firmware referensi adalah **`12345678`**. Ubah password sebelum penggunaan di jaringan yang tidak tepercaya.

Firmware mendukung monitoring tanpa koneksi Wi-Fi router, pemindaian jaringan Wi-Fi, konfigurasi IP server laptop, pencatatan microSD, serta sinkronisasi data melalui LAN/Supabase sesuai konfigurasi perangkat masing-masing.

**Wiring microSD yang diasumsikan:** CS = GPIO5, SCK = GPIO18, MISO = GPIO19, MOSI = GPIO23. Verifikasi wiring sensor, level tegangan, dan pin aktual sebelum upload. Jangan mengandalkan pemilihan board otomatis untuk menentukan hardware PM versus Gas.

### Supabase

Gunakan hanya **publishable/anon key** yang sesuai dengan kebijakan Row Level Security (RLS) project Anda. **Jangan menaruh `service_role` atau secret key** di firmware maupun repository publik. Jika endpoint atau key Supabase berubah, perbarui sketch perangkat yang relevan.

## Keamanan flashing dan identitas perangkat

Sebelum upload, tool membaca MAC address dari chip ESP32 klasik melalui esptool, lalu memeriksa registrasi perangkat di:

```text
%LOCALAPPDATA%\EKInstrumentation\PMGasFirmwareTool\config\devices.csv
```

Setelah flashing berhasil, tool menyimpan hubungan **MAC ↔ PM/Gas** untuk membantu mencegah upload firmware yang berbeda ke board yang sama. Board baru tetap memerlukan pemeriksaan fisik dan konfirmasi pengguna. Jika board memang dialihkan ke fungsi lain, tinjau registrasi secara manual.

Upload sketch bukan operasi *erase flash* penuh, tetapi pembaruan firmware tetap dapat memengaruhi konfigurasi atau data tertentu. Backup konfigurasi penting dan data microSD sebelum upgrade, serta jangan cabut kabel USB selama flashing.

## Struktur proyek

```text
pmgas-esp32-firmware/
├── install.ps1                # Instalasi Windows dari GitHub
├── flash.ps1                  # Compile, pemeriksaan chip, dan upload
├── start_flash.cmd            # Menjalankan tool dari folder source
├── README.md
├── config/
│   └── devices.example.csv    # Contoh registrasi perangkat
├── firmware/
│   ├── PM_ESP32_CLASSIC/
│   │   ├── PM_ESP32_CLASSIC.ino
│   │   └── EK_Dashboard.h
│   └── GAS_ESP32_CLASSIC/
│       ├── GAS_ESP32_CLASSIC.ino
│       └── EK_Dashboard.h
└── .github/workflows/
    └── compile.yml            # Workflow pemeriksaan kompilasi
```

Folder lokal seperti `tools/`, `build/`, `logs/`, dan `config/devices.csv` tidak dimaksudkan untuk disimpan dalam repository publik.

## Troubleshooting

| Masalah | Pemeriksaan / tindakan |
| --- | --- |
| Tidak ada port COM | Gunakan kabel USB data, periksa driver CH340/CP210x dan Windows Device Manager. |
| Verifikasi chip/MAC gagal | Periksa COM, tutup Serial Monitor, sambungkan ulang USB, atau masuk ke mode BOOT jika diperlukan. Upload akan dibatalkan. |
| Tipe chip tidak cocok | Tool ini hanya untuk ESP32 klasik; ESP32-S2/S3/C3 tidak didukung. |
| Library/board package tidak tersedia | Jalankan kembali tool dengan internet aktif, tanpa opsi `-SkipSetup`. |
| ESP32 tidak terhubung Wi-Fi | Periksa hotspot, hasil scan SSID, kredensial tersimpan, dan jangkauan jaringan. |
| Data tidak sampai ke server laptop | Pastikan IP laptop dan port `8787` benar serta kedua perangkat berada di LAN yang sama. |
| Data Supabase tidak masuk | Periksa koneksi internet, endpoint, publishable key, dan kebijakan RLS. |
| Error pada eksekusi PowerShell | Periksa bahwa skrip `install.ps1` didapat dari repository yang benar; lihat log tool. |

Log tersedia di:

```text
%LOCALAPPDATA%\EKInstrumentation\PMGasFirmwareTool\logs
```

## Uninstall

Hapus tool dari laptop melalui PowerShell:

```powershell
Remove-Item -LiteralPath "$env:LOCALAPPDATA\EKInstrumentation\PMGasFirmwareTool" -Recurse -Force
```

Perintah tersebut menghapus file tool, hasil build, log, dan registrasi MAC **di laptop**, tetapi **tidak menghapus firmware pada ESP32**. Arduino CLI atau board package yang dipasang secara global di profil pengguna tidak otomatis dihapus.

## Status pengujian dan referensi

Struktur proyek dan kode telah melalui pemeriksaan statis. **Kompilasi aktual dengan ESP32 core, pembacaan MAC, upload USB, dan pengujian end-to-end pada perangkat Windows/ESP32 fisik masih perlu dilakukan.** Disarankan menjalankan **Compile Only** sebelum upload pertama.

Referensi: [Arduino CLI](https://docs.arduino.cc/arduino-cli/) · [Arduino-ESP32](https://espressif.github.io/arduino-esp32/) · [esptool](https://docs.espressif.com/projects/esptool/en/latest/)
