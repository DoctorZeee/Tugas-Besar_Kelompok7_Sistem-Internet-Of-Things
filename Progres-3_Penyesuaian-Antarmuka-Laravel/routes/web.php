<?php

use App\Http\Controllers\AuthController;
use App\Http\Controllers\InventoryController;
use App\Http\Controllers\LiftController;
use Illuminate\Support\Facades\Route;

Route::middleware('guest')->group(function () {
    Route::get('/login', [AuthController::class, 'showLogin'])->name('login');
    Route::post('/login', [AuthController::class, 'login'])->middleware('throttle:5,1');
});

Route::middleware('auth')->group(function () {
    Route::post('/logout', [AuthController::class, 'logout'])->name('logout');

    Route::get('/', fn () => view('dashboard'))->name('dashboard');

    // Endpoint dashboard: session + CSRF + login wajib (sebelumnya terbuka tanpa autentikasi).
    Route::prefix('lift')->middleware('throttle:60,1')->group(function () {
        Route::get('/status', [LiftController::class, 'status']);
        Route::post('/command', [LiftController::class, 'sendCommand']);
        Route::post('/ack-error', [LiftController::class, 'ackError'])->middleware('can:lift-admin');
    });

    Route::prefix('inventory')->middleware('throttle:60,1')->group(function () {
        Route::get('/', [InventoryController::class, 'index']);
        Route::post('/put-away', [InventoryController::class, 'putAway']);
        Route::post('/picking', [InventoryController::class, 'pickingFifo']);
    });
});
