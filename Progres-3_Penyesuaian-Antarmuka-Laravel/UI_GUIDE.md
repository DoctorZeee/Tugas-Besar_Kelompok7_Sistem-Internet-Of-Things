# UI Guide — Smart Warehouse

## Perubahan antarmuka

- Layout dashboard disusun ulang untuk desktop, tablet, dan ponsel. Kartu lift, alur barang masuk/keluar, dan inventaris menyesuaikan lebar layar. Tabel inventaris menggunakan horizontal scroll pada layar sempit agar kolom tidak saling bertumpuk.
- Tema terang dan gelap menggunakan CSS custom properties. Tema pertama mengikuti preferensi OS; pilihan manual disimpan dengan key `smart-warehouse-theme`.
- Halaman login memakai sistem visual yang sama, tetapi tidak memuat koneksi Reverb. `laravel-echo` dan `pusher-js` hanya diimpor oleh entry dashboard.
- Font memakai system font stack; tidak ada permintaan font eksternal tambahan.
- Tampilan inventaris dibangun menggunakan DOM `textContent`, sehingga nilai barang dari API tidak dimasukkan sebagai HTML mentah.
- Indikator telemetri mendeteksi data snapshot yang kedaluwarsa. Bila Reverb belum tersedia/terputus, dashboard memakai polling 10 detik; saat koneksi realtime aktif, polling dihentikan.

## Hasil audit UI (26 Nov 2026)

- Kontras teks diperbaiki agar memenuhi WCAG AA (4,5:1) pada mode terang: `--text-muted`, `--text-faint`, `--info`, `--warn`, `--danger`, `--ok`, dan `--accent` disesuaikan.
- Ukuran font minimum dinaikkan (sebelumnya banyak teks 9–10 px) menjadi 11–11,5 px untuk label dan 12,5 px ke atas untuk isi.
- Ikon tema memakai SVG (bulan/matahari), bukan karakter `◐`/`☼` yang tampilannya bergantung pada font perangkat.
- `theme-color` mengikuti tema aktif; bila `localStorage` diblokir, tema tetap mengikuti preferensi OS.
- Favicon SVG inline (`partials/favicon.blade.php`); `public/favicon.ico` bawaan berukuran 0 byte.
- Tabel inventaris pada layar ≤560 px tampil sebagai kartu bertumpuk (label per baris), bukan scroll horizontal.
- Kartu digital twin mengisi tinggi yang tersisa di desktop; tombol lantai satu kolom pada layar ≤380 px.
- `role="group"` ditambahkan pada grup status dan metrik agar `aria-label` terbaca pembaca layar.

- Tombol STOP, ACK Error, dan kontrol lantai manual dinonaktifkan selama request berjalan sehingga klik ganda tidak mengirim perintah dua kali. STOP tetap tanpa konfirmasi demi keselamatan.
- Konfirmasi ACK Error memakai elemen `<dialog>` yang mengikuti tema (fallback ke `window.confirm` bila `<dialog>` tidak didukung).
- `welcome.blade.php` dihapus karena tidak punya route.

## File utama

| File | Tanggung jawab |
|---|---|
| `resources/views/dashboard.blade.php` | Struktur dashboard dan URL endpoint dari Laravel |
| `resources/views/auth/login.blade.php` | Halaman login responsif |
| `resources/views/partials/theme-init.blade.php` | Menentukan tema sebelum render untuk mengurangi flash tema |
| `resources/css/dashboard.css` | Design token, komponen, tema, dan breakpoint |
| `resources/js/app.js` | Toggle dan persistensi tema |
| `resources/js/dashboard.js` | Fetch API, telemetry, kontrol, inventaris, dan fallback polling |
| `resources/js/echo.js` | Inisialisasi koneksi Reverb secara opsional |

## Build dan pemeriksaan

```bash
npm ci
npm run build
php artisan test
```

Build Vite dilakukan dari folder proyek Laravel ini. Untuk menjalankan dashboard dengan telemetry realtime, proses `mqtt:listen` dan `reverb:start` tetap perlu aktif sebagaimana dijelaskan di `DEPLOY.md`.

## Batasan verifikasi

Perubahan ini berfokus pada Laravel/UI. Firmware ESP32 pada `Progress-4_SmartWarehouse/SmartWarehouse` tidak diubah. Kesesuaian nama topik MQTT, format command lantai (`1`/`2`), command admin `ACK_ERROR`, serta field telemetry diperiksa secara statis; perilaku motor, sensor, E-STOP, dan kalibrasi tetap harus diuji secara fisik pada perangkat.
