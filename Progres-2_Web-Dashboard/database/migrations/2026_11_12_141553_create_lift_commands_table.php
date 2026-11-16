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
        $table->integer('target_floor'); // 1, 2, atau 3
        $table->enum('status', ['pending', 'moving', 'done', 'failed'])->default('pending');
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
