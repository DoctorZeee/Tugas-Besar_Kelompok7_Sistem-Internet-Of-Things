<?php

namespace Database\Seeders;

use App\Models\Location;
use Illuminate\Database\Seeder;

class LocationSeeder extends Seeder
{
    public function run(): void
    {
        // Firmware hanya mengenal lantai 1 dan 2 (perintah "1" / "2"). Jumlah tick sebenarnya
        // ditentukan kalibrasi dua titik di ESP32 (NVS), bukan oleh kolom target_encoder.
        Location::updateOrCreate(['location_code' => 'R1-L1-S1'], ['floor' => 1, 'target_encoder' => 0, 'is_occupied' => false]);
        Location::updateOrCreate(['location_code' => 'R1-L2-S1'], ['floor' => 2, 'target_encoder' => null, 'is_occupied' => false]);
    }
}
