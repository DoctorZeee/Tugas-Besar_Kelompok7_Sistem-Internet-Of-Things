<?php

use Illuminate\Database\Migrations\Migration;
use Illuminate\Database\Schema\Blueprint;
use Illuminate\Support\Facades\Schema;

return new class extends Migration
{
    /**
     * Run the migrations.
     */
    public function up(): void
    {
        Schema::create('locations', function (Blueprint $table) {
            $table->id();
            $table->string('location_code')->unique(); // Contoh: R1-L1-S1, R1-L2-S1, R1-L3-S1
            $table->integer('floor');                  // Lantai 1, 2, atau 3
            $table->integer('target_encoder');         // Hitungan pulsa encoder target
            $table->boolean('is_occupied')->default(false);
            $table->timestamps();
        });
    }

    /**
     * Reverse the migrations.
     */
    public function down(): void
    {
        Schema::dropIfExists('locations');
    }
};
