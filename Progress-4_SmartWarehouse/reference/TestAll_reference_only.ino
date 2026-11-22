// ===================================================================
//  TEST SEMUA HARDWARE - SmartWarehouse (ESP32 + L298N + JGA25)
//  Motor, Encoder, UP/DOWN, E-STOP, MODE, HC-SR04 x2, OLED
//  Serial Monitor 115200 (kirim perintah tanpa Enter-pun jalan)
//
//  PERINTAH SERIAL
//    u  = motor NAIK 1 detik      d  = motor TURUN 1 detik
//    x  = STOP                    + / - = PWM +10 / -10 (60..245)
//    0  = nol-kan hitungan encoder
//    t  = tes otomatis (naik 1 s, turun 1 s) + cek arah encoder -> konfirmasi 'y'
//    h  = bantuan
//  TOMBOL
//    UP / DOWN : tahan = motor jalan (otomatis berhenti setelah 3 detik)
//    MODE      : ganti halaman OLED, diam 5 detik -> kembali ke halaman 1
//    E-STOP    : motor langsung mati, perintah motor diblokir selama aktif
// ===================================================================

#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>

// ---------- PIN ----------
#define PIN_PWM       25   // ENB L298N (jumper ENB dilepas)
#define PIN_IN1       26   // IN3
#define PIN_IN2       27   // IN4
#define PIN_ENC_A     34
#define PIN_ENC_B     35
#define PIN_ESTOP     32   // push button biasa (NO): satu kaki ke pin, satu kaki ke GND
#define PIN_BTN_MODE  33
#define PIN_BTN_UP    14
#define PIN_BTN_DOWN  13
#define PIN_OLED_SDA  21
#define PIN_OLED_SCL  22
#define PIN_TRIG1     17   // lantai 1 (bawah)
#define PIN_ECHO1     16
#define PIN_TRIG2     18   // lantai 2 (atas)
#define PIN_ECHO2     19

// ---------- PENGATURAN ----------
#define MODE_ACTIVE_LEVEL  LOW   // LOW = push button biasa, HIGH = wiring NC
#define ESTOP_ACTIVE_LEVEL LOW   // LOW = push button biasa (NO), HIGH = tombol tipe NC
#define MOTOR_DIR_INVERT   0     // ubah ke 1 kalau "NAIK" ternyata lift turun
#define ENC_INVERT         1     // 1 = balik arah hitungan encoder (naik harus = tick bertambah)
#define PWM_START          200   // 0-255, PWM dasar (bisa diubah lewat + / -)
#define PWM_MIN            60
#define PWM_MAX            245
#define MAX_JOG_MS         3000  // batas tahan tombol UP/DOWN
#define PULSE_MS           1000  // lama perintah u / d
#define SEQ_RUN_MS         1000  // lama tiap arah di tes otomatis
#define DEBOUNCE_MS        30
#define PAGE_TIMEOUT_MS    5000
#define NUM_PAGES          4

// ---------- KECEPATAN: jangan terlalu cepat / terlalu lambat ----------
#define PWM_UP_PCT    100   // koreksi arah NAIK  (% dari PWM dasar)
#define PWM_DOWN_PCT  100   // koreksi arah TURUN (lift turun dibantu gravitasi, kalau terlalu cepat kecilkan, mis. 70)
#define RAMP_MS       300   // waktu naik pelan-pelan dari PWM_KICK ke PWM target (soft-start)
#define PWM_KICK      110   // PWM awal saat soft-start

// ---------- PWM (core 2.x / 3.x) ----------
#define PWM_FREQ    2000   // L298N: jangan terlalu tinggi
#define PWM_RES     8
#define PWM_CHANNEL 0
void setupPwm() {
#if ESP_ARDUINO_VERSION_MAJOR >= 3
  ledcAttach(PIN_PWM, PWM_FREQ, PWM_RES);
#else
  ledcSetup(PWM_CHANNEL, PWM_FREQ, PWM_RES);
  ledcAttachPin(PIN_PWM, PWM_CHANNEL);
#endif
}
void writePwm(int duty) {
#if ESP_ARDUINO_VERSION_MAJOR >= 3
  ledcWrite(PIN_PWM, duty);
#else
  ledcWrite(PWM_CHANNEL, duty);
#endif
}

// ---------- OLED ----------
Adafruit_SSD1306 oled(128, 64, &Wire, -1);
bool    oledOk   = false;
uint8_t oledAddr = 0;

// ---------- INPUT ----------
#define IDX_UP    0
#define IDX_DOWN  1
#define IDX_ESTOP 2
#define IDX_MODE  3
const int           N = 4;
const uint8_t       pins[N]        = { PIN_BTN_UP, PIN_BTN_DOWN, PIN_ESTOP, PIN_BTN_MODE };
const uint8_t       activeLevel[N] = { LOW,        LOW,          ESTOP_ACTIVE_LEVEL, MODE_ACTIVE_LEVEL };
const char*         names[N]       = { "UP   ", "DOWN ", "ESTOP", "MODE " };
bool                stable[N]      = { false, false, false, false };
bool                lastRaw[N]     = { false, false, false, false };
unsigned long       tChange[N]     = { 0, 0, 0, 0 };
unsigned long       counts[N]      = { 0, 0, 0, 0 };

// ---------- STATE ----------
volatile long ticks      = 0;
volatile bool estopFlag  = false;
int   pwmVal   = PWM_START;
int   motorDir = 0;                 // 1 = naik, -1 = turun, 0 = stop
unsigned long pulseUntil = 0;
unsigned long rampStart  = 0;
int           pulseDir   = 0;
unsigned long jogSince   = 0;
bool          jogLocked  = false;

// tes otomatis: 0 idle, 1 tunggu konfirmasi, 2 naik, 3 jeda, 4 turun, 5 jeda akhir
int           seqState = 0;
unsigned long tSeq     = 0;
long          seqTicks0 = 0, seqUp = 0, seqDown = 0;

// sensor
float         dist[2]    = { -1, -1 };
unsigned long okCount[2] = { 0, 0 };
unsigned long noCount[2] = { 0, 0 };
float         dMin[2]    = { 9999, 9999 };
float         dMax[2]    = { 0, 0 };
bool          turn       = false;
unsigned long tSensor = 0;

int           page = 0;
unsigned long tLastActivity = 0, tOled = 0, tRaw = 0, tStat = 0;
long          lastStatTicks = 0;
long          speedTps = 0;

// ---------- ISR ----------
void IRAM_ATTR countEncoder() {
  if ((digitalRead(PIN_ENC_B) == HIGH) != (ENC_INVERT != 0)) ticks++;
  else                                                       ticks--;
}
void IRAM_ATTR estopISR() {
  digitalWrite(PIN_IN1, LOW);
  digitalWrite(PIN_IN2, LOW);
  estopFlag = true;
}

bool estopActive() { return digitalRead(PIN_ESTOP) == ESTOP_ACTIVE_LEVEL; }

// ---------- MOTOR ----------
const char* motorName() {
  if (motorDir > 0) return "NAIK";
  if (motorDir < 0) return "TURUN";
  return "STOP";
}

void motorStop() {
  digitalWrite(PIN_IN1, LOW);
  digitalWrite(PIN_IN2, LOW);
  writePwm(0);
  if (motorDir != 0) {
    motorDir = 0;
    Serial.println("[MOTOR] STOP");
  }
}

void motorDrive(int dir, int pwm) {
  if (dir == 0 || estopFlag || estopActive()) { motorStop(); return; }
  bool naik = (dir > 0) != (MOTOR_DIR_INVERT != 0);
  digitalWrite(PIN_IN1, naik ? HIGH : LOW);
  digitalWrite(PIN_IN2, naik ? LOW : HIGH);

  if (motorDir != dir) rampStart = millis();

  // PWM target per arah + soft-start (naik bertahap, mengurangi sentakan dan lonjakan arus)
  int target = pwm * ((dir > 0) ? PWM_UP_PCT : PWM_DOWN_PCT) / 100;
  target = constrain(target, 0, 255);
  int eff = target;
  unsigned long el = millis() - rampStart;
  if (el < RAMP_MS && target > PWM_KICK) eff = PWM_KICK + (long)(target - PWM_KICK) * (long)el / RAMP_MS;
  writePwm(eff);

  if (motorDir != dir) {
    motorDir = dir;
    Serial.printf("[MOTOR] %s target=%d IN3(GPIO%d)=%d IN4(GPIO%d)=%d\n",
                  motorName(), target, PIN_IN1, digitalRead(PIN_IN1), PIN_IN2, digitalRead(PIN_IN2));
  }
}

// ---------- SENSOR ----------
float readCm(int trig, int echo) {
  digitalWrite(trig, LOW);  delayMicroseconds(2);
  digitalWrite(trig, HIGH); delayMicroseconds(10);
  digitalWrite(trig, LOW);
  unsigned long t = pulseIn(echo, HIGH, 25000);
  if (t == 0) return -1;
  float cm = t * 0.0343f / 2.0f;
  return (cm > 400.0f) ? -1 : cm;
}

void readSensor(int idx) {
  float d = (idx == 0) ? readCm(PIN_TRIG1, PIN_ECHO1) : readCm(PIN_TRIG2, PIN_ECHO2);
  dist[idx] = d;
  if (d < 0) { noCount[idx]++; return; }
  okCount[idx]++;
  if (d < dMin[idx]) dMin[idx] = d;
  if (d > dMax[idx]) dMax[idx] = d;
}

void fmtCm(char* buf, int len, float d) {
  if (d < 0) snprintf(buf, len, "---");
  else       snprintf(buf, len, "%.1f", d);
}

// ---------- TES OTOMATIS ----------
void abortSeq(const char* why) {
  if (seqState == 0) return;
  seqState = 0;
  motorStop();
  Serial.printf("[TES] dibatalkan: %s\n", why);
}

void printSeqReport() {
  Serial.println("---------- HASIL TES ----------");
  Serial.printf("Naik  : delta tick = %ld\n", seqUp);
  Serial.printf("Turun : delta tick = %ld\n", seqDown);
  if (labs(seqUp) < 5 && labs(seqDown) < 5) {
    Serial.println("HASIL: tick tidak berubah. Cek: 12V/driver, jumper ENB, kabel motor, atau encoder (VCC 3.3V, A=34, B=35).");
  } else if (seqUp > 0 && seqDown < 0) {
    Serial.println("HASIL: OK. Naik = tick bertambah, turun = tick berkurang.");
    Serial.println("Cek manual: apakah lift benar-benar NAIK saat 'NAIK'? Kalau terbalik, ubah MOTOR_DIR_INVERT.");
  } else if (seqUp < 0 && seqDown > 0) {
    Serial.println("HASIL: arah encoder TERBALIK. Tukar kabel A dan B (GPIO 34 <-> 35), atau tukar di kode.");
  } else {
    Serial.println("HASIL: tidak konsisten (cek kabel encoder / pull-up 10k di A dan B / slip mekanik).");
  }
  Serial.println("-------------------------------");
}

void updateSeq() {
  unsigned long now = millis();
  switch (seqState) {
    case 1:
      if (now - tSeq > 5000) { seqState = 0; Serial.println("[TES] konfirmasi kedaluwarsa."); }
      break;
    case 2:
      motorDrive(1, pwmVal);
      if (now - tSeq >= SEQ_RUN_MS) {
        motorStop();
        seqUp = ticks - seqTicks0;
        seqState = 3; tSeq = now;
      }
      break;
    case 3:
      if (now - tSeq >= 800) {
        seqTicks0 = ticks;
        seqState = 4; tSeq = now;
      }
      break;
    case 4:
      motorDrive(-1, pwmVal);
      if (now - tSeq >= SEQ_RUN_MS) {
        motorStop();
        seqDown = ticks - seqTicks0;
        seqState = 5; tSeq = now;
      }
      break;
    case 5:
      if (now - tSeq >= 500) {
        seqState = 0;
        printSeqReport();
      }
      break;
  }
}

// ---------- MOTOR (prioritas: e-stop > tes > tombol > perintah serial) ----------
void updateMotor() {
  unsigned long now = millis();

  if (estopFlag || estopActive()) {
    motorStop();
    pulseUntil = 0;
    abortSeq("E-STOP");
    return;
  }

  bool up = stable[IDX_UP], down = stable[IDX_DOWN];
  if ((up || down) && seqState >= 2) abortSeq("tombol ditekan");

  if (seqState == 1) updateSeq();   // hanya cek kedaluwarsa konfirmasi
  if (seqState >= 2) { updateSeq(); return; }

  if (up != down) {
    if (jogLocked) { motorStop(); return; }
    if (jogSince == 0) jogSince = now;
    if (now - jogSince > MAX_JOG_MS) {
      jogLocked = true;
      motorStop();
      Serial.println("[MOTOR] batas tahan 3 detik, lepas tombol dulu.");
      return;
    }
    pulseUntil = 0;
    motorDrive(up ? 1 : -1, pwmVal);
    return;
  }

  jogSince = 0;
  jogLocked = false;

  if (now < pulseUntil) { motorDrive(pulseDir, pwmVal); return; }
  if (motorDir != 0) motorStop();
}

// ---------- SERIAL ----------
void printHelp() {
  Serial.println("PERINTAH: u=naik 1s  d=turun 1s  x=stop  +/-=PWM  0=nol encoder  t=tes otomatis  h=bantuan");
}

void handleSerial() {
  while (Serial.available()) {
    char c = Serial.read();
    if (c == '\n' || c == '\r' || c == ' ') continue;
    tLastActivity = millis();

    if (c == 'y' && seqState == 1) {
      if (estopFlag || estopActive()) { seqState = 0; Serial.println("[TES] e-stop aktif."); continue; }
      seqTicks0 = ticks;
      seqState = 2; tSeq = millis();
      Serial.println("[TES] mulai: NAIK...");
      continue;
    }
    if (seqState == 1) { seqState = 0; Serial.println("[TES] dibatalkan."); }

    switch (c) {
      case 'u':
      case 'd':
        if (seqState >= 2) break;
        if (estopFlag || estopActive()) { Serial.println("[MOTOR] diblokir: E-STOP aktif."); break; }
        pulseDir = (c == 'u') ? 1 : -1;
        pulseUntil = millis() + PULSE_MS;
        break;
      case 'x':
        pulseUntil = 0;
        abortSeq("perintah stop");
        motorStop();
        break;
      case '+':
        pwmVal = min(pwmVal + 10, PWM_MAX);
        Serial.printf("[PWM] %d\n", pwmVal);
        break;
      case '-':
        pwmVal = max(pwmVal - 10, PWM_MIN);
        Serial.printf("[PWM] %d\n", pwmVal);
        break;
      case '0':
        ticks = 0;
        Serial.println("[ENC] ticks = 0");
        break;
      case 't':
        if (estopFlag || estopActive()) { Serial.println("[TES] diblokir: E-STOP aktif."); break; }
        if (stable[IDX_UP] || stable[IDX_DOWN]) { Serial.println("[TES] lepas tombol UP/DOWN dulu."); break; }
        Serial.println("[TES] PERINGATAN: lift akan naik 1 detik lalu turun 1 detik.");
        Serial.println("[TES] Pastikan lift di tengah (tidak dekat batas atas/bawah), tangan jauh. Ketik y dalam 5 detik, atau x untuk batal.");
        seqState = 1; tSeq = millis();
        break;
      case 'h':
        printHelp();
        break;
      default:
        break;
    }
  }
}

// ---------- OLED ----------
uint8_t scanI2C() {
  uint8_t found = 0;
  Serial.println("[I2C] Scan...");
  for (uint8_t addr = 1; addr < 127; addr++) {
    Wire.beginTransmission(addr);
    if (Wire.endTransmission() == 0) {
      Serial.printf("[I2C] Device ditemukan di 0x%02X\n", addr);
      if (!found && (addr == 0x3C || addr == 0x3D)) found = addr;
    }
  }
  if (!found) Serial.println("[I2C] OLED TIDAK ketemu. Cek VCC(3.3V), GND, SDA=21, SCL=22.");
  return found;
}

const char* pageTitle(int p) {
  switch (p) {
    case 0: return "NORMAL";
    case 1: return "INPUT";
    case 2: return "ENCODER+MOTOR";
    case 3: return "SENSOR";
  }
  return "?";
}

void drawOled() {
  if (!oledOk) return;
  oled.clearDisplay();
  oled.setTextSize(1);

  oled.setCursor(0, 0);
  oled.printf("[%d/%d] %s", page + 1, NUM_PAGES, pageTitle(page));
  oled.drawFastHLine(0, 9, 128, SSD1306_WHITE);
  if ((millis() / 500) % 2) oled.fillCircle(124, 3, 2, SSD1306_WHITE);

  char b1[12], b2[12];
  fmtCm(b1, sizeof(b1), dist[0]);
  fmtCm(b2, sizeof(b2), dist[1]);

  if (page == 0) {
    oled.setCursor(0, 12); oled.printf("Pos  : %ld", (long)ticks);
    oled.setCursor(0, 23); oled.printf("Motor: %s pwm=%d", motorName(), pwmVal);
    oled.setCursor(0, 34); oled.printf("L1   : %s cm", b1);
    oled.setCursor(0, 45); oled.printf("L2   : %s cm", b2);
  } else if (page == 1) {
    for (int i = 0; i < N; i++) {
      oled.setCursor(0, 12 + i * 11);
      oled.printf("%s: %s (%lu)", names[i], stable[i] ? "ON " : "off", counts[i]);
    }
  } else if (page == 2) {
    oled.setCursor(0, 12); oled.printf("Ticks: %ld", (long)ticks);
    oled.setCursor(0, 23); oled.printf("A=%d B=%d", digitalRead(PIN_ENC_A), digitalRead(PIN_ENC_B));
    oled.setCursor(0, 34); oled.printf("Speed: %ld t/s", speedTps);
    oled.setCursor(0, 45); oled.printf("IN3=%d IN4=%d P%d", digitalRead(PIN_IN1), digitalRead(PIN_IN2), pwmVal);
  } else {
    oled.setCursor(0, 12); oled.printf("L1: %s cm", b1);
    oled.setCursor(0, 23); oled.printf("ok=%lu gagal=%lu", okCount[0], noCount[0]);
    oled.setCursor(0, 34); oled.printf("L2: %s cm", b2);
    oled.setCursor(0, 45); oled.printf("ok=%lu gagal=%lu", okCount[1], noCount[1]);
  }

  oled.setCursor(0, 56);
  if (estopFlag || estopActive()) {
    oled.print("!! E-STOP !!");
  } else if (seqState >= 1) {
    oled.print("TES OTOMATIS...");
  } else if (page == 0) {
    oled.print("MODE: ganti halaman");
  } else {
    long sisa = (PAGE_TIMEOUT_MS - (long)(millis() - tLastActivity) + 999) / 1000;
    if (sisa < 0) sisa = 0;
    oled.printf("Kembali otomatis: %lds", sisa);
  }
  oled.display();
}

// ---------- SETUP / LOOP ----------
void setup() {
  // Motor mati dulu, secepat mungkin
  pinMode(PIN_IN1, OUTPUT);
  pinMode(PIN_IN2, OUTPUT);
  digitalWrite(PIN_IN1, LOW);
  digitalWrite(PIN_IN2, LOW);
  setupPwm();
  writePwm(0);

  Serial.begin(115200);
  delay(500);
  Serial.println();
  Serial.println("=== TEST SEMUA HARDWARE SmartWarehouse ===");
  {
    esp_reset_reason_t r = esp_reset_reason();
    const char* nm = (r == ESP_RST_POWERON) ? "POWER ON" : (r == ESP_RST_BROWNOUT) ? "BROWNOUT (tegangan drop!)" :
                     (r == ESP_RST_SW) ? "software" : (r == ESP_RST_PANIC) ? "PANIC/crash" :
                     (r == ESP_RST_EXT) ? "tombol EN/reset" : "lainnya";
    Serial.printf("[BOOT] alasan reset: %s (%d)\n", nm, (int)r);
  }

  pinMode(PIN_ENC_A, INPUT);
  pinMode(PIN_ENC_B, INPUT);
  for (int i = 0; i < N; i++) pinMode(pins[i], INPUT_PULLUP);

  pinMode(PIN_TRIG1, OUTPUT); pinMode(PIN_ECHO1, INPUT);
  pinMode(PIN_TRIG2, OUTPUT); pinMode(PIN_ECHO2, INPUT);
  digitalWrite(PIN_TRIG1, LOW);
  digitalWrite(PIN_TRIG2, LOW);
  delay(100);
  Serial.printf("[HCSR04] ECHO idle: L1=%d L2=%d (harus 0)\n", digitalRead(PIN_ECHO1), digitalRead(PIN_ECHO2));

  Wire.begin(PIN_OLED_SDA, PIN_OLED_SCL);
  Wire.setClock(400000);
  oledAddr = scanI2C();
  if (oledAddr) {
    oledOk = oled.begin(SSD1306_SWITCHCAPVCC, oledAddr);
    Serial.println(oledOk ? "[OLED] Init OK" : "[OLED] Init GAGAL");
  }
  if (oledOk) oled.setTextColor(SSD1306_WHITE);

  attachInterrupt(digitalPinToInterrupt(PIN_ENC_A), countEncoder, RISING);
  attachInterrupt(digitalPinToInterrupt(PIN_ESTOP), estopISR, (ESTOP_ACTIVE_LEVEL == HIGH) ? RISING : FALLING);

  if (estopActive()) Serial.println("[ESTOP] AKTIF saat boot (tombol tertekan / terkunci). Motor diblokir.");

  tLastActivity = millis();
  printHelp();
  Serial.println("Tahap 1 (tanpa 12V): cek tombol, OLED, sensor, putar poros motor manual -> ticks harus berubah.");
  Serial.println("Tahap 2 (12V): lift di tengah, PWM kecil, coba tombol UP/DOWN lalu 't'.");
}

void loop() {
  unsigned long now = millis();

  // E-stop dilepas -> bersihkan flag (motor tetap berhenti sampai ada perintah baru)
  if (estopFlag && !estopActive()) {
    estopFlag = false;
    Serial.println("[ESTOP] dilepas.");
  } else if (estopFlag) {
    static unsigned long tMsg = 0;
    if (now - tMsg > 1000) { tMsg = now; Serial.println("[ESTOP] AKTIF - motor diblokir."); }
  }

  // Input + debounce
  for (int i = 0; i < N; i++) {
    bool raw = (digitalRead(pins[i]) == activeLevel[i]);
    if (raw != lastRaw[i]) { lastRaw[i] = raw; tChange[i] = now; }
    if (now - tChange[i] >= DEBOUNCE_MS && raw != stable[i]) {
      stable[i] = raw;
      tLastActivity = now;
      if (raw) counts[i]++;
      Serial.printf("[%s] %s  (hitungan = %lu)\n", names[i], raw ? "ON  / DITEKAN" : "off / dilepas", counts[i]);
      if (i == IDX_MODE && raw) {
        page = (page + 1) % NUM_PAGES;
        Serial.printf("[PAGE] -> %d/%d (%s)\n", page + 1, NUM_PAGES, pageTitle(page));
      }
    }
  }

  if (page != 0 && now - tLastActivity >= PAGE_TIMEOUT_MS) {
    page = 0;
    Serial.println("[PAGE] static -> kembali ke halaman default");
  }

  handleSerial();
  updateMotor();

  // HC-SR04 bergantian tiap 100 ms
  if (now - tSensor >= 100) {
    tSensor = now;
    readSensor(turn ? 1 : 0);
    turn = !turn;
  }

  // Kecepatan encoder + status serial tiap 1 detik
  if (now - tStat >= 1000) {
    long dt = (long)(now - tStat);
    tStat = now;
    long tk = ticks;
    speedTps = (tk - lastStatTicks) * 1000L / dt;
    lastStatTicks = tk;
    char b1[12], b2[12];
    fmtCm(b1, sizeof(b1), dist[0]);
    fmtCm(b2, sizeof(b2), dist[1]);
    Serial.printf("[STAT] ticks=%ld v=%ld t/s motor=%s pwm=%d L1=%s L2=%s\n",
                  tk, speedTps, motorName(), pwmVal, b1, b2);
  }

  if (now - tRaw >= 2000) {
    tRaw = now;
    Serial.printf("[RAW] UP=%d DOWN=%d ESTOP=%d MODE=%d A=%d B=%d (1=HIGH)\n",
                  digitalRead(PIN_BTN_UP), digitalRead(PIN_BTN_DOWN), digitalRead(PIN_ESTOP),
                  digitalRead(PIN_BTN_MODE), digitalRead(PIN_ENC_A), digitalRead(PIN_ENC_B));
  }

  if (now - tOled >= 100) {
    tOled = now;
    drawOled();
  }
}
