#ifndef CONFIG_H
#define CONFIG_H

#include <Arduino.h>

// =====================================================================
// SMART WAREHOUSE - HARDWARE CONFIGURATION
// ESP32 DevKit WROOM + L298N + JGA25 + encoder + 2x HC-SR04 + SSD1306
// Pin mapping dikunci mengikuti wiring hardware yang sudah ada.
// =====================================================================

// ---------- FITUR ----------
// Build terintegrasi: perilaku lift hasil perbaikan R20 + WiFi/MQTT + sensor rak + OLED baru.
// Untuk debugging lift murni (tanpa gangguan jaringan/sensor) set keduanya ke 0.
#define FEATURE_NETWORK 1   // WiFi + MQTT (berjalan di task sendiri, tidak memblokir kontrol motor)
#define FEATURE_RACK    1   // 2x HC-SR04 kapasitas rak (task sendiri)
#define FEATURE_LOOP_WATCHDOG 1   // reset ESP32 bila loop() macet (mencegah motor jalan tanpa kontrol)

// ---------- WIFI & MQTT ----------
const char* const WIFI_SSID     = "Nama_WiFi_Kamu";
const char* const WIFI_PASSWORD = "Password_WiFi_Kamu";
const char* const MQTT_BROKER   = "ISI_ALAMAT_CLUSTER.s1.eu.hivemq.cloud";   // HiveMQ Cloud
const int         MQTT_PORT     = 8883;   // 8883 = MQTT-TLS (ESP32/Laravel). 8884 = WebSocket untuk browser, JANGAN dipakai di ESP32

const char* const MQTT_TOPIC_COMMAND   = "kelompok7/smartwarehouse/lift/command";
const char* const MQTT_TOPIC_TELEMETRY = "kelompok7/smartwarehouse/lift/telemetry";

// ---------- KEAMANAN MQTT ----------
// Enkripsi: TLS (MQTTS, port 8883). Autentikasi: login broker. Siapa yang boleh jadi admin diatur di
// Laravel + ACL broker (hanya akun Laravel yang boleh publish ke topik admin/command).
#define MQTT_USE_TLS 1
// Sertifikat root CA broker (PEM) untuk memverifikasi server. Kosong = tetap terenkripsi tetapi
// identitas server tidak diverifikasi (rentan man-in-the-middle). Untuk HiveMQ Cloud / Let's Encrypt
// dipakai "ISRG Root X1" (berlaku sampai Juni 2035).
const char* const MQTT_CA_CERT = R"(-----BEGIN CERTIFICATE-----
MIIFazCCA1OgAwIBAgIRAIIQz7DSQONZRGPgu2OCiwAwDQYJKoZIhvcNAQELBQAw
TzELMAkGA1UEBhMCVVMxKTAnBgNVBAoTIEludGVybmV0IFNlY3VyaXR5IFJlc2Vh
cmNoIEdyb3VwMRUwEwYDVQQDEwxJU1JHIFJvb3QgWDEwHhcNMTUwNjA0MTEwNDM4
WhcNMzUwNjA0MTEwNDM4WjBPMQswCQYDVQQGEwJVUzEpMCcGA1UEChMgSW50ZXJu
ZXQgU2VjdXJpdHkgUmVzZWFyY2ggR3JvdXAxFTATBgNVBAMTDElTUkcgUm9vdCBY
MTCCAiIwDQYJKoZIhvcNAQEBBQADggIPADCCAgoCggIBAK3oJHP0FDfzm54rVygc
h77ct984kIxuPOZXoHj3dcKi/vVqbvYATyjb3miGbESTtrFj/RQSa78f0uoxmyF+
0TM8ukj13Xnfs7j/EvEhmkvBioZxaUpmZmyPfjxwv60pIgbz5MDmgK7iS4+3mX6U
A5/TR5d8mUgjU+g4rk8Kb4Mu0UlXjIB0ttov0DiNewNwIRt18jA8+o+u3dpjq+sW
T8KOEUt+zwvo/7V3LvSye0rgTBIlDHCNAymg4VMk7BPZ7hm/ELNKjD+Jo2FR3qyH
B5T0Y3HsLuJvW5iB4YlcNHlsdu87kGJ55tukmi8mxdAQ4Q7e2RCOFvu396j3x+UC
B5iPNgiV5+I3lg02dZ77DnKxHZu8A/lJBdiB3QW0KtZB6awBdpUKD9jf1b0SHzUv
KBds0pjBqAlkd25HN7rOrFleaJ1/ctaJxQZBKT5ZPt0m9STJEadao0xAH0ahmbWn
OlFuhjuefXKnEgV4We0+UXgVCwOPjdAvBbI+e0ocS3MFEvzG6uBQE3xDk3SzynTn
jh8BCNAw1FtxNrQHusEwMFxIt4I7mKZ9YIqioymCzLq9gwQbooMDQaHWBfEbwrbw
qHyGO0aoSCqI3Haadr8faqU9GY/rOPNk3sgrDQoo//fb4hVC1CLQJ13hef4Y53CI
rU7m2Ys6xt0nUW7/vGT1M0NPAgMBAAGjQjBAMA4GA1UdDwEB/wQEAwIBBjAPBgNV
HRMBAf8EBTADAQH/MB0GA1UdDgQWBBR5tFnme7bl5AFzgAiIyBpY9umbbjANBgkq
hkiG9w0BAQsFAAOCAgEAVR9YqbyyqFDQDLHYGmkgJykIrGF1XIpu+ILlaS/V9lZL
ubhzEFnTIZd+50xx+7LSYK05qAvqFyFWhfFQDlnrzuBZ6brJFe+GnY+EgPbk6ZGQ
3BebYhtF8GaV0nxvwuo77x/Py9auJ/GpsMiu/X1+mvoiBOv/2X/qkSsisRcOj/KK
NFtY2PwByVS5uCbMiogziUwthDyC3+6WVwW6LLv3xLfHTjuCvjHIInNzktHCgKQ5
ORAzI4JMPJ+GslWYHb4phowim57iaztXOoJwTdwJx4nLCgdNbOhdjsnvzqvHu7Ur
TkXWStAmzOVyyghqpZXjFaH3pO3JLF+l+/+sKAIuvtd7u+Nxe5AW0wdeRlN8NwdC
jNPElpzVmbUq4JUagEiuTDkHzsxHpFKVK7q4+63SM1N95R1NbdWhscdCb+ZAJzVc
oyi3B43njTOQ5yOf+1CceWxG1bQVs5ZufpsMljq4Ui0/1lvh+wjChP4kqKOJ2qxq
4RgqsahDYVvTH9w7jXbyLeiNdd8XM2w9U/t7y0Ff/9yi0GE44Za4rF2LN9d11TPA
mRGunUHBcnWEvgJBQl9nJEiU0Zsnvgc/ubhPgXRR4Xq37Z0j4r7g1SgEEzwxA57d
emyPxgcYxn/eR44/KJ4EBs+lVDR3veyJm+kXQ99b21/+jh5Xos1AnX5iItreGCc=
-----END CERTIFICATE-----)";
// Login broker (kosong = anonim).
const char* const MQTT_USER     = "ISI_USERNAME_MQTT";
const char* const MQTT_PASSWORD = "ISI_PASSWORD_MQTT";   // isi sendiri; jangan commit nilai asli

// ---------- ADMIN (dikendalikan dari Laravel lewat MQTT) ----------
// Perintah yang diterima di topik admin/command (teks biasa): ACK_ERROR, STATUS, CANCEL_CAL,
// LOG_ON, LOG_OFF, PING. Perintah gerak lift, ZERO, CLEAR_TRAVEL_CAL, RESET, STOP ditolak.
// Bila true, admin hanya aktif jika MQTT_USE_TLS=1 DAN MQTT_USER terisi (broker tanpa login = siapa pun
// bisa publish). Set false hanya untuk uji coba di broker publik.
const bool ADMIN_REQUIRE_SECURE_BROKER = true;
const char* const MQTT_TOPIC_ADMIN_CMD    = "kelompok7/smartwarehouse/lift/admin/command";
const char* const MQTT_TOPIC_ADMIN_RESULT = "kelompok7/smartwarehouse/lift/admin/result";
const char* const MQTT_TOPIC_LOG          = "kelompok7/smartwarehouse/lift/log";             // aliran serial monitor

// ---------- PIN (TIDAK DIUBAH) ----------
const int PIN_PWM = 25;   // L298N ENB
const int PIN_IN1 = 26;   // L298N IN3
const int PIN_IN2 = 27;   // L298N IN4

const int PIN_ENC_A = 34; // input-only; external pull-up bila diperlukan
const int PIN_ENC_B = 35; // input-only; external pull-up bila diperlukan

const int PIN_ESTOP    = 32;
const int PIN_BTN_MODE = 33; // MODE OLED + calibration wizard (bukan limit switch)
const int PIN_BTN_UP   = 13; // ditukar sesuai permintaan: GPIO13 sekarang tombol UP
const int PIN_BTN_DOWN = 14; // ditukar sesuai permintaan: GPIO14 sekarang tombol DOWN

const int PIN_TRIG1 = 17; // HC-SR04 kapasitas rak lantai 1
const int PIN_ECHO1 = 16;
const int PIN_TRIG2 = 18; // HC-SR04 kapasitas rak lantai 2
const int PIN_ECHO2 = 19;

const int PIN_OLED_SDA = 21;
const int PIN_OLED_SCL = 22;
const int OLED_WIDTH   = 128;
const int OLED_HEIGHT  = 64;
const uint8_t OLED_ADDR_DEFAULT = 0x3C;

// ---------- POLARITAS ----------
#define ESTOP_ACTIVE_LEVEL LOW
#define MODE_ACTIVE_LEVEL  LOW
#define MOTOR_DIR_INVERT   0
#define ENC_INVERT         0

// ---------- MOTOR ----------
const int PWM_FREQ = 2000;
const int PWM_RES  = 8;
const int PWM_CHANNEL = 0; // hanya Core 2.x
const int PWM_UP_PCT   = 100;
const int PWM_DOWN_PCT = 100;
const unsigned long RAMP_MS = 300;
const int PWM_KICK = 110;
// Dorongan awal (menggantikan soft-start): di log, awal gerak dengan PWM rendah (110-150)
// membuat rem terlepas tetapi motor belum cukup kuat menahan, sehingga lift/encoder bergeser
// 35-60 tick BERLAWANAN arah dulu. Selama KICK_MS pertama, PWM minimal PWM_START_BOOST.
const unsigned long KICK_MS = 250;
const int PWM_START_BOOST = 200;
#define STOP_BRAKE 1

// ---------- POSISI LANTAI ----------
// FLOOR 1 selalu menjadi koordinat logis 0. Travel FLOOR 2 diukur langsung
// oleh wizard dua titik dan disimpan di ESP32 NVS (Preferences).
const long FLOOR_1_TICKS = 0;
const long FLOOR_TOL_TICKS = 30;
const long FLOOR_TRAVEL_MIN_TICKS = 50;
// Re-referensi ditolak bila encoder sudah terpercaya (posisi valid saat wizard dibuka)
// tetapi bacaan encoder berbeda lebih dari nilai ini dari lantai yang dipilih.
// Artinya travel tersimpan tidak cocok dengan jarak fisik -> pakai UKUR ULANG F1+F2.
const long REREF_MISMATCH_TICKS = 100;
// Batas atas travel Floor 1 -> Floor 2. Dipakai untuk (1) menolak penyimpanan kalibrasi
// dan (2) menghentikan jog UP di tahap 2 kalibrasi. Nilai 100000 praktis tanpa batas:
// setelah travel terukur, set sekitar 1.3x nilai F2 yang tampil di OLED.
const long FLOOR_TRAVEL_MAX_TICKS = 100000;
const long FLOOR_SOFT_OVERRUN_TICKS = 30; // watchdog software di luar titik lantai terkalibrasi

// ---------- PID & GERAK ----------
const float PID_KP = 0.8f;   // 0.5 terlalu lemah untuk travel pendek (~290 tick): PWM jatuh ke minimum terlalu cepat
const float PID_KI = 0.02f;
const float PID_KD = 0.05f;

const int PWM_MOVE_MIN = 180;   // di bawah ~150 lift merayap (~30 t/s) pada beban ini
const int PWM_MOVE_MAX = 220;
// PWM minimum khusus arah NAIK. Naik melawan gravitasi, jadi PWM 130 (minimum turun)
// bisa terlalu lemah, terutama bila PID menghasilkan nilai kecil karena travel pendek.
// Naikkan bila fault NO_PROGRESS masih muncul saat naik; turunkan bila naik kebablasan.
const int PWM_MOVE_MIN_UP = 170;
// Jog khusus kalibrasi: hanya aktif ketika wizard MODE terbuka.
// Nilai 200 mengikuti nilai PWM awal pada TestAll yang telah diuji pengguna.
const int PWM_CALIBRATION_JOG = 200;

const long TOLERANCE_TICKS = 12;
// Gerakan otomatis dibatalkan bila encoder bergerak berlawanan arah dari target
// lebih dari nilai ini (encoder terbalik/rusak): mencegah lift naik/turun tanpa kendali.
const long WRONG_DIRECTION_TICKS = 40;
// Di awal gerak (rem dilepas, torsi belum terbentuk) encoder sering bergeser berlawanan arah
// 30-40 tick (terlihat di log: awal DOWN ticks naik dulu, awal UP ticks turun dulu).
// Selama WRONG_DIRECTION_GRACE_MS pertama, batas yang dipakai lebih longgar
// (WRONG_DIRECTION_HARD_TICKS). Sesudahnya kembali ke WRONG_DIRECTION_TICKS.
const unsigned long WRONG_DIRECTION_GRACE_MS = 700;
const long WRONG_DIRECTION_HARD_TICKS = 120;
// Pengereman prediktif: motor dihentikan lebih awal sebesar (kecepatan x BRAKE_LEAD_MS)
// agar momentum + gravitasi tidak membuat lift kebablasan melewati lantai.
// Terlalu kecil -> masih kebablasan; terlalu besar -> berhenti sebelum lantai.
const unsigned long BRAKE_LEAD_MS = 0;
const long BRAKE_LEAD_MAX_TICKS = 100;
// Pantulan setelah rem: di log, lift memantul ~20-25 tick berlawanan arah gerak setelah direm.
// Rem dimulai sebesar ini lebih "lewat" dari target supaya pantulan membawanya tepat ke lantai.
// Berhenti kurang dari lantai -> naikkan; melewati lantai -> turunkan. Batas aman: < FLOOR_SOFT_OVERRUN_TICKS.
const long BRAKE_RECOIL_TICKS = 25;
const unsigned long SPEED_SAMPLE_MS = 50;
const unsigned long SETTLE_REPORT_MS = 600;  // laporan posisi akhir setelah lift berhenti total
const unsigned long MOVE_TIMEOUT_MS = 12000;
const unsigned long STALL_FAIL_MS = 1200;
// Deteksi 'tidak ada kemajuan': lift mentok tetapi encoder masih bergetar +-1 tick,
// sehingga MOTOR_STALL (yang hanya melihat tick berubah) tidak pernah aktif.
// Gerakan dianggap macet bila kemajuan menuju target tidak bertambah minimal
// PROGRESS_MIN_TICKS dalam PROGRESS_FAIL_MS.
const long PROGRESS_MIN_TICKS = 10;
const unsigned long PROGRESS_FAIL_MS = 1200;
const unsigned long MOTOR_DEADTIME_MS = 80;
// Setiap tekan UP/DOWN saat kalibrasi dibatasi 1.5 detik.
// Lepas kedua tombol untuk membuka kesempatan jog berikutnya.
const unsigned long CALIBRATION_JOG_MAX_MS = 1500;
const unsigned long CALIBRATION_JOG_STALL_MS = 900;

// ---------- INPUT / POSITION CALIBRATION ----------
const unsigned long DEBOUNCE_MS = 30;
const unsigned long MODE_MULTI_CLICK_WINDOW_MS = 900;
const uint8_t MODE_CALIBRATION_CLICKS = 3;
const unsigned long CALIBRATION_CONFIRM_HOLD_MS = 1500;
const unsigned long CALIBRATION_TIMEOUT_MS = 60000; // wizard bisa memerlukan beberapa jog pulse

// ---------- RACK CAPACITY SENSING (2x HC-SR04) ----------
// Sensor menghadap ke arah lift/akses rak LIFO. Sensor hanya memonitor
// kedekatan barang dengan area deteksi (indikasi rak mendekati penuh).
// Sensor TIDAK digunakan untuk mengetahui posisi lift atau melakukan homing.
// Threshold awal 30/35 cm adalah nilai commissioning; ukur dan sesuaikan
// dengan geometri rak, arah sensor, dan karakteristik pantulan barang.
const float RACK1_NEAR_FULL_ENTER_CM = 30.0f; // masuk NEAR_FULL pada <= nilai ini
const float RACK1_NEAR_FULL_EXIT_CM  = 35.0f; // kembali SPACE_OK pada >= nilai ini
const float RACK2_NEAR_FULL_ENTER_CM = 30.0f;
const float RACK2_NEAR_FULL_EXIT_CM  = 35.0f;
const uint8_t RACK_CAPACITY_STABLE_SAMPLES = 3;

// ---------- SENSOR ----------
const unsigned long SENSOR_MAX_AGE_MS = 900;
const unsigned long SENSOR_INTERVAL_MS = 100; // HC-SR04 ditembak bergantian
const float SENSOR_MIN_VALID_CM = 2.0f;
const float SENSOR_MAX_VALID_CM = 400.0f;

// ---------- WIFI / MQTT ----------
const unsigned long WIFI_BOOT_WAIT_MS = 4000;
const unsigned long TELEMETRY_BUSY_MS = 1000;
const unsigned long TELEMETRY_IDLE_MS = 5000;
const unsigned long MQTT_RETRY_MS = 5000;
// Pesan retained di broker langsung dikirim saat subscribe. Perintah yang tiba dalam jendela ini
// diabaikan agar perintah lama (mis. "2") tidak dieksekusi ulang tiap ESP32 tersambung.
const unsigned long MQTT_IGNORE_AFTER_SUBSCRIBE_MS = 1500;

// ---------- OLED ----------
// Layout dirancang untuk OLED dua warna: 16 baris atas KUNING (header/status), 48 baris bawah BIRU (isi).
// Batas warna ini fisik dan tidak bisa digeser; Display.h menaruh semua isi di bawah y=16.
// Tampilan di Display.h dirender ulang tiap DISPLAY_FRAME_MS. Halaman tetap dipilih
// sampai operator menekan MODE lagi (klik 1x = halaman berikut, 2x = lompat dua halaman).
const unsigned long DISPLAY_FRAME_MS = 125;
// Grafik kapasitas rak: bar penuh bila jarak sensor ke barang = 0 cm, kosong bila >= nilai ini.
const float RACK_GAUGE_MAX_CM = 60.0f;

#endif
