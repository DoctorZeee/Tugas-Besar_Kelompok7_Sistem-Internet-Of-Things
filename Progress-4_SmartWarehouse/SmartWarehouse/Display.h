#ifndef DISPLAY_H
#define DISPLAY_H

// =====================================================================
// OLED SSD1306 128x64 - tampilan operator + diagnostik
//
// Prinsip desain:
//   * Satu halaman = satu pertanyaan. Dashboard menjawab "lift di mana dan sedang apa";
//     halaman lain untuk angka rinci, grafik gerakan, rak, jaringan, dan sistem.
//   * Pesan memakai kalimat singkat (bukan kode seperti POSITION_UNREFERENCED).
//   * Tidak ada angka internal yang sulit dibaca (NOTICK, OUTPUT AGE, dsb).
//     Detail itu tetap ada di log Serial.
//   * Fault dan E-STOP mengambil seluruh layar, lengkap dengan penjelasan.
//
// Semua data dibaca dari snapshot queue agar task display tidak mengakses
// variabel kontrol/motor yang sedang dimodifikasi loop utama.
// =====================================================================

#include <Wire.h>
#include <stdio.h>
#include <string.h>
#include <math.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>
#include <freertos/FreeRTOS.h>
#include <freertos/queue.h>
#include "Config.h"
#include "State.h"
#include "Sensors.h"

// Halaman yang bisa dipilih dengan klik MODE.
#define PG_DASH   0
#define PG_DETAIL 1
#define PG_TRACE  2
#define PG_RACK   3
#define PG_NET    4
#define PG_SYS    5
#define UI_PAGE_COUNT 6

// Scene internal.
#define SC_BOOT   0
#define SC_PAGE0  1     // SC_PAGE0 + nomor halaman
#define SC_CAL    20
#define SC_FAULT  21
#define SC_ESTOP  22

struct UiData {
  int state;
  long ticks;
  long target;
  int motorDir;
  int pwm;
  long lastGoal;
  bool lastGoalValid;
  long moveOrigin;
  long speedTps;
  bool pwmReady;
  bool positionValid;
  bool homed;
  bool travelCalibrated;
  long floor2Ticks;
  bool networkEnabled;
  bool rackEnabled;
  int calStage;
  long calDelta;
  unsigned long modeHeldMs;
  bool wifi;
  bool mqtt;
  int admin;            // 0 nonaktif, 1 siap, 2 serial monitor jarak jauh aktif
  int rssi;
  char ip[16];
  const char* info;
  const char* lastFault;
  const char* build;
  int page;
  unsigned long lastActivity;
  unsigned long calibrationFlashUntil;
  int bootTarget;
  const char* bootText;
  bool bootDone;
  float sensorDist[2];
  bool sensorValid[2];
  int rackStatus[2];
  bool calibrationActive;
  int calibrationFloor;
  bool upPressed;
  bool downPressed;
  bool modePressed;
  bool estopPressed;
};

UiData ui;
static QueueHandle_t uiQueue = nullptr;
static UiData uiView = {};

bool displayQueueInit() {
  uiQueue = xQueueCreate(1, sizeof(UiData));
  return uiQueue != nullptr;
}

void displayPublish() {
  if (uiQueue) xQueueOverwrite(uiQueue, &ui);
}

static Adafruit_SSD1306 oled(OLED_WIDTH, OLED_HEIGHT, &Wire, -1);
static bool oledOk = false;

#define W_ SSD1306_WHITE
#define B_ SSD1306_BLACK

// ---------------------------------------------------------------------
// Alat gambar dasar
// ---------------------------------------------------------------------
static int textW(const char* s, int size = 1) {
  const int n = (int)strlen(s);
  return (n > 0) ? (n * 6 - 1) * size : 0;
}

static void putText(Adafruit_GFX& g, int x, int y, const char* s, int size = 1, uint16_t color = W_) {
  g.setTextSize(size);
  g.setTextColor(color);
  g.setCursor(x, y);
  g.print(s);
}

// xRight = kolom piksel terakhir yang boleh terisi.
static void putRight(Adafruit_GFX& g, int xRight, int y, const char* s, int size = 1, uint16_t color = W_) {
  putText(g, xRight - textW(s, size) + 1, y, s, size, color);
}

static void putCenter(Adafruit_GFX& g, int y, const char* s, int size = 1, uint16_t color = W_) {
  putText(g, (OLED_WIDTH - textW(s, size)) / 2, y, s, size, color);
}

static void dotLineH(Adafruit_GFX& g, int x0, int x1, int y) {
  for (int x = x0; x <= x1; x += 3) g.drawPixel(x, y, W_);
}

static int clampi(long v, int lo, int hi) {
  if (v < lo) return lo;
  if (v > hi) return hi;
  return (int)v;
}

static float clampf(float v, float lo, float hi) {
  if (v < lo) return lo;
  if (v > hi) return hi;
  return v;
}

static void drawHeader(Adafruit_GFX& g, const char* title, int page) {
  // Zona kuning (y 0..15): judul halaman + nomor halaman.
  putText(g, 0, 4, title);
  char num[12];
  snprintf(num, sizeof(num), "%d/%d", page + 1, UI_PAGE_COUNT);
  putRight(g, 127, 4, num);
}

// Baris "label ........ nilai" dengan titik-titik penuntun.
static void kvRow(Adafruit_GFX& g, int y, const char* label, const char* value) {
  putText(g, 0, y, label);
  putRight(g, 127, y, value);
  const int x0 = textW(label) + 5;
  const int x1 = 127 - textW(value) - 4;
  for (int x = x0; x < x1; x += 3) g.drawPixel(x, y + 6, W_);
}

// ---------------------------------------------------------------------
// Pengetahuan tampilan: lantai, arah, label
// ---------------------------------------------------------------------
static int uiFloor() {
  if (!uiView.positionValid) return 0;
  if (labs(uiView.ticks - FLOOR_1_TICKS) <= FLOOR_TOL_TICKS) return 1;
  if (uiView.travelCalibrated && labs(uiView.ticks - uiView.floor2Ticks) <= FLOOR_TOL_TICKS) return 2;
  return 0;
}

// Arah gerak yang sedang dituju (bukan arah PWM sesaat, supaya panah tidak berkedip).
static int uiTravelDir() {
  if (uiView.state != STATE_MOVING) return 0;
  return (uiView.target >= uiView.ticks) ? 1 : -1;
}

static const char* stateLabel() {
  switch (uiView.state) {
    case STATE_BOOT: return "BOOT";
    case STATE_POSITION_UNREFERENCED: return "NO REF";
    case STATE_IDLE: return uiView.positionValid ? "SIAP" : "NO REF";
    case STATE_MOVING: return (uiTravelDir() >= 0) ? "NAIK" : "TURUN";
    case STATE_ARRIVED: return "SAMPAI";
    case STATE_MANUAL: return "MANUAL";
    case STATE_ESTOP: return "E-STOP";
    case STATE_ERROR: return "FAULT";
    default: return "?";
  }
}

static const char* rackLabel(int status) {
  switch (status) {
    case RACK_CAPACITY_SPACE_OK: return "LONGGAR";
    case RACK_CAPACITY_NEAR_FULL: return "HAMPIR PENUH";
    case RACK_CAPACITY_NO_TARGET: return "TANPA ECHO";
    case RACK_CAPACITY_STALE: return "SENSOR MATI";
    default: return "MENGUKUR";
  }
}

// ---------------------------------------------------------------------
// Pesan: kode info/fault -> kalimat. '|' = pindah baris.
// Baris dashboard maks 15 karakter; baris layar kalibrasi/fault maks 21.
// ---------------------------------------------------------------------
struct UiMsg { const char* code; const char* text; };

static const UiMsg UI_MSGS[] = {
  // umum / dashboard
  {"POSITION_UNREFERENCED",          "Posisi hilang|MODE x3: ref."},
  {"POSITION_CALIBRATED_READY",      "Kalibrasi OK|Siap bergerak"},
  {"FLOOR1_SET_NOW_MEASURE_FLOOR2",  "F1 tersimpan|Lanjut: MODE x3"},
  {"CALIBRATE_FLOOR1_THEN_FLOOR2",   "Ukur F1 lalu F2|MODE x3 mulai"},
  {"ALREADY_AT_REQUESTED_FLOOR",     "Sudah di lantai|tujuan"},
  {"BUSY_WAIT_FOR_ARRIVAL",          "Lift masih|bergerak"},
  {"BOTH_FLOOR_BUTTONS_PRESSED",     "UP & DOWN|bersamaan"},
  {"ERROR_ACK_REQUIRED",             "Ada fault|Kirim ACK_ERROR"},
  {"CALIBRATION_BUSY",               "Kalibrasi|sedang aktif"},
  {"CALIBRATION_NOT_ALLOWED",        "Kalibrasi belum|bisa dibuka"},
  {"CALIBRATION_CANCELLED",          "Kalibrasi|dibatalkan"},
  {"CALIBRATION_INCOMPLETE",         "Kalibrasi belum|lengkap"},
  {"PWM_NOT_READY",                  "PWM motor gagal|init"},
  {"PWM_INIT_FAILED",                "PWM motor gagal|init"},
  {"TARGET_NOT_CALIBRATED",          "Target tidak|terkalibrasi"},
  {"TARGET_OUT_OF_RANGE",            "Target di luar|rentang lantai"},
  {"STOPPED",                        "Dihentikan|(perintah STOP)"},
  {"UNKNOWN_CMD",                    "Perintah tidak|dikenal"},
  {"HOME_UNAVAILABLE_NO_REFERENCE",  "HOME nonaktif|pakai MODE x3"},
  {"USE_MODE_TRIPLE_CLICK",          "Pakai MODE x3|untuk referensi"},
  {"FAULT_ACK_POSITION_UNREFERENCED","Fault di-ACK|MODE x3: ref."},
  {"ESTOP_RELEASED_RECALIBRATE",     "E-STOP dilepas|MODE x3: ref."},
  {"ESTOP_POSITION_INVALID",         "E-STOP aktif|posisi hilang"},
  {"TRAVEL_CAL_CLEARED_RECALIBRATE", "Travel dihapus|MODE x3: ukur"},
  {"ACK_ERROR_NOT_ALLOWED",          "ACK_ERROR|ditolak"},
  {"CANCEL_CAL_NOT_ALLOWED",         "CANCEL_CAL|ditolak"},
  {"CLEAR_TRAVEL_NOT_ALLOWED",       "CLEAR_TRAVEL|ditolak"},
  {"SERIAL_COMMAND_TOO_LONG",        "Perintah|terlalu panjang"},
  // wizard kalibrasi
  {"CALIBRATION_JOG_LIMIT_RELEASE",       "Batas jog 1,5 dtk|lepas UP/DOWN"},
  {"RELEASE_BEFORE_DIRECTION_CHANGE",     "Lepas tombol dulu|sebelum ganti arah"},
  {"CALIBRATION_BOTH_BUTTONS_RELEASE",    "UP+DOWN bersamaan|lepas keduanya"},
  {"CALIBRATION_INTERLOCK_RELEASE_BUTTONS","Lepas UP/DOWN dulu|lalu tahan MODE"},
  {"UP_JOG_REQUIRED_FOR_FLOOR2",          "Jog UP ke Floor 2|sebelum menyimpan"},
  {"ENCODER_UP_DIRECTION_INVALID",        "Arah encoder salah|UP harus +tick"},
  {"TRAVEL_DELTA_TOO_SMALL",              "Travel terlalu kecil|jog lebih jauh"},
  {"TRAVEL_DELTA_TOO_LARGE",              "Travel terlalu besar|cek batas travel"},
  {"TRAVEL_MAX_REACHED",                  "Batas travel maks|tercapai, lepas"},
  {"TRAVEL_NVS_SAVE_FAILED",              "Gagal simpan NVS|ulangi tahan MODE"},
  {"TRAVEL_NVS_OPEN_FAILED",              "NVS tidak bisa|dibuka"},
  {"CALIBRATE_FLOOR1_FIRST",              "Set F1 dulu|(tahap 1)"},
  {"STAGE1_REQUIRES_FLOOR1",              "Tahap 1 hanya|untuk Floor 1"},
  {"STAGE2_REQUIRES_FLOOR2",              "Tahap 2 hanya|untuk Floor 2"},
  {"STAGE1_FIXED_FLOOR1_HOLD_MODE_TO_SET","Lantai terkunci F1|tahan MODE: simpan"},
  {"STAGE2_FIXED_FLOOR2_HOLD_MODE_TO_SAVE","Lantai terkunci F2|tahan MODE: simpan"},
  {"TRAVEL_MISMATCH_REMEASURE",           "Travel tak cocok|pilih UKUR ULANG"},
  {"SELECT_VALID_FLOOR",                  "Pilih F1 atau F2|dulu"},
  {"CALIBRATION_STAGE_INVALID",           "Tahap kalibrasi|tidak valid"},
};

static const char* uiMsgLookup(const char* code) {
  if (!code || !code[0]) return nullptr;
  for (size_t i = 0; i < sizeof(UI_MSGS) / sizeof(UI_MSGS[0]); ++i) {
    if (strcmp(UI_MSGS[i].code, code) == 0) return UI_MSGS[i].text;
  }
  return nullptr;
}

// Pecah teks jadi maks 2 baris selebar maxChars. '|' = baris baru paksa;
// selebihnya dipotong di spasi terakhir yang muat.
static void splitMsg(const char* text, int maxChars, char* l1, char* l2) {
  char tmp[64];
  strncpy(tmp, text, sizeof(tmp) - 1);
  tmp[sizeof(tmp) - 1] = '\0';
  l1[0] = l2[0] = '\0';

  char* bar = strchr(tmp, '|');
  const char* second = nullptr;
  if (bar) {
    *bar = '\0';
    second = bar + 1;
  } else if ((int)strlen(tmp) > maxChars) {
    int cut = maxChars;
    for (int i = maxChars; i > 0; --i) {
      if (tmp[i] == ' ') { cut = i; break; }
    }
    second = tmp + cut;                       // tidak ada spasi: potong di tengah kata
    if (tmp[cut] == ' ') { tmp[cut] = '\0'; second = tmp + cut + 1; }
  }
  strncpy(l1, tmp, maxChars);
  l1[maxChars] = '\0';
  if (second) {
    strncpy(l2, second, maxChars);
    l2[maxChars] = '\0';
  }
}

// Kode tak dikenal -> "KODE_ASLI" jadi "KODE ASLI" supaya tetap terbaca.
static void rawCodeMsg(const char* code, int maxChars, char* l1, char* l2) {
  char tmp[48];
  size_t n = 0;
  for (; code[n] && n < sizeof(tmp) - 1; ++n) tmp[n] = (code[n] == '_') ? ' ' : code[n];
  tmp[n] = '\0';
  splitMsg(tmp, maxChars, l1, l2);
}

struct UiFault { const char* code; const char* title; const char* text; };

static const UiFault UI_FAULTS[] = {
  {"MOTOR_STALL",                   "MOTOR STALL",         "Encoder diam. Cek|motor & kabel encoder"},
  {"NO_PROGRESS",                   "TIDAK ADA KEMAJUAN",  "Lift mentok / berat.|Cek beban & nilai F2"},
  {"MOTION_TIMEOUT",                "GERAK TERLALU LAMA",  "Lebih dari 12 detik.|Cek mekanik dan PWM"},
  {"TOP_SOFT_LIMIT",                "LEWAT BATAS ATAS",    "Lewat Floor 2.|Cek F2 dan pengereman"},
  {"BOTTOM_SOFT_LIMIT",             "LEWAT BATAS BAWAH",   "Lewat Floor 1.|Cek rem dan kalibrasi"},
  {"WRONG_DIRECTION",               "ARAH ENCODER SALAH",  "Tick berlawanan arah.|Cek fase encoder A/B"},
  {"CALIBRATION_ENCODER_NO_MOTION", "JOG TANPA TICK",      "Motor jalan, encoder|diam. Cek encoder"},
  {"PWM_INIT_FAILED",               "PWM GAGAL INIT",      "Output motor gagal|Restart ESP32"},
};

// ---------------------------------------------------------------------
// Ikon status (bar atas dashboard)
// ---------------------------------------------------------------------
static int rssiLevel(int rssi) {
  if (rssi > -55) return 4;
  if (rssi > -65) return 3;
  if (rssi > -75) return 2;
  if (rssi > -85) return 1;
  return 0;
}

static const char* rssiWord(int rssi) {
  if (rssi > -60) return "kuat";
  if (rssi > -70) return "baik";
  if (rssi > -80) return "lemah";
  return "buruk";
}

static void drawWifiIcon(Adafruit_GFX& g, int x, int level) {
  for (int i = 0; i < 4; ++i) {
    const int h = 2 * (i + 1);
    const int bx = x + i * 3;
    if (i < level) g.fillRect(bx, 13 - h, 2, h, W_);
    else g.drawPixel(bx, 12, W_);
  }
}

static void drawStatusIcons(Adafruit_GFX& g) {
  if (uiView.rackEnabled) {
    const bool full = (uiView.rackStatus[0] == RACK_CAPACITY_NEAR_FULL) ||
                      (uiView.rackStatus[1] == RACK_CAPACITY_NEAR_FULL);
    if (full) {
      g.fillRect(118, 3, 9, 9, W_);
      putText(g, 120, 4, "R", 1, B_);
    } else {
      g.drawRect(118, 3, 9, 9, W_);
      putText(g, 120, 4, "R", 1, W_);
    }
  }
  if (uiView.networkEnabled) {
    drawWifiIcon(g, 98, uiView.wifi ? rssiLevel(uiView.rssi) : 0);
    if (uiView.mqtt) g.fillCircle(113, 8, 2, W_);
    else g.drawCircle(113, 8, 2, W_);
  }
}

// ---------------------------------------------------------------------
// Dashboard
// ---------------------------------------------------------------------
static void drawShaft(Adafruit_GFX& g) {
  // Zona biru (y >= 16). Landing Floor 2 di y=30, Floor 1 di y=61.
  g.drawFastVLine(9, 17, 45, W_);
  g.drawFastVLine(29, 17, 45, W_);
  g.drawFastHLine(6, 30, 27, W_);
  g.drawFastHLine(6, 61, 27, W_);
  putText(g, 0, 24, "2");
  putText(g, 0, 55, "1");

  int top;   // tinggi kereta 12 px: puncak 18 (Floor 2) .. 49 (Floor 1)
  if (uiView.positionValid) {
    float frac = 0.0f;
    if (uiView.travelCalibrated && uiView.floor2Ticks > 0) {
      frac = clampf((float)uiView.ticks / (float)uiView.floor2Ticks, -0.06f, 1.06f);
    }
    top = 49 - (int)(frac * 31.0f + (frac >= 0 ? 0.5f : -0.5f));
  } else {
    top = 33;   // posisi tidak diketahui: gantung di tengah
  }

  if (top > 16) g.drawFastVLine(19, 16, top - 16, W_);   // kabel

  if (!uiView.positionValid) {
    g.drawRect(12, top, 15, 12, W_);
    putText(g, 17, top + 2, "?");
    return;
  }

  g.fillRect(12, top, 15, 12, W_);
  const int dir = uiTravelDir();
  if (dir > 0) {
    g.fillTriangle(19, top + 2, 15, top + 8, 23, top + 8, B_);
  } else if (dir < 0) {
    g.fillTriangle(19, top + 9, 15, top + 3, 23, top + 3, B_);
  } else {
    g.drawFastVLine(19, top + 1, 10, B_);   // pintu
  }
}

static void dashMessage(char* l1, char* l2) {
  l1[0] = l2[0] = '\0';
  const char* code = uiView.info;

  if (code && strcmp(code, "ARRIVED") == 0) {
    const int f = uiFloor();
    snprintf(l1, 16, "Sampai di");
    if (f > 0) snprintf(l2, 16, "Lantai %d", f);
    return;
  }
  const char* text = uiMsgLookup(code);
  if (text) {
    splitMsg(text, 15, l1, l2);
    return;
  }
  if (!uiView.positionValid) {
    splitMsg(uiMsgLookup("POSITION_UNREFERENCED"), 15, l1, l2);
  } else if (!uiView.travelCalibrated) {
    splitMsg(uiMsgLookup("CALIBRATE_FLOOR1_THEN_FLOOR2"), 15, l1, l2);
  } else if (uiView.state == STATE_IDLE) {
    snprintf(l1, 16, "UP   = Lantai 2");
    snprintf(l2, 16, "DOWN = Lantai 1");
  } else if (code && code[0]) {
    rawCodeMsg(code, 15, l1, l2);
  }
}

static void drawDashboard(Adafruit_GFX& g) {
  // Zona kuning: status besar di kiri, ikon koneksi di kanan.
  putText(g, 0, 1, stateLabel(), 2);
  drawStatusIcons(g);

  drawShaft(g);

  // Angka lantai besar (panah saat bergerak) + posisi tick
  const int f = uiFloor();
  char big[2] = { '?', '\0' };
  if (uiView.positionValid) {
    const int dir = uiTravelDir();
    // 24/25 = panah atas/bawah pada font bawaan Adafruit_GFX (CP437)
    big[0] = (f == 1) ? '1' : (f == 2) ? '2' : (dir > 0) ? (char)24 : (dir < 0) ? (char)25 : '-';
  }
  putText(g, 41, 19, big, 3);

  char row[24];
  snprintf(row, sizeof(row), "%ld", uiView.ticks);
  putRight(g, 127, 19, row, 2);

  if (uiView.state == STATE_MOVING) {
    snprintf(row, sizeof(row), "KE LANTAI %d", uiTravelDir() >= 0 ? 2 : 1);
  } else if (uiView.travelCalibrated) {
    snprintf(row, sizeof(row), "F2=%ld tick", uiView.floor2Ticks);
  } else {
    snprintf(row, sizeof(row), "F2 belum");
  }
  putRight(g, 127, 36, row);

  dotLineH(g, 38, 127, 45);

  // Area bawah: progres saat bergerak, pesan saat diam
  if (uiView.state == STATE_MOVING) {
    const int dir = uiTravelDir();
    const long span = labs(uiView.target - uiView.moveOrigin);
    long done = (uiView.ticks - uiView.moveOrigin) * dir;
    if (done < 0) done = 0;
    if (done > span) done = span;
    const float frac = (span > 0) ? (float)done / (float)span : 0.0f;
    long rem = (uiView.target - uiView.ticks) * dir;
    if (rem < 0) rem = 0;

    g.drawRect(38, 48, 90, 6, W_);
    const int fw = (int)(frac * 86.0f + 0.5f);
    if (fw > 0) g.fillRect(40, 50, fw, 2, W_);

    snprintf(row, sizeof(row), "Sisa %ld", rem);
    putText(g, 38, 56, row);
    snprintf(row, sizeof(row), "%ld/s", labs(uiView.speedTps));
    putRight(g, 127, 56, row);
  } else {
    char l1[24], l2[24];
    dashMessage(l1, l2);
    putText(g, 38, 48, l1);
    putText(g, 38, 56, l2);
  }
}

// ---------------------------------------------------------------------
// Detail angka
// ---------------------------------------------------------------------
static void drawDetail(Adafruit_GFX& g) {
  drawHeader(g, "LIFT DETAIL", PG_DETAIL);
  char v[24];

  snprintf(v, sizeof(v), "%ld tick", uiView.ticks);
  kvRow(g, 18, "Posisi", v);

  const long goal = uiView.lastGoalValid ? uiView.lastGoal : uiView.target;
  if (uiView.lastGoalValid) snprintf(v, sizeof(v), "%ld", goal);
  else snprintf(v, sizeof(v), "---");
  kvRow(g, 27, "Tujuan", v);

  if (uiView.lastGoalValid) snprintf(v, sizeof(v), "%+ld", goal - uiView.ticks);
  else snprintf(v, sizeof(v), "---");
  kvRow(g, 36, "Selisih", v);

  snprintf(v, sizeof(v), "%ld t/s", uiView.speedTps);
  kvRow(g, 45, "Kecepatan", v);

  if (uiView.motorDir > 0) snprintf(v, sizeof(v), "NAIK  PWM %d", uiView.pwm);
  else if (uiView.motorDir < 0) snprintf(v, sizeof(v), "TURUN PWM %d", uiView.pwm);
  else snprintf(v, sizeof(v), "berhenti");
  kvRow(g, 54, "Motor", v);
}

// ---------------------------------------------------------------------
// Trace posisi: grafik posisi lift beberapa detik terakhir
// ---------------------------------------------------------------------
#define TRACE_N 112
static long traceTicks[TRACE_N];
static bool traceOk[TRACE_N];
static int traceHead = 0;   // slot yang akan ditulis berikutnya = sampel tertua

static void traceAdd(long ticks, bool valid) {
  traceTicks[traceHead] = ticks;
  traceOk[traceHead] = valid;
  traceHead = (traceHead + 1) % TRACE_N;
}

static void drawTrace(Adafruit_GFX& g) {
  char title[24];
  snprintf(title, sizeof(title), "TRACE %lus", (unsigned long)(TRACE_N * DISPLAY_FRAME_MS / 1000UL));
  putText(g, 0, 4, title);
  char num[12];
  snprintf(num, sizeof(num), "%d/%d", PG_TRACE + 1, UI_PAGE_COUNT);
  putRight(g, 127, 4, num);
  snprintf(num, sizeof(num), "%ld", uiView.ticks);
  putRight(g, 104, 4, num);

  const int px0 = 15, px1 = 126;      // area plot
  const int py0 = 19, py1 = 61;

  // rentang sumbu Y
  bool any = false;
  long dmin = 0, dmax = 0;
  for (int i = 0; i < TRACE_N; ++i) {
    if (!traceOk[i]) continue;
    if (!any) { dmin = dmax = traceTicks[i]; any = true; }
    if (traceTicks[i] < dmin) dmin = traceTicks[i];
    if (traceTicks[i] > dmax) dmax = traceTicks[i];
  }
  float lo, hi;
  const bool haveFloors = uiView.travelCalibrated && uiView.floor2Ticks > 0;
  if (haveFloors) {
    const float f2 = (float)uiView.floor2Ticks;
    lo = -0.12f * f2;
    hi = 1.12f * f2;
    if (any && (float)dmin < lo) lo = (float)dmin - 0.03f * f2;
    if (any && (float)dmax > hi) hi = (float)dmax + 0.03f * f2;
  } else if (any) {
    lo = (float)dmin - 20.0f;
    hi = (float)dmax + 20.0f;
    if (hi - lo < 80.0f) { lo -= 40.0f; hi += 40.0f; }
  } else {
    lo = -50.0f;
    hi = 50.0f;
  }
  const float span = hi - lo;
  #define TRACE_Y(v) (py1 - (int)(((float)(v) - lo) / span * (float)(py1 - py0) + 0.5f))

  if (haveFloors) {
    const int y1 = clampi(TRACE_Y(FLOOR_1_TICKS), py0, py1);
    const int y2 = clampi(TRACE_Y(uiView.floor2Ticks), py0, py1);
    dotLineH(g, px0, px1, y1);
    dotLineH(g, px0, px1, y2);
    putText(g, 0, y1 - 3, "F1");
    putText(g, 0, y2 - 3, "F2");
  } else {
    g.drawFastVLine(px0 - 1, py0, py1 - py0 + 1, W_);
  }

  // garis posisi, tertua di kiri
  int prevX = -1, prevY = 0;
  for (int i = 0; i < TRACE_N; ++i) {
    const int idx = (traceHead + i) % TRACE_N;
    if (!traceOk[idx]) { prevX = -1; continue; }
    const int x = px0 + 1 + i;
    const int y = clampi(TRACE_Y(traceTicks[idx]), py0, py1);
    if (prevX >= 0) g.drawLine(prevX, prevY, x, y, W_);
    else g.drawPixel(x, y, W_);
    prevX = x;
    prevY = y;
  }
  if (prevX >= 0) g.fillCircle(prevX, prevY, 2, W_);   // titik "sekarang"
  #undef TRACE_Y
}

// ---------------------------------------------------------------------
// Kapasitas rak
// ---------------------------------------------------------------------
static void drawRackBlock(Adafruit_GFX& g, int idx, int y) {
  char buf[24];
  snprintf(buf, sizeof(buf), "RAK %d", idx + 1);
  putText(g, 0, y, buf);

  const int st = uiView.rackStatus[idx];
  const char* label = rackLabel(st);
  if (st == RACK_CAPACITY_NEAR_FULL) {
    const int w = textW(label) + 5;
    g.fillRect(128 - w, y - 1, w, 9, W_);
    putText(g, 128 - w + 3, y, label, 1, B_);
  } else {
    putRight(g, 127, y, label);
  }

  const int by = y + 11;
  g.drawRect(0, by, 86, 8, W_);
  int fw = 0;
  if (uiView.sensorValid[idx]) {
    const float f = clampf(1.0f - uiView.sensorDist[idx] / RACK_GAUGE_MAX_CM, 0.0f, 1.0f);
    fw = (int)(f * 82.0f + 0.5f);
    if (fw > 0) g.fillRect(2, by + 2, fw, 4, W_);
  }

  // penanda batas "hampir penuh"
  const float thr = (idx == 0) ? RACK1_NEAR_FULL_ENTER_CM : RACK2_NEAR_FULL_ENTER_CM;
  const float ft = clampf(1.0f - thr / RACK_GAUGE_MAX_CM, 0.0f, 1.0f);
  const int tx = 2 + (int)(ft * 82.0f);
  g.drawFastVLine(tx, by - 2, 2, W_);
  g.drawFastVLine(tx, by + 8, 2, W_);
  g.drawFastVLine(tx, by + 2, 4, (tx < 2 + fw) ? B_ : W_);

  if (uiView.sensorValid[idx]) {
    if (uiView.sensorDist[idx] < 100.0f) snprintf(buf, sizeof(buf), "%.1fcm", uiView.sensorDist[idx]);
    else snprintf(buf, sizeof(buf), "%.0fcm", uiView.sensorDist[idx]);
  } else {
    snprintf(buf, sizeof(buf), "--- cm");
  }
  putRight(g, 127, by + 1, buf);
}

static void drawRack(Adafruit_GFX& g) {
  drawHeader(g, "KAPASITAS RAK", PG_RACK);
  if (!uiView.rackEnabled) {
    putCenter(g, 30, "SENSOR RAK NONAKTIF");
    putCenter(g, 42, "FEATURE_RACK = 0");
    return;
  }
  drawRackBlock(g, 0, 18);
  drawRackBlock(g, 1, 40);
}

// ---------------------------------------------------------------------
// Jaringan
// ---------------------------------------------------------------------
static void drawNetwork(Adafruit_GFX& g) {
  drawHeader(g, "JARINGAN", PG_NET);
  char v[24];
  const bool ssidSet = strcmp(WIFI_SSID, "Nama_WiFi_Kamu") != 0;

  if (!uiView.networkEnabled) snprintf(v, sizeof(v), "nonaktif");
  else if (!ssidSet) snprintf(v, sizeof(v), "belum diatur");
  else if (uiView.wifi) snprintf(v, sizeof(v), "%.14s", WIFI_SSID);
  else snprintf(v, sizeof(v), "mencari...");
  kvRow(g, 18, "WiFi", v);

  if (uiView.networkEnabled && uiView.wifi) snprintf(v, sizeof(v), "%d dBm %s", uiView.rssi, rssiWord(uiView.rssi));
  else snprintf(v, sizeof(v), "---");
  kvRow(g, 27, "Sinyal", v);

  if (uiView.networkEnabled && uiView.wifi) snprintf(v, sizeof(v), "%.15s", uiView.ip);
  else snprintf(v, sizeof(v), "---");
  kvRow(g, 36, "IP", v);

  if (!uiView.networkEnabled) snprintf(v, sizeof(v), "nonaktif");
  else if (uiView.mqtt) snprintf(v, sizeof(v), "tersambung");
  else if (uiView.wifi) snprintf(v, sizeof(v), "menyambung...");
  else snprintf(v, sizeof(v), "tunggu WiFi");
  kvRow(g, 45, "MQTT", v);

  if (!uiView.networkEnabled) snprintf(v, sizeof(v), "---");
  else if (uiView.admin == 2) snprintf(v, sizeof(v), "LOG AKTIF");
  else if (uiView.admin == 1) snprintf(v, sizeof(v), "siap");
  else snprintf(v, sizeof(v), "nonaktif");
  kvRow(g, 54, "Admin", v);
}

// ---------------------------------------------------------------------
// Sistem + status input
// ---------------------------------------------------------------------
static void drawInputBox(Adafruit_GFX& g, int x, const char* label, bool on) {
  const int w = 31, h = 10, y = 53;
  const int tx = x + (w - textW(label)) / 2;
  if (on) {
    g.fillRect(x, y, w, h, W_);
    putText(g, tx, y + 1, label, 1, B_);
  } else {
    g.drawRect(x, y, w, h, W_);
    putText(g, tx, y + 1, label, 1, W_);
  }
}

static void drawSystem(Adafruit_GFX& g) {
  drawHeader(g, "SISTEM", PG_SYS);
  char v[24];

  const unsigned long secs = millis() / 1000UL;
  snprintf(v, sizeof(v), "%lu:%02lu:%02lu", secs / 3600UL, (secs / 60UL) % 60UL, secs % 60UL);
  kvRow(g, 18, "Uptime", v);

  snprintf(v, sizeof(v), "%lu KB", (unsigned long)(ESP.getFreeHeap() / 1024UL));
  kvRow(g, 27, "Memori", v);

  if (uiView.lastFault && uiView.lastFault[0]) snprintf(v, sizeof(v), "%.15s", uiView.lastFault);
  else snprintf(v, sizeof(v), "tidak ada");
  kvRow(g, 36, "Fault", v);

  snprintf(v, sizeof(v), "%.15s", (uiView.build && uiView.build[0]) ? uiView.build : "-");
  kvRow(g, 45, "Build", v);

  drawInputBox(g, 0, "UP", uiView.upPressed);
  drawInputBox(g, 32, "DOWN", uiView.downPressed);
  drawInputBox(g, 64, "MODE", uiView.modePressed);
  drawInputBox(g, 96, "ESTOP", uiView.estopPressed);
}

// ---------------------------------------------------------------------
// Wizard kalibrasi
// ---------------------------------------------------------------------
static void drawCalibration(Adafruit_GFX& g) {
  // Zona kuning menyala penuh sebagai banner.
  g.fillRect(0, 0, 128, 16, W_);
  putText(g, 3, 4, "KALIBRASI", 1, B_);
  const char* step = (uiView.calStage == 1) ? "1/2" : (uiView.calStage == 2) ? "2/2" : "REF";
  putRight(g, 124, 4, step, 1, B_);
  if (uiView.motorDir > 0) g.fillTriangle(64, 4, 60, 11, 68, 11, B_);
  else if (uiView.motorDir < 0) g.fillTriangle(64, 11, 60, 4, 68, 4, B_);

  const char* heading = "";
  const char* instr = "";
  char headBuf[24];
  switch (uiView.calStage) {
    case 1:
      heading = "TAHAP 1: SET FLOOR 1";
      instr = "Jog ke tanda Floor 1|lalu lepas tombol";
      break;
    case 2:
      heading = "TAHAP 2: UKUR FLOOR 2";
      instr = "Jog UP ke tanda Floor 2|lalu lepas tombol";
      break;
    case 3:
      if (uiView.calibrationFloor == 3) {
        heading = "UKUR ULANG F1 + F2";
        instr = "Hapus travel lama|lalu mulai Tahap 1";
      } else {
        snprintf(headBuf, sizeof(headBuf), "RE-REFERENSI: F%d", uiView.calibrationFloor);
        heading = headBuf;
        instr = "Klik MODE: ganti opsi|Lift harus di situ";
      }
      break;
    default:
      heading = "MENYIAPKAN...";
      break;
  }
  putCenter(g, 19, heading);
  g.drawFastHLine(0, 28, 128, W_);

  // Pesan penolakan/peringatan menggantikan petunjuk bila ada.
  const char* alert = uiMsgLookup(uiView.info);
  char l1[28], l2[28];
  splitMsg(alert ? alert : instr, 21, l1, l2);
  putText(g, 0, 32, l1);
  putText(g, 0, 40, l2);

  // pembacaan langsung
  char row[28];
  snprintf(row, sizeof(row), "ENC %ld", uiView.ticks);
  putText(g, 0, 48, row);
  if (uiView.calStage == 2) {
    snprintf(row, sizeof(row), "F1>F2 %+ld", uiView.calDelta);
    putRight(g, 127, 48, row);
  } else if (uiView.calStage == 3 && uiView.travelCalibrated) {
    snprintf(row, sizeof(row), "F2 %ld", uiView.floor2Ticks);
    putRight(g, 127, 48, row);
  }

  // bar tahan MODE
  if (uiView.modeHeldMs > 0) {
    float f = (float)uiView.modeHeldMs / (float)CALIBRATION_CONFIRM_HOLD_MS;
    f = clampf(f, 0.0f, 1.0f);
    g.drawRect(0, 56, 128, 8, W_);
    const int fw = (int)(f * 124.0f + 0.5f);
    if (fw > 0) g.fillRect(2, 58, fw, 4, W_);
  } else {
    putCenter(g, 56, "Tahan MODE 1,5s: OK");
  }
}

// ---------------------------------------------------------------------
// Fault dan E-STOP
// ---------------------------------------------------------------------
static void drawFault(Adafruit_GFX& g) {
  g.fillRect(0, 0, 128, 16, W_);
  putText(g, 3, 4, "FAULT", 1, B_);
  putRight(g, 124, 4, "motor berhenti", 1, B_);

  const char* code = (uiView.lastFault && uiView.lastFault[0]) ? uiView.lastFault : "UNSPECIFIED";
  const UiFault* hit = nullptr;
  for (size_t i = 0; i < sizeof(UI_FAULTS) / sizeof(UI_FAULTS[0]); ++i) {
    if (strcmp(UI_FAULTS[i].code, code) == 0) { hit = &UI_FAULTS[i]; break; }
  }

  char l1[28], l2[28];
  if (hit) {
    putCenter(g, 19, hit->title);
    splitMsg(hit->text, 21, l1, l2);
  } else {
    char t[22];
    snprintf(t, sizeof(t), "%.21s", code);
    putCenter(g, 19, t);
    splitMsg("Lihat Serial Monitor|untuk rinciannya", 21, l1, l2);
  }
  g.drawFastHLine(0, 28, 128, W_);
  putText(g, 0, 31, l1);
  putText(g, 0, 39, l2);
  dotLineH(g, 0, 127, 48);
  putCenter(g, 50, "ACK_ERROR: admin/USB");
  putCenter(g, 57, "lalu ref: MODE x3");
}

static void drawEstop(Adafruit_GFX& g) {
  g.fillRect(0, 0, 128, 16, W_);
  putCenter(g, 1, "E-STOP", 2, B_);
  putCenter(g, 21, "Tombol darurat aktif");
  putCenter(g, 31, "Motor dimatikan");
  dotLineH(g, 0, 127, 42);
  putCenter(g, 46, "Lepas tombol E-STOP");
  putCenter(g, 55, "lalu MODE x3: ref.");
}

// ---------------------------------------------------------------------
// Boot
// ---------------------------------------------------------------------
static void drawBoot(Adafruit_GFX& g) {
  putCenter(g, 1, "SMART WH", 2);                 // zona kuning
  putCenter(g, 24, "Lift Controller");
  g.drawRect(14, 38, 100, 8, W_);
  const int fw = (int)(clampf((float)uiView.bootTarget / 100.0f, 0.0f, 1.0f) * 96.0f + 0.5f);
  if (fw > 0) g.fillRect(16, 40, fw, 4, W_);

  char t[28];
  const char* step = uiView.bootText ? uiView.bootText : "";
  if (strcmp(step, "Siap") == 0) snprintf(t, sizeof(t), "Siap!");
  else snprintf(t, sizeof(t), "Memulai %s", step);
  putCenter(g, 52, t);
}

// ---------------------------------------------------------------------
// Pemilihan scene
// ---------------------------------------------------------------------
static int desiredScene() {
  if (!uiView.bootDone) return SC_BOOT;
  if (uiView.state == STATE_ESTOP || uiView.estopPressed) return SC_ESTOP;
  if (uiView.calibrationActive) return SC_CAL;

  int page = uiView.page;
  if (page < 0) page = 0;
  if (page >= UI_PAGE_COUNT) page = UI_PAGE_COUNT - 1;
  if (uiView.state == STATE_ERROR && page == PG_DASH) return SC_FAULT;
  return SC_PAGE0 + page;
}

static void renderScene(Adafruit_GFX& g, int scene) {
  g.fillScreen(B_);
  g.setTextWrap(false);
  g.setTextColor(W_);
  g.setTextSize(1);

  switch (scene) {
    case SC_BOOT:  drawBoot(g); break;
    case SC_CAL:   drawCalibration(g); break;
    case SC_FAULT: drawFault(g); break;
    case SC_ESTOP: drawEstop(g); break;
    case SC_PAGE0 + PG_DASH:   drawDashboard(g); break;
    case SC_PAGE0 + PG_DETAIL: drawDetail(g); break;
    case SC_PAGE0 + PG_TRACE:  drawTrace(g); break;
    case SC_PAGE0 + PG_RACK:   drawRack(g); break;
    case SC_PAGE0 + PG_NET:    drawNetwork(g); break;
    case SC_PAGE0 + PG_SYS:    drawSystem(g); break;
    default:       drawDashboard(g); break;
  }
}

void displayInit(uint8_t addr) {
  Wire.begin(PIN_OLED_SDA, PIN_OLED_SCL);
  Wire.setClock(400000);
  oledOk = oled.begin(SSD1306_SWITCHCAPVCC, addr);
  if (oledOk) {
    oled.clearDisplay();
    oled.setTextColor(W_);
    oled.display();
  }
}

void displayTask(void* arg) {
  (void)arg;
  for (;;) {
    const unsigned long t0 = millis();

    if (uiQueue) {
      UiData incoming;
      if (xQueueReceive(uiQueue, &incoming, 0) == pdTRUE) uiView = incoming;
    }

    traceAdd(uiView.ticks, uiView.bootDone && uiView.positionValid);

    if (oledOk) {
      renderScene(oled, desiredScene());
      oled.display();
    }

    const unsigned long spent = millis() - t0;
    vTaskDelay(pdMS_TO_TICKS(spent >= DISPLAY_FRAME_MS ? 1UL : (DISPLAY_FRAME_MS - spent)));
  }
}

#endif
