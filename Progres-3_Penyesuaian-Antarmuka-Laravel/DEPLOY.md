# Deploy & Operasional — Smart Warehouse (Laravel)

## 1. Pertama kali
```bash
composer install --no-dev --optimize-autoloader
cp .env.example .env          # isi DB, MQTT_*, REVERB_*, ADMIN_*, OPERATOR_* (password kuat, bukan contoh)
php artisan key:generate
php artisan migrate:fresh --seed   # migrasi diubah (stock_movements, lift_commands, anomaly_logs, users.role): wajib fresh sekali
npm ci && npm run build
```
Production: `APP_ENV=production`, `APP_DEBUG=false`, `APP_URL=https://...`, `REVERB_SCHEME=https`, `REVERB_ALLOWED_ORIGINS=domain-dashboard-anda`, `SESSION_SECURE_COOKIE=true`. Samakan `VITE_REVERB_*` dengan endpoint Reverb yang dapat dijangkau browser, lalu jalankan ulang `npm run build` setelah mengubah nilainya.
Lalu `php artisan config:cache route:cache view:cache`.

Pastikan `MQTT_HOST`, `MQTT_AUTH_USERNAME`, dan `MQTT_AUTH_PASSWORD` memakai akun broker Laravel tersendiri. Aktifkan validasi sertifikat TLS (`MQTT_TLS_VERIFY_PEER=true` dan `MQTT_TLS_VERIFY_PEER_NAME=true`); isi `MQTT_TLS_CA_FILE` hanya bila CA broker tidak tersedia di trust store sistem. Akun `ADMIN_*` dan `OPERATOR_*` dibuat saat `db:seed`; password tidak disediakan secara default.

## 2. Dua proses yang harus selalu hidup (pakai Supervisor/systemd)
- `php artisan mqtt:listen`  — menerima telemetri ESP32, menutup status perintah, broadcast ke dashboard
- `php artisan reverb:start` — WebSocket untuk dashboard
(Tanpa `mqtt:listen`, dashboard tidak update dan lift dianggap offline setelah 15 detik.)

## 3. Urutan operasi harian
1. Nyalakan ESP32 → **kalibrasi MODE x3 di perangkat** (posisi selalu tidak terreferensi setelah boot).
2. Dashboard menampilkan `SIAP GERAK`. Baru put-away / picking / manual bisa dipakai.
3. Fault: cek fisik → `ACK Error` di dashboard → kalibrasi ulang di perangkat.

## 4. Akun broker HiveMQ
- `esp32`  : read `.../lift/command`, `.../lift/admin/command`; write `.../lift/telemetry`, `.../lift/admin/result`, `.../lift/log`
- `laravel`: write `.../lift/command`, `.../lift/admin/command`; read `.../lift/telemetry`, `.../lift/admin/result`

## 5. Checklist sebelum dipakai
- [ ] Password WiFi dan MQTT yang sempat ter-push sudah diganti di HiveMQ/router
- [ ] `APP_DEBUG=false`, HTTPS aktif, `SESSION_SECURE_COOKIE=true`
- [ ] `mqtt:listen` dan `reverb:start` berjalan di Supervisor (autorestart)
- [ ] Akun broker `laravel` dan `esp32` terpisah dengan ACL seperti di atas
- [ ] `php artisan test` hijau; uji end-to-end: put-away, picking, STOP, E-STOP, cabut WiFi ESP32
- [ ] Backup database terjadwal
