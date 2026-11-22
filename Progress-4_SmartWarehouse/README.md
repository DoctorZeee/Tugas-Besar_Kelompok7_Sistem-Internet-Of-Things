# SmartWarehouse — ESP32 Lift Controller

Firmware lift dua lantai berbasis ESP32, motor DC gearbox JGA25, driver L298N, rotary encoder, PID position control, E-STOP, tombol manual UP/DOWN, tombol MODE untuk OLED, dua sensor HC-SR04, OLED SSD1306, Wi-Fi, MQTT, dan FreeRTOS.

> **Penting — perubahan hardware:** bottom limit switch sudah dilepas. GPIO33 sekarang dipakai sebagai tombol MODE OLED. Karena itu firmware ini tidak melakukan automatic homing dan tidak menganggap posisi lift valid setelah boot/reset.

## 1. Hardware dan pin mapping

| Komponen | GPIO | Catatan |
|---|---:|---|
| L298N ENB / PWM | 25 | Lepas jumper ENB dan kendalikan melalui PWM |
| L298N IN3 | 26 | Arah motor |
| L298N IN4 | 27 | Arah motor |
| Encoder A | 34 | Input-only; gunakan pull-up eksternal bila diperlukan |
| Encoder B | 35 | Input-only; gunakan pull-up eksternal bila diperlukan |
| E-STOP | 32 | Default kode: NO ke GND, aktif LOW; verifikasi wiring |
| MODE button | 33 | Navigasi OLED, INPUT_PULLUP, aktif LOW |
| Button UP | 14 | Jog naik ketika posisi valid |
| Button DOWN | 13 | Jog turun ketika posisi valid |
| HC-SR04 #1 TRIG | 17 | Sensor lantai 1 |
| HC-SR04 #1 ECHO | 16 | Wajib voltage divider 5 V ke 3,3 V |
| HC-SR04 #2 TRIG | 18 | Sensor lantai 2 |
| HC-SR04 #2 ECHO | 19 | Wajib voltage divider 5 V ke 3,3 V |
| OLED SDA | 21 | I2C |
| OLED SCL | 22 | I2C |

Target board: **ESP32 Dev Module / ESP32 WROOM**. GPIO16/17 perlu diverifikasi ulang bila board yang digunakan bukan WROOM standar atau memiliki PSRAM.

### Wiring penting

- Satukan GND ESP32, driver, sensor, dan supply sesuai desain kelistrikan.
- ECHO HC-SR04 adalah 5 V pada banyak modul. Jangan langsung menghubungkannya ke GPIO ESP32; gunakan pembagi tegangan/level shifter.
- GPIO34/35 tidak memiliki internal pull-up. Pastikan output encoder memiliki level 3,3 V yang benar dan pull-up eksternal bila dibutuhkan.
- Pasang pull-down yang sesuai pada input enable/arah driver agar motor tetap mati ketika ESP32 boot/reset.
- **E-STOP wajib memutus tenaga motor melalui rangkaian hardware yang sesuai.** ISR firmware hanya lapisan tambahan, bukan emergency stop yang berdiri sendiri.
- Verifikasi apakah E-STOP fisik benar-benar NO aktif LOW seperti konfigurasi `TestAll.ino`. Jika wiring NC, ubah level dan edge interrupt setelah diverifikasi.

## 2. Persyaratan posisi setelah bottom limit switch dilepas

Encoder yang dipakai bersifat incremental. Setelah ESP32 kehilangan daya/reset, firmware tidak dapat mengetahui posisi absolut lift hanya dari nilai encoder yang volatile.

Perilaku firmware:

1. Setiap boot, posisi dimulai sebagai `POSITION_UNREFERENCED`.
2. Motor otomatis dan jog UP/DOWN diblokir sampai posisi direferensikan secara eksplisit.
3. Setelah E-STOP atau error yang membuat posisi diragukan, posisi diinvalidasi kembali.
4. `HOME` dan `RESET` MQTT tidak lagi menjalankan homing.
5. Perintah MQTT untuk pindah lantai ditolak selama `position_valid=false`.

### Referensi posisi manual

Fitur `ZERO` **tidak menggerakkan motor**. Perintah ini hanya memberi tahu firmware bahwa operator sudah menempatkan carriage tepat pada referensi fisik FLOOR 1.

Prosedur commissioning:

1. Isolasi/cabut suplai tenaga motor dan amankan carriage/beban secara mekanis. Jangan mengandalkan gearbox atau software untuk menahan beban.
2. Dengan prosedur servis mekanis yang aman, tempatkan carriage tepat pada titik referensi FLOOR 1. Jika carriage tidak dapat diposisikan manual dengan aman, jangan memaksakan prosedur ini; pasang kembali sensor referensi posisi independen.
3. Pastikan E-STOP tidak aktif, motor berhenti, serta tombol UP/DOWN dilepas.
4. Buka Serial Monitor pada 115200 baud.
5. Kirim `ZERO` lalu kirim `CONFIRM ZERO` dalam 10 detik.
6. Firmware menetapkan encoder ke `FLOOR_1_TICKS` dan mengubah status ke `IDLE`.
7. Verifikasi arah encoder, hitungan FLOOR 2, batas mekanis, dan `FLOOR_2_TICKS` menggunakan prosedur uji terkendali sebelum mengaktifkan gerakan otomatis.

Perintah serial yang tersedia:

- `ZERO` — mempersenjatai prosedur set posisi selama 10 detik.
- `CONFIRM ZERO` — menetapkan posisi FLOOR 1 tanpa menggerakkan motor.
- `STATUS` — menampilkan state, ticks, dan validitas posisi.
- `STOP` — menghentikan motor.

**Peringatan:** `position_valid=true` adalah pernyataan operator setelah referensi manual, bukan konfirmasi sensor absolut. Software travel limit tidak menggantikan limit switch fisik. Untuk sistem lift yang digunakan rutin, rekomendasi kuat adalah memasang sensor referensi/limit independen (minimal referensi bawah; desain limit atas/bawah lebih baik) dan mengembangkan ulang homing fail-safe.

## 3. OLED dan tombol MODE

OLED SSD1306 128×64 mempunyai tampilan default berupa idle animation saat sistem `IDLE` dan posisi sudah valid.

Tekan tombol MODE GPIO33 untuk menjelajahi empat halaman:

1. **NORMAL** — posisi, target, status motor, PWM, jarak sensor.
2. **INPUT** — status tombol UP, DOWN, MODE, dan E-STOP.
3. **ENCODER + MOTOR** — ticks, estimasi ticks/second, target, arah, PWM.
4. **SENSOR** — jarak HC-SR04 dan jumlah pembacaan berhasil/gagal.

Setelah tidak ada interaksi selama 5 detik, tampilan kembali ke idle animation. Tombol MODE hanya mengubah UI dan tidak boleh memicu pergerakan lift.

Prioritas display:

1. E-STOP.
2. ERROR.
3. Posisi belum tervalidasi.
4. Pergerakan otomatis/manual.
5. Notifikasi target tercapai.
6. Halaman monitoring atau idle animation.

Animasi diperbarui berdasarkan `millis()` tanpa `delay()` dan tidak mempunyai jalur kontrol motor. OLED serta pembacaan HC-SR04 dimiliki satu `monitorTask` pada core 0. Task kontrol dan PID tetap berjalan pada `loop()`.

## 4. PID dan batas gerak

Parameter awal berada di `Config.h`:

- `PID_KP`, `PID_KI`, `PID_KD`
- `PID_MIN_PWM`, `PID_MAX_PWM`
- `FLOOR_1_TICKS`, `FLOOR_2_TICKS`
- `FLOOR_TOL_TICKS`, `TOLERANCE_TICKS`
- `MOVE_TIMEOUT_MS`
- `MAX_MANUAL_JOG_MS`
- `ENCODER_STALL_TIMEOUT_MS`

`FLOOR_2_TICKS=750` hanyalah nilai placeholder dari konfigurasi lama. Kalibrasikan pada mekanisme sebenarnya. Jangan menganggap parameter PID atau batas ticks siap untuk produksi tanpa pengujian.

Firmware mempunyai timeout gerakan, timeout jog manual, pemeriksaan kemajuan encoder, dan batas perjalanan berbasis encoder. Namun, semua itu tetap bergantung pada encoder dan tidak mencegah kecelakaan bila encoder salah arah, terputus, slip, atau mekanik melampaui batas. Gunakan limit hardware yang independen.

## 5. FreeRTOS dan pembagian tugas

- `loop()` menangani state machine, PID, motor, tombol, MQTT, dan telemetry.
- `monitorTask` menjadi satu-satunya pemilik OLED/I2C dan membaca HC-SR04 secara bergantian.
- Snapshot UI dan sensor dipertukarkan melalui FreeRTOS queue.
- Akses encoder disinkronisasi dengan critical section khusus ISR/task.
- Pembacaan HC-SR04 memakai timeout dan berada di task monitoring, bukan di loop PID.

## 6. MQTT

Default topic:

- Command: `topic/lift/command`
- Telemetry: `topic/lift/telemetry`

Perintah yang diterima:

- `1`, `0`, atau `GROUND` — menuju FLOOR 1.
- `2` — menuju FLOOR 2.
- `STOP` — menghentikan gerakan.

`HOME`, `RESET`, dan `ZERO` ditolak karena tidak ada bottom limit switch. Penetapan referensi saat ini hanya tersedia secara lokal melalui Serial Monitor setelah penempatan fisik yang aman.

Telemetry memuat antara lain `state`, `floor`, `ticks`, `target`, `dist1`, `dist2`, `sensor1_valid`, `sensor2_valid`, `estop`, `position_valid`, `homed`, `pwm`, `direction`, dan `info`.

Field `homed` dipertahankan sebagai alias kompatibilitas lama untuk `position_valid`; field ini **tidak** berarti firmware menjalankan automatic homing.

Jangan memakai broker publik tanpa autentikasi untuk kontrol perangkat fisik. Atur broker privat, autentikasi, ACL topic, TLS bila tersedia, dan validasi command di sisi server.

## 7. Library dan build

Library Arduino yang diperlukan:

- Adafruit GFX Library
- Adafruit SSD1306
- PubSubClient

Board: ESP32 Dev Module. Kode PWM mempunyai cabang API Arduino-ESP32 core 2.x dan 3.x. Kompilasi akhir tetap harus dilakukan menggunakan versi board package dan library yang dipasang pada environment target.

Sebelum upload:

1. Isi SSID/password Wi-Fi pada `Config.h`.
2. Atur broker dan topic MQTT.
3. Verifikasi wiring E-STOP, driver, encoder, dan sensor.
4. Kalibrasi `FLOOR_2_TICKS`.
5. Periksa apakah arah encoder positif benar-benar berarti lift naik.
6. Pastikan emergency stop hardware memutus suplai tenaga motor.

## 8. Checklist pengujian sebelum motor diberi tenaga

### Tahap A — tanpa suplai motor

- [ ] Periksa semua GND dan polaritas.
- [ ] Periksa ECHO HC-SR04 melalui voltage divider.
- [ ] Periksa pull-up encoder eksternal.
- [ ] Periksa tombol MODE berpindah halaman satu kali per tekan.
- [ ] Periksa auto-return 5 detik.
- [ ] Periksa idle animation ketika posisi valid dan state IDLE.
- [ ] Periksa E-STOP ditampilkan paling prioritas.
- [ ] Pastikan reboot menghasilkan `POSITION_UNREFERENCED`.
- [ ] Pastikan perintah MQTT lantai ditolak ketika posisi belum valid.
- [ ] Pastikan `ZERO` tidak menggerakkan motor.

### Tahap B — commissioning dengan mekanisme diamankan

- [ ] Verifikasi penetapan referensi FLOOR 1.
- [ ] Verifikasi arah encoder dengan pergerakan terkendali.
- [ ] Verifikasi batas bawah dan atas secara konservatif.
- [ ] Verifikasi E-STOP memutus tenaga motor secara hardware.
- [ ] Verifikasi timeout gerakan dan encoder stall.
- [ ] Uji jog manual dengan batas waktu 3 detik.
- [ ] Uji STOP MQTT.
- [ ] Uji koneksi Wi-Fi/MQTT terputus saat lift berhenti dan bergerak.

Jangan langsung menjalankan lift dengan beban. Lakukan pengujian tanpa beban, kecepatan rendah, pengawasan operator, dan prosedur keselamatan mekanis.

## 9. Batas validasi

Source code ini merupakan hasil refactoring berbasis dua arsip yang diberikan. Kode belum dapat dianggap tervalidasi secara hardware. Kompilasi final, arah motor/encoder, tuning PID, perilaku driver L298N, waktu travel, batas mekanis, dan fungsi emergency-stop harus diuji pada perangkat fisik sebelum digunakan.
