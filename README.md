# EK Instrumentation — ESP32 Firmware Installer

**One-shot firmware compiler & uploader** untuk dua perangkat monitoring terpisah: **PM** dan **Gas AQI**. Sistem berjalan dari Windows PowerShell, mengambil versi firmware terbaru dari GitHub **pada setiap eksekusi**, dan membersihkan seluruh folder kerja sementara setelah selesai.

| Firmware | Perangkat | Sensor |
| --- | --- | --- |
| PM Monitoring | Alat monitoring partikulat | 2× PMS5003T + SHT31 |
| Gas AQI Monitoring | Alat monitoring gas | MH-Z19C + MiCS-4514 + SHT31 |

**Target hardware:** ESP32 klasik (ESP32-WROOM-32 / ESP32 DevKit V1). Tidak mendukung ESP32-S2/S3/C3 atau perangkat Muon.

## Quick Start — Windows

**Persyaratan:** Windows 10/11 64-bit, Windows PowerShell 5.1+, koneksi internet, cukup ruang kosong untuk toolchain ESP32 (disarankan beberapa GB), kabel USB data, serta driver CH340/CP210x sesuai board. Driver USB tidak diinstal oleh tool ini.

1. Hubungkan alat PM **atau** Gas ke USB laptop.
2. Buka **Windows PowerShell**.
3. Jalankan:

   ```powershell
   irm https://raw.githubusercontent.com/efendiyhy/pmgas-esp32-firmware/main/install.ps1 | iex
   ```

4. Pilih **Monitoring PM** atau **Monitoring Gas** sesuai perangkat fisik.
5. Tunggu persiapan ESP32 core dan library, serta kompilasi otomatis.
6. Pilih **Compile Only** (tidak upload) atau lanjut ke upload.
7. Jika upload, pilih COM, periksa identitas **ESP32 klasik + MAC**, lalu ketik konfirmasi nama firmware yang dipilih.
8. Setelah selesai/ditolak/gagal, installer berusaha menghapus semua file hasil unduhan dan build dari sesi itu.

**Setiap proses upload atau kompilasi baru harus diawali dengan perintah yang sama.** Tidak ada aplikasi firmware permanen, shortcut, atau salinan firmware yang sengaja disimpan oleh installer.

> Menjalankan `irm | iex` berarti mengeksekusi skrip dari internet. Tinjau isi [`install.ps1`](install.ps1) terlebih dahulu dan pastikan URL berasal dari repository yang dipercaya.

## Cara Kerja & Pembersihan

Sistem memakai direktori sementara unik di `%TEMP%` (nama diawali `EK-PMGAS-ONESHOT-`). Di dalam sesi tersebut terdapat:

- source `.ino` dan `EK_Dashboard.h` terbaru dari repository GitHub;
- Arduino CLI portabel, ESP32 core, serta Adafruit SHT31/BusIO dalam profil Arduino **terisolasi**;
- hasil kompilasi, log sementara, dan cache build;
- data verifikasi chip/MAC yang hanya ditampilkan selama sesi, tidak didaftarkan permanen.

Saat sesi berakhir, folder tersebut dibersihkan otomatis menggunakan blok `finally`. Cleanup juga dicoba saat terjadi error atau pengguna membatalkan upload. **Jika Windows masih mengunci file**, terminal akan menampilkan lokasi folder yang perlu dihapus manual.

Tidak ada uninstall karena aplikasi tidak diinstal permanen. Jika terdapat folder aplikasi firmware dari installer EK versi lama (`%LOCALAPPDATA%\EKInstrumentation\PMGasFirmwareTool`), installer baru juga berusaha menghapusnya setelah sesi, termasuk registrasi MAC/log lama. **Tidak** menghapus direktori laptop-server (`...\LaptopServer`). Tidak perlu menghapus `Documents/Arduino`.

**Batasan:** pembersihan hanya mencakup berkas yang dibuat di dalam folder sesi. Kami tidak menghapus instalasi Arduino IDE/CLI lama, driver, cache/riwayat sistem Windows, riwayat PowerShell, maupun konfigurasi dan data dalam ESP32 atau microSD. Penghentian paksa proses atau mati listrik dapat menyisakan folder sementara.

**Konsekuensi:** karena ESP32 core dan library ikut dibersihkan, proses berikutnya perlu mengunduh toolchain lagi, yang relatif besar dan memerlukan internet.

## Keamanan Saat Upload

- Verifikasi chip melalui `esptool` dengan parameter `--chip esp32` sebelum upload.
- Menampilkan port COM dan MAC address sebelum konfirmasi akhir.
- Meminta pengguna mengetik `PM` atau `GAS` secara eksplisit untuk memulai flashing.
- **Jenis alat tidak dapat dideteksi dari chip/MAC saja.** Pengguna wajib memeriksa perangkat fisik, kabel sensor, dan tujuan firmware.
- Karena tidak menyimpan registrasi MAC antar-sesi, sistem **tidak dapat mengingat** bahwa sebuah MAC pernah digunakan sebagai PM atau Gas. Selalu verifikasi sebelum flashing.
- Arduino CLI menggunakan FQBN `esp32:esp32:esp32`, target ESP32 core **3.3.12**.

## Monitoring Perangkat

| Perangkat | Wi-Fi AP | Dashboard |
| --- | --- | --- |
| PM | `PM-MONITOR-SETUP` | `http://192.168.4.1` |
| Gas | `AQI-SETUP` | `http://192.168.4.1` |

Password default hotspot pada firmware referensi adalah `12345678`. Ubah untuk penggunaan di lingkungan yang tidak tepercaya. Firmware mempertahankan fitur monitoring offline, konfigurasi Wi-Fi, microSD, Supabase, dan pengiriman LAN ke laptop-server.

**Catatan kabel microSD:** konfigurasi kode mengasumsikan CS GPIO5, SCK GPIO18, MISO GPIO19, MOSI GPIO23. Cocokkan wiring fisik sebelum flash.

## Masalah Umum

| Gejala | Pemeriksaan |
| --- | --- |
| Port COM tidak muncul | Gunakan kabel data; periksa driver CH340/CP210x dan Device Manager. |
| Chip/MAC gagal diperiksa | Tutup Serial Monitor, pilih COM yang benar, coba BOOT sesuai board. |
| Board bukan ESP32 klasik | Upload dibatalkan; ESP32-S3/S2/C3 tidak didukung. |
| Unduhan core/library gagal | Pastikan internet dan ruang penyimpanan cukup. Jalankan kembali perintah instalasi. |
| Upload gagal | Periksa sambungan USB, daya, dan kemungkinan konflik port. Salin error dari terminal **sebelum menutupnya**. |
| Folder sementara gagal dibersihkan | Tutup proses yang mengunci file, lalu hapus folder sesi yang dicetak installer. |

## Struktur Repository

```text
pmgas-esp32-firmware/
├── install.ps1
├── flash.ps1
├── README.md
├── firmware/
│   ├── PM_ESP32_CLASSIC/
│   │   ├── PM_ESP32_CLASSIC.ino
│   │   └── EK_Dashboard.h
│   └── GAS_ESP32_CLASSIC/
│       ├── GAS_ESP32_CLASSIC.ino
│       └── EK_Dashboard.h
└── .github/workflows/compile.yml
```

Aplikasi **PM/Gas Laptop Server** berada di repository terpisah: [pmgas-merge](https://github.com/efendiyhy/pmgas-merge).

## Pengujian dan Batasan

Struktur proyek dan skrip telah diperiksa secara statis. Pengujian langsung alur instalasi/cleanup di Windows serta flashing dua perangkat fisik **belum dilakukan**. Workflow GitHub Actions akan menguji kompilasi ketika kode di-push.

Gunakan hanya publishable/anon key Supabase yang dibatasi RLS. Jangan menyimpan password jaringan, `service_role` atau kredensial privat pada repository publik.

Referensi: [Arduino CLI](https://docs.arduino.cc/arduino-cli/) · [Arduino ESP32](https://espressif.github.io/arduino-esp32/) · [esptool](https://docs.espressif.com/projects/esptool/en/latest/)
