# Smart Warehouse Lift

Firmware ESP32 untuk lift gudang dua lantai dengan kontrol posisi PID, kalibrasi dua titik, monitoring kapasitas rak (2x HC-SR04), layar OLED, dan kendali jarak jauh lewat MQTT (TLS).

Prinsip utama: **kontrol motor tidak pernah menunggu jaringan.** Semua akses WiFi/MQTT berjalan di task terpisah (core 0); `loop()` hanya menaruh telemetri ke buffer dan mengambil perintah dari antrean.

Versi firmware: `SW-R21-20261124` (`FIRMWARE_BUILD_ID` di `SmartWarehouse.ino`).

## Isi

- [Perangkat keras](#perangkat-keras)
- [Struktur proyek](#struktur-proyek)
- [Memulai](#memulai)
- [Konfigurasi](#konfigurasi)
- [Pengoperasian](#pengoperasian)
- [Layar OLED](#layar-oled)
- [Sensor rak](#sensor-rak)
- [MQTT](#mqtt)
- [Admin dari Laravel](#admin-dari-laravel)
- [Keamanan](#keamanan)
- [Pengujian](#pengujian)
- [Catatan hardware](#catatan-hardware)

## Perangkat keras

ESP32 DevKit WROOM, driver motor L298N, motor JGA25 dengan encoder kuadratur, 2x HC-SR04, OLED SSD1306 128x64 (I2C), tombol UP / DOWN / MODE, dan tombol E-STOP.

| Fungsi | GPIO | Keterangan |
|---|---|---|
| PWM motor | 25 | L298N ENB |
| Arah motor | 26 / 27 | L298N IN3 / IN4 |
| Encoder A / B | 34 / 35 | input-only, pull-up eksternal bila perlu |
| E-STOP | 32 | aktif LOW |
| Tombol MODE | 33 | aktif LOW |
| Tombol UP / DOWN | 13 / 14 | aktif LOW |
| HC-SR04 rak lantai 1 (TRIG / ECHO) | 17 / 16 | |
| HC-SR04 rak lantai 2 (TRIG / ECHO) | 18 / 19 | |
| OLED SDA / SCL | 21 / 22 | alamat 0x3C atau 0x3D (dipindai otomatis) |

Semua pin dan polaritas ada di `Config.h`.

**E-STOP:** ISR firmware hanya menandai kondisi E-STOP. Pemutusan daya motor **wajib** dilakukan oleh hardware.

## Struktur proyek

```
SmartWarehouse/
├── SmartWarehouse.ino   state machine lift, kalibrasi, perintah, setup/loop
├── Config.h             pin, fitur, parameter motor/PID, WiFi/MQTT, ambang sensor
├── State.h              enum state sistem dan pemetaan tick -> lantai
├── PIDControl.h         kelas LiftPID (anti-windup, tanpa derivative kick)
├── Button.h             debounce tombol
├── Sensors.h            HC-SR04, filter median, status kapasitas rak
├── Display.h            task OLED dan semua halaman tampilan
├── MQTTManager.h        task jaringan WiFi + MQTT, antrean perintah, admin
├── RemoteLog.h          serial monitor jarak jauh lewat MQTT
├── CommandUtil.h        normalisasi teks perintah
└── README.md
```

Nama folder harus sama dengan `SmartWarehouse.ino` agar bisa dibuka di Arduino IDE.

## Memulai

1. Pasang library: **Adafruit GFX**, **Adafruit SSD1306**, **PubSubClient**.
2. Pilih board ESP32 (Arduino core 2.x atau 3.x didukung).
3. Isi `Config.h` (lihat [Konfigurasi](#konfigurasi)).
4. Upload, lalu lakukan [kalibrasi dua titik](#kalibrasi-dua-titik). Posisi selalu tidak terreferensi setelah boot, jadi kalibrasi/referensi ulang wajib dilakukan setiap restart.

Bila WiFi tidak diisi, MQTT dan admin nonaktif tetapi lift tetap berfungsi penuh.

## Konfigurasi

### Flag fitur

| Flag | Default | Fungsi |
|---|---|---|
| `FEATURE_NETWORK` | 1 | WiFi + MQTT (task sendiri) |
| `FEATURE_RACK` | 1 | 2x HC-SR04 kapasitas rak (task sendiri) |
| `FEATURE_LOOP_WATCHDOG` | 1 | ESP32 reset bila `loop()` macet lebih dari timeout watchdog (default 5 detik) |

Untuk mengisolasi masalah lift, set `FEATURE_NETWORK` dan `FEATURE_RACK` ke 0. Perilaku lift tidak berubah.

### Jaringan

| Konstanta | Keterangan |
|---|---|
| `WIFI_SSID`, `WIFI_PASSWORD` | Kredensial WiFi |
| `MQTT_BROKER`, `MQTT_PORT` | Broker; 8883 untuk MQTT-TLS (8884 adalah WebSocket, jangan dipakai di ESP32) |
| `MQTT_USE_TLS` | 1 = TLS |
| `MQTT_CA_CERT` | Root CA broker (PEM). Kosong = terenkripsi tetapi identitas server tidak diverifikasi |
| `MQTT_USER`, `MQTT_PASSWORD` | Login broker. Kosong = anonim dan admin nonaktif |
| `ADMIN_REQUIRE_SECURE_BROKER` | true = admin hanya aktif bila TLS dan `MQTT_USER` terisi |

### Parameter penyetelan yang sering diubah

| Konstanta | Fungsi |
|---|---|
| `PID_KP`, `PID_KI`, `PID_KD` | Gain PID posisi |
| `PWM_MOVE_MIN`, `PWM_MOVE_MAX`, `PWM_MOVE_MIN_UP` | Batas PWM gerak (naik punya minimum sendiri) |
| `TOLERANCE_TICKS` | Toleransi tiba di target |
| `BRAKE_RECOIL_TICKS` | Rem dimulai sebesar ini lebih "lewat" target agar pantulan membawa lift tepat ke lantai |
| `FLOOR_TRAVEL_MAX_TICKS` | Batas atas travel F1 -> F2. Default 100000 (praktis tanpa batas); setelah travel terukur, set sekitar 1,3x nilai F2 |
| `MOTOR_DIR_INVERT`, `ENC_INVERT` | Polaritas motor dan encoder. UP harus menaikkan tick; perbaiki tanda encoder dengan `ENC_INVERT`, bukan `MOTOR_DIR_INVERT` |
| `RACKn_NEAR_FULL_ENTER_CM` / `_EXIT_CM` | Ambang sensor rak |

Fault `NO_PROGRESS` saat naik: naikkan `PWM_MOVE_MIN_UP`. Naik kebablasan: turunkan.

## Pengoperasian

### Kontrol

- **UP / DOWN** (satu tap, tidak perlu ditahan): lift berjalan otomatis ke Floor 2 / Floor 1 dengan PID.
- **E-STOP**: motor mati dan posisi dianggap hilang; wajib referensi ulang.
- **MODE**: klik 1x = halaman OLED berikut, 2x = lompat dua halaman, 3x = wizard kalibrasi.
- UP/DOWN ditolak bila posisi belum valid, travel belum terkalibrasi, lift sibuk, wizard terbuka, E-STOP aktif, atau sedang FAULT.

### Kalibrasi dua titik

Wizard dibuka dengan MODE x3 saat lift diam (state IDLE atau POSITION_UNREFERENCED). Jog UP/DOWN hanya aktif di dalam wizard, hold-to-run, maksimal 1,5 detik per tekanan (lepas kedua tombol untuk jog berikutnya). Menekan MODE saat jog langsung menghentikan motor. Wizard batal otomatis setelah 60 detik tanpa aktivitas.

1. **Tahap 1:** jog ke Floor 1, lepas tombol, tahan MODE 1,5 detik. Floor 1 menjadi tick 0.
2. **Tahap 2:** buka wizard lagi, jog UP ke Floor 2, tahan MODE 1,5 detik. Travel F2 divalidasi lalu disimpan ke NVS dan diverifikasi baca-ulang.

Setelah travel tersimpan, wizard di boot berikutnya hanya melakukan **referensi ulang**: klik MODE untuk memilih Floor 1, Floor 2, atau UKUR ULANG F1+F2 (menghapus travel lama), lalu tahan MODE. Referensi ulang ditolak bila encoder sudah terpercaya tetapi berbeda lebih dari `REREF_MISMATCH_TICKS` dari lantai yang dipilih.

### Fault

Gerakan dibatalkan dan lift masuk state ERROR bila terjadi: `WRONG_DIRECTION`, `MOTOR_STALL`, `NO_PROGRESS`, `MOTION_TIMEOUT`, `TOP_SOFT_LIMIT`, `BOTTOM_SOFT_LIMIT`, atau `CALIBRATION_ENCODER_NO_MOTION`. Pemulihan: kirim `ACK_ERROR` (Serial atau tombol admin Laravel), lalu referensi ulang dengan MODE x3. `ACK_ERROR` hanya membersihkan status fault, tidak memvalidasi posisi.

### Perintah Serial (115200 baud)

`1` / `0` / `GROUND` (Floor 1), `2` (Floor 2), `STOP`, `STATUS`, `ACK_ERROR`, `CANCEL_CAL`, `CLEAR_TRAVEL_CAL`. `HOME`, `RESET`, dan `ZERO` sengaja dinonaktifkan karena tidak ada sensor referensi fisik independen.

## Layar OLED

Dirancang untuk OLED dua warna: 16 baris atas kuning (status/judul), 48 baris bawah biru (isi). Pada OLED satu warna tampilan tetap normal. Klik MODE untuk ganti halaman.

| Halaman | Isi |
|---|---|
| 1 DASHBOARD | Poros lift animasi, lantai/arah, posisi, status, bar progres, ikon WiFi / MQTT / rak hampir penuh |
| 2 DETAIL | Posisi, tujuan, selisih, kecepatan, arah + PWM, travel F2 |
| 3 TRACE | Grafik posisi 14 detik terakhir dengan garis F1/F2 (untuk melihat overshoot dan settling) |
| 4 RAK | Bar kapasitas dan jarak tiap rak; `\|` menandai batas hampir penuh |
| 5 JARINGAN | SSID, sinyal, IP, status MQTT, status admin (nonaktif / siap / LOG AKTIF) |
| 6 SISTEM | Uptime, memori bebas, fault terakhir, versi build, status tombol |

Layar otomatis: **Kalibrasi**, **FAULT** (nama, penyebab, langkah), dan **E-STOP** (selalu menimpa). Detail teknis (NOTICK, PWM driver, GPIO, kode mentah) hanya ada di log Serial.

## Sensor rak

Dua HC-SR04 menghadap ke arah akses rak LIFO dan memberi peringatan bila barang mendekati zona deteksi. Sensor **tidak** menentukan lantai, tidak dipakai untuk homing, dan bukan satu-satunya pengaman overflow.

| Status | Arti |
|---|---|
| `SPACE_OK` | Jarak valid dan di atas ambang EXIT secara stabil |
| `NEAR_FULL` | Jarak valid pada/di bawah ambang ENTER selama beberapa sampel |
| `NO_TARGET` | Tidak ada echo. Bukan bukti rak kosong atau sensor sehat |
| `STALE` | Task sensor tidak memperbarui sampel tepat waktu |
| `UNKNOWN` | Belum ada kondisi stabil |

Filter: median 3 sampel, `RACK_CAPACITY_STABLE_SAMPLES` sampel berurutan, dan hysteresis ENTER/EXIT. Kedua sensor ditembak bergantian tiap `SENSOR_INTERVAL_MS`, sehingga status bukan alarm real-time.

### Menentukan ambang

Nilai awal 30/35 cm hanyalah nilai commissioning. Ukur per rak (geometri tidak boleh diasumsikan sama):

1. Amankan lift; ukur awal tanpa gerak motor.
2. Pastikan sensor menghadap zona barang, bukan rangka, carriage, lantai, atau dinding.
3. Catat jarak dengan rak kosong, lalu tambah barang satu per satu sesuai pola LIFO dan catat jarak saat barang mulai terdeteksi.
4. Pilih ENTER = jarak saat peringatan harus aktif; EXIT sedikit lebih besar agar tidak berkedip.
5. Uji di tiap lantai dan saat carriage dapat memotong beam sensor; pastikan tidak ada false positive.
6. Jalankan beberapa siklus put-away/keluarkan barang; status harus kembali `SPACE_OK`.

## MQTT

Topik (ubah di `Config.h`):

| Arah | Topik |
|---|---|
| Perintah ke ESP32 | `kelompok7/smartwarehouse/lift/command` |
| Telemetri dari ESP32 | `kelompok7/smartwarehouse/lift/telemetry` |
| Perintah admin ke ESP32 | `kelompok7/smartwarehouse/lift/admin/command` |
| Hasil perintah admin | `kelompok7/smartwarehouse/lift/admin/result` |
| Serial monitor jarak jauh | `kelompok7/smartwarehouse/lift/log` |

### Perintah gerak (topik `command`)

Teks biasa, tidak peka huruf besar: `1`, `0`/`GROUND`, `2`, `STOP`. Perintah dieksekusi di `loop()` lewat jalur yang sama dengan tombol sehingga semua pengaman (E-STOP, kalibrasi, FAULT, lift sibuk) tetap berlaku.

Pesan yang tiba dalam `MQTT_IGNORE_AFTER_SUBSCRIBE_MS` (1,5 detik) setelah subscribe diabaikan karena kemungkinan pesan *retained* lama. Perintah yang sah dikirim sesudah jendela itu.

### Telemetri (JSON)

Dikirim saat state berubah, tiap 1 detik saat bergerak, tiap 5 detik saat diam. Bila jaringan lambat hanya nilai terbaru yang dikirim.

`state, floor, ticks, target, dist1, dist2, sensor1_valid, sensor2_valid, rack1_status, rack2_status, rack1_near_full, rack2_near_full, estop, homed, position_valid, travel_config_valid, ready_to_move, position_source, pwm, direction, speed, floor2_ticks, fault, info`

`state = "OFFLINE"` dikirim broker otomatis (Last Will) bila ESP32 putus mendadak.

### Perintah admin (topik `admin/command`)

`ACK_ERROR`, `STATUS`, `CANCEL_CAL`, `LOG_ON`, `LOG_OFF`, `PING`. Selain itu ditolak dengan balasan `{"cmd":"...","ok":false,"msg":"..."}` di `admin/result`. Perintah gerak lift, `ZERO`, `CLEAR_TRAVEL_CAL`, `RESET`, dan `STOP` tidak bisa lewat jalur ini.

`LOG_ON` mengalirkan keluaran Serial per baris ke topik `log` (maks ~25 baris/detik; baris berlebih saat gerakan cepat bisa terlewat). `LOG_OFF` (default) tidak menampung apa pun.

## Admin dari Laravel

Pembagian tugas: **Laravel** menentukan siapa yang boleh menjadi admin (login/role). **MQTT** hanya mengangkut perintah lewat TLS + login broker. ESP32 tidak mengurus akun; ia menolak admin bila TLS/login tidak aktif dan hanya menerima perintah yang aman bagi lift.

### Broker

Pakai broker dengan login + TLS (HiveMQ Cloud, Mosquitto, EMQX). Broker publik anonim tidak cocok untuk admin: TLS mencegah penyadapan, bukan pengiriman pesan oleh pihak lain. Contoh ACL Mosquitto:

```
user esp32
topic read  kelompok7/smartwarehouse/lift/command
topic read  kelompok7/smartwarehouse/lift/admin/command
topic write kelompok7/smartwarehouse/lift/telemetry
topic write kelompok7/smartwarehouse/lift/admin/result
topic write kelompok7/smartwarehouse/lift/log

user laravel
topic readwrite kelompok7/smartwarehouse/lift/#
```

### Laravel (contoh, belum dijalankan di proyek ini)

`composer require php-mqtt/laravel-client`, lalu isi `config/mqtt-client.php` (host, port 8883, `use_tls`, username/password, `tls_ca_file`). Tombol admin di controller (lindungi dengan middleware/role):

```php
use PhpMqtt\Client\Facades\MQTT;

public function ackError()
{
    $this->authorize('lift-admin');   // gate/role milikmu
    MQTT::publish('kelompok7/smartwarehouse/lift/admin/command', 'ACK_ERROR');
    return back();
}
```

Perintah lain dikirim dengan cara yang sama. Untuk menampilkan hasil dan log, jalankan artisan command permanen (supervisor) yang subscribe ke `log` dan `admin/result`, lalu simpan ke DB atau broadcast ke browser (Reverb/Echo). Kirim `LOG_ON` saat halaman log dibuka dan `LOG_OFF` saat ditutup.

## Keamanan

- Jangan commit kredensial. `WIFI_PASSWORD`, `MQTT_USER`, dan `MQTT_PASSWORD` di `Config.h` harus diganti sebelum dipakai dan sebaiknya tidak ikut ke repositori.
- Isi `MQTT_CA_CERT` (mis. ISRG Root X1 untuk HiveMQ Cloud/Let's Encrypt). Tanpa itu koneksi rentan man-in-the-middle.
- Batasi siapa yang boleh publish ke topik `command` dan `admin/command` lewat ACL broker dan role di Laravel. Jangan izinkan publish dengan flag retain.
- Watchdog `loop()` aktif secara default. Saat reset, pin motor sempat floating sebelum `setup()` menahannya; pastikan driver L298N aman terhadap kondisi itu (mis. resistor pull-down pada IN1/IN2).
- Isi Serial dapat dibaca siapa pun yang punya akses subscribe ke topik `log`.

## Pengujian

Setiap tahap harus lulus sebelum lanjut. Bila tahap 2 ke atas gagal, matikan flag terkait untuk mengisolasi.

1. **Lift murni** (`FEATURE_NETWORK=0`, `FEATURE_RACK=0`): kalibrasi, UP/DOWN, E-STOP, STOP, fault.
2. **Rak** (`FEATURE_RACK=1`): UP/DOWN 10x; waktu tempuh dan titik berhenti harus sama dengan tahap 1. Halaman RAK menampilkan bar dan jarak.
3. **Serial**: kirim `STATUS`, `STOP`, dan `ACK_ERROR` (setelah fault).
4. **Jaringan** (`FEATURE_NETWORK=1`, isi SSID): periksa halaman JARINGAN. Matikan broker/internet saat lift diam lalu tap UP/DOWN; respons harus langsung. Putuskan WiFi saat lift bergerak; lift harus tetap berhenti tepat di target.
5. **MQTT**: kirim `2`, `1`, `STOP` saat bergerak, dan `2` saat E-STOP (harus ditolak). Cek telemetri di dashboard.
6. **Admin**: kirim `PING`, `LOG_ON` (log mengalir di topik `log`), picu fault lalu `ACK_ERROR`. Kirim `2` atau `ZERO` ke topik admin (harus ditolak). Kosongkan `MQTT_USER` (admin harus nonaktif).
7. **Daya**: jalankan lift dengan WiFi aktif 20 menit tanpa reset (brownout).

## Catatan hardware

Reset berulang dengan log `[WDT]` atau `Task watchdog` berarti `loop()` tertahan; set `FEATURE_LOOP_WATCHDOG` ke 0 hanya untuk diagnosis, lalu cari penyebab tertahannya.

WiFi menimbulkan lonjakan arus saat transmisi. Bila ESP32 reset (brownout) hanya saat motor dan WiFi aktif, pasang kapasitor besar dekat VIN ESP32 dan jangan berbagi jalur daya motor dengan catu ESP32.
