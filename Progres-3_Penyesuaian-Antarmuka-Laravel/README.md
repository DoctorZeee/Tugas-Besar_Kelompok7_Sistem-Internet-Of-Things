# Smart Warehouse — Web Dashboard (Laravel)

Dashboard dan backend untuk lift gudang 2 lantai (firmware ESP32 `SmartWarehouse`, folder `Progress-4_SmartWarehouse`).
Laravel dan ESP32 berkomunikasi lewat **MQTT (TLS)**, bukan REST.

```
Browser ──HTTPS──> Laravel ──publish──> HiveMQ ──> ESP32
   ^                  ^                    |
   └── Reverb (WS) ───┴── mqtt:listen <────┘  (telemetri JSON)
```

## Fitur
- Login (role **admin** / **operator**), semua endpoint di balik session + CSRF.
- Put-away (rak kosong terbawah) dan picking **FIFO** (batch terlama). Data stok dan perintah lift disimpan dalam satu transaksi; bila lift tidak siap, stok tidak berubah.
- Telemetri real-time (lantai, tick, sensor rak, state, fault) via Reverb, dengan polling fallback dan indikator telemetri kedaluwarsa.
- Riwayat perintah lift (`lift_commands`), pergerakan stok (`stock_movements`), log anomali (`anomaly_logs`).
- Antarmuka responsif dengan tema gelap/terang yang tersimpan di browser. Asset realtime (Echo/Pusher) hanya dimuat pada halaman dashboard, bukan halaman login.

## Hak akses
| Aksi | Operator | Admin |
|---|:-:|:-:|
| Put-away / Picking | ✔ | ✔ |
| STOP | ✔ | ✔ |
| Kontrol manual lantai | ✘ | ✔ |
| ACK Error | ✘ | ✔ |

## Kontrak Laravel ↔ firmware
| Topik (sama dengan `Config.h`) | Arah | Isi |
|---|---|---|
| `kelompok7/smartwarehouse/lift/command` | Laravel → ESP32 | `1`, `2`, `STOP` |
| `kelompok7/smartwarehouse/lift/admin/command` | Laravel → ESP32 | `ACK_ERROR` |
| `kelompok7/smartwarehouse/lift/telemetry` | ESP32 → Laravel | JSON state/floor/ticks/ready_to_move/... |
| `kelompok7/smartwarehouse/lift/admin/result` | ESP32 → Laravel | hasil perintah admin |

Aturan penting:
- Lift hanya menerima perintah bila telemetri terbaru `ready_to_move=true` dan state `IDLE`/`ARRIVED`.
- Posisi ESP32 **selalu belum terreferensi setelah boot**: kalibrasi MODE x3 di perangkat wajib sebelum dashboard bisa menggerakkan lift.
- `HOME`/`RESET` dinonaktifkan di firmware; tidak ada tombol Homing di web.
- `ACK_ERROR` hanya menghapus status fault. Kalibrasi ulang tetap manual di perangkat.
- Perintah dianggap `done` bila lift berhenti (IDLE/ARRIVED) di lantai tujuan; `failed` bila berhenti di tempat lain, E-STOP, ERROR, atau offline. Artinya "lift tiba", **bukan** konfirmasi barang sudah diletakkan/diambil.

## Menjalankan
Lihat `DEPLOY.md` (instalasi, supervisor untuk `mqtt:listen` dan `reverb:start`, akun broker). Konfigurasi MQTT, Reverb, topik, dan akun awal tersedia sebagai template di `.env.example`.

```bash
php artisan test      # test memakai MQTT palsu, tidak butuh broker
```


## Antarmuka dan build asset

Antarmuka menggunakan CSS lokal dengan design token untuk mode terang/gelap, tanpa meminta font eksternal. Tema mengikuti preferensi sistem pada kunjungan pertama dan pilihan pengguna disimpan di `localStorage`. Untuk membangun asset produksi:

```bash
npm ci
npm run build
```

JavaScript dashboard berada di `resources/js/dashboard.js`; `resources/js/app.js` hanya menangani tema global. Jika Reverb belum dikonfigurasi, halaman dashboard memakai polling status berkala sebagai fallback.

Lihat `UI_GUIDE.md` untuk ringkasan perubahan tampilan dan catatan pengujian.
