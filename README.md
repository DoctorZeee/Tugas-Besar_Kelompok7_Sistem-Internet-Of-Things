# Smart Warehouse: Lift PID, IoT, dan Laravel

Tugas Besar 1 - Sistem Internet of Things

## Identitas Kelompok

| Keterangan | Isi |
| --- | --- |
| Nama kelompok | Kelompok 7 |
| Mata kuliah | Sistem Internet of Things |

### Anggota

| No | Nama | NIM | Bidang kerja |
| --- | --- | --- | --- |
| 1 | Dimas Rafif Zaidan | H1H024043 | IoT penuh: mekanik dan perakitan hardware, firmware ESP32, PID, state machine, klien HTTP |
| 2 | Huriyatun Nur Anajmi | H1H024035 | Backend: Laravel, database, REST API (command, status, done), validasi, FIFO, autentikasi token |
| 3 | Lula Khaisha Delavia | H1H024064 | Frontend: antarmuka web, dashboard rak, form put-away/picking, kontrol manual, panel tuning PID, laporan SCM, dokumentasi |

## Deskripsi Singkat

Prototipe miniatur gudang pintar skala meja: lift satu sumbu dengan kendali posisi PID, rak pasif tiga lantai, dan website Laravel yang mencatat stok serta lokasi barang. Pengguna membuat perintah put-away atau order picking lewat website, ESP32 mengambil perintah melalui REST API, menggerakkan lift ke lantai tujuan dengan umpan balik encoder, lalu melaporkan hasilnya ke Laravel.

Terdapat dua varian:

- **Semi** (target utama): lift otomatis dengan PID, barang diletakkan dan diambil manual di platform.
- **Full** (jika waktu dan anggaran cukup): ditambah carriage dorong-tarik di platform untuk memasukkan dan menarik kotak dari rak.

## Komponen Utama

- ESP32 DevKit
- Motor DC gearbox + encoder (JGB37-520, 12 V) dan driver TB6612FNG
- Limit switch, sensor IR, buck converter LM2596, tombol E-stop + fuse
- Varian Full: motor carriage, rel carriage, servo SG90, limit switch carriage
- Laravel + database untuk REST API dan dashboard

## Endpoint API

| Endpoint | Fungsi |
| --- | --- |
| `GET /api/lift/command` | ESP32 mengecek perintah baru |
| `POST /api/lift/status` | ESP32 mengirim posisi dan status |
| `POST /api/lift/done` | ESP32 melaporkan perintah selesai |

## Dokumen

- Progress Tugas Besar 1 (PDF): gambaran umum, daftar komponen, diagram blok, flowchart, mekanisme kerja, dan pembagian kerja.

## Status

Tahap perancangan sistem selesai; pengerjaan mengikuti urutan pada dokumen progress.
