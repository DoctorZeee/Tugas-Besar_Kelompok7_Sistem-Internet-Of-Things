<?php

/*
 * Konfigurasi integrasi Laravel <-> firmware ESP32 (SmartWarehouse.ino).
 * Nama topik HARUS sama dengan Config.h di firmware.
 */
return [
    'topics' => [
        'command'       => env('WAREHOUSE_TOPIC_COMMAND', 'kelompok7/smartwarehouse/lift/command'),
        'telemetry'     => env('WAREHOUSE_TOPIC_TELEMETRY', 'kelompok7/smartwarehouse/lift/telemetry'),
        'admin_command' => env('WAREHOUSE_TOPIC_ADMIN_COMMAND', 'kelompok7/smartwarehouse/lift/admin/command'),
        'admin_result'  => env('WAREHOUSE_TOPIC_ADMIN_RESULT', 'kelompok7/smartwarehouse/lift/admin/result'),
    ],

    // Kunci "lift sibuk". Harus > MOVE_TIMEOUT_MS firmware (12 detik) supaya kunci tidak lepas
    // sebelum firmware benar-benar menyerah.
    'lock_seconds' => (int) env('WAREHOUSE_LOCK_SECONDS', 16),

    // Telemetri idle dikirim tiap 5 detik (TELEMETRY_IDLE_MS). Lebih lama dari ini = dianggap offline.
    'telemetry_max_age' => (int) env('WAREHOUSE_TELEMETRY_MAX_AGE', 15),

    // Setelah perintah dikirim, abaikan telemetri IDLE "basi" selama beberapa detik
    // sebelum menyimpulkan lift sudah tiba / perintah ditolak firmware.
    'settle_seconds' => (int) env('WAREHOUSE_SETTLE_SECONDS', 3),
    'reject_seconds' => (int) env('WAREHOUSE_REJECT_SECONDS', 8),
];
