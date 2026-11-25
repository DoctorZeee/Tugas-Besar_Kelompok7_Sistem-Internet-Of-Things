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
    Schema::create('lift_commands', function (Blueprint $table) {
        $table->id();
        $table->enum('type', ['put_away', 'picking', 'manual']);
        $table->string('item_code')->nullable();
        $table->integer('target_floor'); // 1 atau 2 (firmware: gudang 2 lantai)
        $table->enum('status', ['pending', 'moving', 'done', 'failed'])->default('pending');
        $table->string('note')->nullable(); // alasan gagal / info dari ESP32
        $table->timestamps();
    });
}
    /**
     * Reverse the migrations.
     */
    public function down(): void
    {
        Schema::dropIfExists('lift_commands');
    }
};
