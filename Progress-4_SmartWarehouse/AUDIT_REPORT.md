# SmartWarehouse — Audit & Refactoring Report

## Ringkasan

Firmware utama telah disesuaikan dengan hardware reference terbaru `TestAll.ino`. GPIO33 sekarang hanya menjadi tombol MODE OLED. Semua penggunaan GPIO33 sebagai bottom limit switch dan semua jalur automatic homing berbasis bottom limit switch dihapus.

Perubahan paling penting adalah penguncian gerak ketika posisi belum tervalidasi. Karena encoder incremental tidak memberikan posisi absolut setelah restart, firmware selalu boot ke `POSITION_UNREFERENCED` dan tidak menjalankan motor sampai operator menetapkan referensi secara eksplisit.

## Temuan dari source awal

| Severity | File / area | Temuan | Tindakan |
|---|---|---|---|
| CRITICAL | `Config.h`, `.ino`, `README.md` | GPIO33 masih didefinisikan sebagai bottom limit switch, sedangkan `TestAll.ino` terbaru menggunakan GPIO33 sebagai MODE. Firmware utama masih melakukan homing otomatis. | Dihapus. GPIO33 sekarang `PIN_BTN_MODE`; tidak ada fungsi `PIN_LIMIT_BOTTOM`, `limitBottomHit()`, atau `STATE_HOMING`. |
| CRITICAL | Boot & position state | Firmware lama memulai automatic homing dan kemudian menganggap posisi 0 valid. Dengan bottom limit switch dilepas, asumsi ini tidak lagi benar. | Boot ke `STATE_POSITION_UNREFERENCED`; pergerakan otomatis/manual diblokir sampai referensi manual diset. |
| CRITICAL | E-STOP | E-STOP software sebelumnya menurunkan pin arah motor, tetapi tidak dapat menggantikan pemutusan tenaga motor secara hardware. Konfigurasi README NC juga tidak cocok dengan asumsi NO aktif LOW di `TestAll.ino`. | Asumsi input mengikuti `TestAll.ino` (NO aktif LOW) dan mismatch dijelaskan. Persyaratan hardware E-STOP ditegaskan. Polaritas wajib diverifikasi di perangkat fisik. |
| HIGH | FreeRTOS shared state | `currentState`, encoder, dan data sensor sebelumnya dibaca dari task monitor dan loop kontrol tanpa snapshot/sinkronisasi yang konsisten. `volatile` bukan pengganti mutual exclusion. | Encoder memakai critical section; UI dan data sensor dipertukarkan melalui FreeRTOS queue. State machine dan motor dikendalikan dari `loop()` saja. |
| HIGH | Manual control | Jalur manual turun sebelumnya mengandalkan bottom limit switch untuk re-zero. Jalur ini tidak valid setelah switch dilepas. | Jog UP/DOWN hanya aktif ketika posisi valid, dibatasi software travel range, 3 detik per hold, dan encoder-progress timeout. |
| HIGH | MQTT callback | Callback menggunakan `String` dengan alokasi dinamis dan belum membatasi panjang payload. | Callback menggunakan buffer tetap 32 byte, validasi topic, trim, dan uppercase. |
| HIGH | Motion feedback | Tidak ada pemeriksaan eksplisit encoder tidak bertambah ketika motor diperintah bergerak. | Ditambahkan encoder-stall timeout untuk gerakan otomatis dan manual. Ini tetap bukan pengganti inspeksi sensor/driver fisik. |
| MEDIUM | PID | Integral sebelumnya dibatasi, tetapi integrasi masih dapat berlanjut saat output tersaturasi. Batas PWM minimum juga dapat memengaruhi overshoot. | Ditambahkan conditional integration dan konstanta min/max PWM configurable. Tuning hardware tetap diperlukan. |
| MEDIUM | OLED | Layar sebelumnya hanya menampilkan status statis sederhana; tidak memiliki navigasi MODE seperti pada `TestAll.ino`. | Ditambahkan empat halaman, debounce 30 ms, timeout 5 detik, idle animation, dan display priority. |
| MEDIUM | Wi-Fi startup | Koneksi Wi-Fi dilakukan secara blocking pada setup. | Wi-Fi dimulai secara asynchronous melalui `WiFi.begin()`; reconnect MQTT hanya dicoba ketika motor tidak aktif. |
| MEDIUM | Sensor display | Pembacaan sensor yang gagal dapat ditampilkan sebagai nilai lama atau tidak memiliki penanda freshness yang jelas. | Nilai invalid/stale ditampilkan sebagai `N/A`; snapshot sensor memuat valid flag dan counter. |

## Implementasi yang dibuat

### UI / OLED

- GPIO33 (`PIN_BTN_MODE`) mengganti halaman OLED.
- MODE aktif LOW, `INPUT_PULLUP`, debounce 30 ms, edge event satu kali.
- Halaman NORMAL, INPUT, ENCODER + MOTOR, dan SENSOR mengikuti nama/struktur dari `TestAll.ino`.
- Default idle ketika `STATE_IDLE` dan `positionValid=true` adalah animasi warehouse.
- Auto-return ke animasi setelah 5 detik tanpa interaksi.
- E-STOP, ERROR, posisi belum tervalidasi, dan pergerakan mempunyai prioritas di atas screensaver.
- Animasi berbasis `millis()`; tidak ada `delay()` untuk animasi.
- OLED/I2C hanya dimiliki `monitorTask`.

### Position reference

- `homed` internal diganti menjadi `positionValid`.
- Posisi tidak dipulihkan otomatis dari encoder ticks setelah boot.
- `ZERO` lalu `CONFIRM ZERO` di Serial Monitor menetapkan FLOOR 1 setelah operator secara fisik menempatkan carriage tepat di titik referensi.
- `ZERO` tidak menggerakkan motor.
- `HOME`, `RESET`, dan `ZERO` via MQTT ditolak.
- Setelah E-STOP atau error yang menginvalidasi posisi, proses referensi harus diulang.

### Control & concurrency

- Encoder increment/decrement disinkronisasi menggunakan `portMUX_TYPE`.
- Sensor dan UI menggunakan queue snapshot.
- Callback MQTT menggunakan buffer tetap.
- Task monitoring tidak memiliki akses tulis ke state machine motor.
- Motor melewati dead-time ketika berubah arah.
- Manual jog dibatasi durasi dan memerlukan posisi valid.

## Risiko / hal yang belum bisa divalidasi dari ZIP

1. **E-STOP wiring**: `TestAll.ino` memakai NO aktif LOW; README firmware lama menyebut NC. Firmware hasil ini mengikuti TestAll.ino, tetapi wiring aktual harus diperiksa dengan multimeter sebelum driver diberi tenaga. E-STOP hardware harus memutus supply motor.
2. **Tidak ada limit switch fisik**: software travel boundary berbasis encoder tidak melindungi dari encoder terputus, slip, arah salah, atau carriage melewati batas mekanis. Tambahkan sensor referensi independen/limit hardware untuk sistem yang dipakai rutin.
3. **Referensi manual**: `position_valid=true` hanya hasil konfirmasi operator. Pastikan proses penempatan carriage di FLOOR 1 dapat dilakukan dengan aman. Jika tidak bisa, jangan gunakan metode ZERO; pasang sensor referensi.
4. **Encoder polarity**: `ENCODER_INVERT=true` mengikuti `TestAll.ino`; tetap validasi bahwa ticks benar-benar bertambah saat lift naik.
5. **PID / motion constants**: `FLOOR_2_TICKS=750`, Kp/Ki/Kd, min PWM, timeout, dan jog PWM belum dituning pada hardware.
6. **L298N & motor**: arus start/stall, thermal margin, brake/coast behavior, dan beban mekanis belum diuji.
7. **MQTT security**: konfigurasi bawaan masih memakai broker publik placeholder. Ganti dengan broker privat berautentikasi dan ACL sebelum penggunaan fisik.
8. **OLED/sensor failure**: hilangnya OLED tidak menghentikan kontrol, tetapi task/queue allocation failure perlu diperhatikan melalui Serial log.

## Validasi yang dilakukan pada source hasil

- Pemeriksaan tekstual: tidak ada lagi referensi `PIN_LIMIT_BOTTOM`, `limitBottomHit`, `STATE_HOMING`, atau `HOMING_TIMEOUT` di firmware utama.
- Syntax-only check menggunakan stub API untuk cabang Arduino-ESP32 Core 2.x: **lulus**.
- Syntax-only check menggunakan stub API untuk cabang Arduino-ESP32 Core 3.x: **lulus**.

**Batas validasi:** environment ini tidak menyediakan `arduino-cli`, PlatformIO, ESP32 board package, atau library Adafruit/PubSubClient target. Karena itu belum dilakukan compile menggunakan toolchain Arduino yang sesungguhnya dan belum dilakukan upload atau pengujian hardware. Stub syntax-check bukan pengganti compile final.

## Rekomendasi sebelum penggunaan lift

1. Verifikasi polaritas E-STOP dan jalur pemutusan motor secara hardware.
2. Pasang kembali/ tambahkan sensor referensi bawah independen dan idealnya limit atas serta bawah.
3. Validasi arah encoder dan motor pada mekanisme tanpa beban dengan kecepatan rendah.
4. Kalibrasi FLOOR 2 ticks dan tuning PID secara bertahap.
5. Uji sensor stall, timeout, E-STOP, Wi-Fi/MQTT failure, serta restart pada berbagai posisi.
6. Jangan mengoperasikan lift dengan beban sebelum seluruh uji keselamatan hardware dinyatakan lulus.
