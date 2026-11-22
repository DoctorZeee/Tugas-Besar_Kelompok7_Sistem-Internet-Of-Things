<?php

namespace App\Http\Controllers;

use App\Models\LiftCommand;
use App\Models\Location;
use Illuminate\Http\Request;
use Illuminate\Support\Facades\Cache;
use PhpMqtt\Client\Facades\MQTT;

class LiftController extends Controller
{
    public function sendCommand(Request $request)
    {
        $request->validate([
            'action'      => 'required|in:PUT_AWAY,PICKING,MANUAL,HOMING,STOP',
            'target_rack' => 'nullable|string',
            'item_code'   => 'nullable|string',
        ]);

        // 1. Jika perintah STOP darurat dari web, langsung kirim tanpa menunggu lock
        if ($request->action === 'STOP') {
            MQTT::publish('topic/lift/command', 'STOP');
            Cache::forget('lift_status');
            return response()->json([
                'status'  => 'stopped',
                'message' => 'Perintah STOP dikirim ke ESP32.'
            ]);
        }

        // 2. Cek Locking State: Tolak perintah baru jika lift sedang bergerak
        if (Cache::get('lift_status') === 'LIFT_MOVING') {
            return response()->json([
                'status'  => 'rejected',
                'message' => 'Lift sedang bergerak. Tunggu hingga status IDLE.'
            ], 409);
        }

        // 3. Jika perintah HOMING / RESET
        if ($request->action === 'HOMING') {
            Cache::put('lift_status', 'LIFT_MOVING', now()->addSeconds(16));
            MQTT::publish('topic/lift/command', 'HOME');

            return response()->json([
                'status'  => 'success',
                'message' => 'Perintah HOME dikirim ke ESP32.'
            ]);
        }

        // 4. Perintah gerak ke lantai target (PUT_AWAY, PICKING, MANUAL)
        $request->validate([
            'target_rack' => 'required|string|exists:locations,location_code',
        ]);

        $location = Location::where('location_code', $request->target_rack)->firstOrFail();

        $command = LiftCommand::create([
            'type'         => strtolower($request->action),
            'item_code'    => $request->item_code,
            'target_floor' => $location->floor,
            'status'       => 'moving',
        ]);

        // Kunci status maksimal 10 detik (lebih aman dari MOVE_TIMEOUT_MS = 8000ms di ESP32)
        Cache::put('lift_status', 'LIFT_MOVING', now()->addSeconds(10));
        Cache::put('active_command_id', $command->id, now()->addSeconds(10));

        // Payload string sesuai kode Arduino: "1" atau "2"
        $mqttPayload = (string) $location->floor;
        MQTT::publish('topic/lift/command', $mqttPayload);

        return response()->json([
            'status'  => 'success',
            'message' => "Perintah gerak ke Lantai {$location->floor} ({$location->location_code}) dikirim.",
            'data'    => $command,
        ]);
    }

    public function unlockEmergency()
    {
        Cache::forget('lift_status');
        Cache::forget('active_command_id');

        // Kirim perintah HOME ke ESP32 agar keluar dari STATE_ERROR setelah E-STOP dilepas
        MQTT::publish('topic/lift/command', 'HOME');

        return response()->json([
            'status'  => 'unlocked',
            'message' => 'Locking state di-reset dan perintah HOME dikirim ke ESP32.'
        ]);
    }
}