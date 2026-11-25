# Smart Warehouse: Lift PID, IoT, dan Laravel

Tugas Besar 1 - Sistem Internet of Things

## Identitas Kelompok

| Keterangan | Isi |
| --- | --- |
| Nama kelompok | Kelompok 7 |
| Mata kuliah | Sistem Internet of Things |

### Anggota

| No | Nama | NIM | Bidang kerja |
| --- | --- | --- | --- |
| 1 | Dimas Rafif Zaidan | H1H024043 | IoT penuh: mekanik dan perakitan hardware, firmware ESP32, PID, state machine, MQTT |
| 2 | Huriyatun Nur Anajmi | H1H024035 | Backend: Laravel, database, integrasi MQTT, validasi, FIFO, autentikasi |
| 3 | Lula Khaisha Delavia | H1H024064 | Frontend: antarmuka web, dashboard, form put-away/picking, dokumentasi |

## Deskripsi Singkat

Prototipe miniatur gudang pintar skala meja: lift satu sumbu dengan kendali posisi PID, rak dua lantai, dan website Laravel yang mencatat stok dan lokasi barang. Operator membuat perintah put-away atau picking lewat website; Laravel mengirim nomor lantai ke ESP32 melalui **MQTT (HiveMQ Cloud, TLS)**. ESP32 menggerakkan lift dengan PID dan umpan balik encoder, lalu mengirim telemetri JSON yang ditampilkan real-time di dashboard (Laravel Reverb).

## Struktur Repositori

| Folder | Isi |
| --- | --- |
| `Progress-4_SmartWarehouse/` | Firmware ESP32 (Arduino): PID, kalibrasi dua titik, sensor rak HC-SR04, OLED, MQTT |
| `Progres-5_Laravel-Disesuaikan/` | Web Laravel versi terbaru yang sudah disamakan dengan firmware |
| `Progres-2_*`, `Progres-3_*` | Arsip versi web sebelumnya (tidak dipakai) |

## Komponen Perangkat Keras
ESP32 DevKit WROOM, driver L298N, motor JGA25 dengan encoder, 2x HC-SR04, OLED SSD1306, tombol UP/DOWN/MODE, tombol E-STOP.

## Komunikasi (MQTT)

| Topik | Arah | Isi |
| --- | --- | --- |
| `kelompok7/smartwarehouse/lift/command` | Laravel → ESP32 | `1`, `2`, `STOP` |
| `kelompok7/smartwarehouse/lift/admin/command` | Laravel → ESP32 | `ACK_ERROR` |
| `kelompok7/smartwarehouse/lift/telemetry` | ESP32 → Laravel | JSON status lift dan rak |
| `kelompok7/smartwarehouse/lift/admin/result` | ESP32 → Laravel | hasil perintah admin |

## Catatan Operasional
- Setelah ESP32 menyala, lakukan kalibrasi dua titik di perangkat (MODE x3) sebelum web dapat menggerakkan lift.
- Kredensial WiFi dan MQTT tidak boleh di-commit; isi lokal saja.
- Detail firmware: `Progress-4_SmartWarehouse/SmartWarehouse/README.md`. Detail web: `README.md` dan `DEPLOY.md` di folder Laravel.

## Status
Firmware dan web sudah terintegrasi lewat MQTT. Tahap berikutnya: uji end-to-end dengan perangkat nyata.
