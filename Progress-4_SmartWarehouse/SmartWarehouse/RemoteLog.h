#ifndef REMOTE_LOG_H
#define REMOTE_LOG_H

// =====================================================================
// Serial monitor jarak jauh (lewat MQTT).
//
// Semua Serial.xxx di sketch tetap keluar ke USB seperti biasa. Ketika admin mengirim LOG_ON,
// keluaran yang sama juga ditampung di ring buffer; netTask mengirimnya per baris ke topik log.
// Saat LOG_OFF (default) tidak ada yang ditampung, jadi tidak ada beban tambahan.
// =====================================================================

#include <Arduino.h>
#include <string.h>
#include <freertos/FreeRTOS.h>

static const size_t RLOG_BUF = 4096;
static char rlogBuf[RLOG_BUF];
static uint32_t rlogTotal = 0;                 // total byte yang pernah ditulis
static uint32_t rlogRead = 0;                  // posisi baca netTask
static volatile bool rlogEnabled = false;
static portMUX_TYPE rlogMux = portMUX_INITIALIZER_UNLOCKED;

static void rlogPut(const uint8_t* b, size_t n) {
  if (!rlogEnabled) return;
  portENTER_CRITICAL(&rlogMux);
  uint32_t t = rlogTotal;
  for (size_t i = 0; i < n; i++) {
    rlogBuf[t % RLOG_BUF] = (char)b[i];
    t++;
  }
  rlogTotal = t;
  portEXIT_CRITICAL(&rlogMux);
}

// Buang antrean lama (dipakai saat LOG_ON dan saat MQTT tersambung ulang).
static void rlogSkipToEnd() {
  portENTER_CRITICAL(&rlogMux);
  rlogRead = rlogTotal;
  portEXIT_CRITICAL(&rlogMux);
}

// Ambil satu baris lengkap (tanpa '\r' / '\n'). false bila belum ada baris utuh.
static bool rlogPopLine(char* out, size_t maxLen) {
  bool got = false;
  size_t n = 0;
  portENTER_CRITICAL(&rlogMux);
  const uint32_t total = rlogTotal;
  const uint32_t oldest = (total > RLOG_BUF) ? (total - RLOG_BUF) : 0;
  if (rlogRead < oldest) rlogRead = oldest;     // buffer tertimpa: loncat ke yang tersisa
  uint32_t pos = rlogRead;
  while (pos < total && n < maxLen - 1) {
    const char ch = rlogBuf[pos % RLOG_BUF];
    pos++;
    if (ch == '\n') { got = true; break; }
    if (ch != '\r') out[n++] = ch;
  }
  if (!got && n >= maxLen - 1) got = true;      // baris terlalu panjang: kirim potongannya
  if (got) rlogRead = pos;
  portEXIT_CRITICAL(&rlogMux);
  if (got) out[n] = '\0';
  return got;
}

// Pembungkus Serial: tulis ke UART asli DAN ke ring buffer (bila LOG_ON).
class LogSerialClass : public Print {
 public:
  void begin(unsigned long baud) { Serial.begin(baud); }
  int available() { return Serial.available(); }
  int read() { return Serial.read(); }
  size_t write(uint8_t c) override {
    Serial.write(c);
    rlogPut(&c, 1);
    return 1;
  }
  size_t write(const uint8_t* buf, size_t n) override {
    Serial.write(buf, n);
    rlogPut(buf, n);
    return n;
  }
  using Print::write;
};
static LogSerialClass LogSerial;

#endif
