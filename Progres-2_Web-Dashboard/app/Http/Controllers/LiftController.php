<?php

namespace App\Http\Controllers;

use App\Models\LiftCommand;
use App\Models\Location;
use Illuminate\Http\Request;
use Illuminate\Support\Facades\Cache;
use PhpMqtt\Client\Facades\MQTT;

class LiftController extends Controller
{
    /**
     * Mengirim perintah gerakan ke ESP32 melalui MQTT dengan proteksi Locking State.
     */
    public function sendCommand(Request $request)
    {
        $request->validate([
            'action'      => 'required|in:PUT_AWAY,PICKING,MANUAL,HOMING',
            'target_rack' => 'required|string|exists:locations,location_code',
            'item_code'   => 'nullable|string',
        ]);

        // 1. Cek Locking State: Tolak perintah baru jika lift sedang bergerak
        if (Cache::get('lift_status') === 'LIFT_MOVING') {
            return response()->json([
                'status'  => 'rejected',
                'message' => 'Lift sedang bergerak (LIFT_MOVING). Tunggu hingga status IDLE.'
            ], 409);
        }

        $location = Location::where('location_code', $request->target_rack)->firstOrFail();

        // 2. Simpan riwayat perintah ke database
        $command = LiftCommand::create([
            'type'         => strtolower($request->action === 'HOMING' ? 'manual' : $request->action),
            'item_code'    => $request->item_code,
            'target_floor' => $location->floor,
            'status'       => 'moving',
        ]);

        // 3. Kunci status sistem selama maksimal 15 detik (pengaman jika ESP32 putus koneksi)
        Cache::put('lift_status', 'LIFT_MOVING', now()->addSeconds(15));
        Cache::put('active_command_id', $command->id, now()->addSeconds(15));

        // 4. Susun payload sesuai Data Contract MQTT (topic/lift/command)
        $payload = json_encode([
            'command_id'     => $command->id,
            'action'         => $request->action,
            'target_rack'    => $location->location_code,
            'target_floor'   => $location->floor,
            'target_encoder' => $location->target_encoder,
        ]);

        // 5. Publish ke MQTT Broker
        MQTT::publish('topic/lift/command', $payload);

        return response()->json([
            'status'  => 'success',
            'message' => 'Perintah dikirim ke ESP32.',
            'data'    => $command,
        ]);
    }

    /**
     * Reset manual lock jika terjadi kondisi darurat / emergency stop.
     */
    public function unlockEmergency()
    {
        Cache::forget('lift_status');
        Cache::forget('active_command_id');

        return response()->json([
            'status'  => 'unlocked',
            'message' => 'Locking state berhasil di-reset ke IDLE.'
        ]);
    }
}