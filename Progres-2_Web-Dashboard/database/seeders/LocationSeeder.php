<?php

namespace Database\Seeders;

use Illuminate\Database\Seeder;
use Illuminate\Support\Facades\DB;

class LocationSeeder extends Seeder
{
    public function run(): void
    {
        DB::table('locations')->insert([
            [
                'location_code'  => 'R1-L1-S1',
                'floor'          => 1,
                'target_encoder' => 0,      // Titik Home / Lantai 1
                'is_occupied'    => false,
                'created_at'     => now(),
                'updated_at'     => now(),
            ],
            [
                'location_code'  => 'R1-L2-S1',
                'floor'          => 2,
                'target_encoder' => 1250,   // Target pulsa Lantai 2 (sesuaikan kalibrasi Huri)
                'is_occupied'    => false,
                'created_at'     => now(),
                'updated_at'     => now(),
            ],
            [
                'location_code'  => 'R1-L3-S1',
                'floor'          => 3,
                'target_encoder' => 2500,   // Target pulsa Lantai 3 (sesuaikan kalibrasi Huri)
                'is_occupied'    => false,
                'created_at'     => now(),
                'updated_at'     => now(),
            ],
        ]);
    }
}