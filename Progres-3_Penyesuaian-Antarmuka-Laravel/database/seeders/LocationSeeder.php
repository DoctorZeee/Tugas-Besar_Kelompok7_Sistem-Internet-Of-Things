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
                'target_encoder' => 0,     // FLOOR_1_TICKS di Config.h
                'is_occupied'    => false,
                'created_at'     => now(),
                'updated_at'     => now(),
            ],
            [
                'location_code'  => 'R1-L2-S1',
                'floor'          => 2,
                'target_encoder' => 750,   // FLOOR_2_TICKS di Config.h
                'is_occupied'    => false,
                'created_at'     => now(),
                'updated_at'     => now(),
            ],
        ]);
    }
}