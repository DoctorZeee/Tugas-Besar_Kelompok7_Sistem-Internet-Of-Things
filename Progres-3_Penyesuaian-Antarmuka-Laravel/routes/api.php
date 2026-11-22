<?php

use App\Http\Controllers\InventoryController;
use App\Http\Controllers\LiftController;
use Illuminate\Support\Facades\Route;

Route::prefix('lift')->group(function () {
    Route::post('/command', [LiftController::class, 'sendCommand']);
    Route::post('/unlock', [LiftController::class, 'unlockEmergency']);
});

Route::prefix('inventory')->group(function () {
    Route::get('/', [InventoryController::class, 'index']);
    Route::post('/put-away', [InventoryController::class, 'putAway']);
    Route::post('/picking', [InventoryController::class, 'pickingFifo']);
});