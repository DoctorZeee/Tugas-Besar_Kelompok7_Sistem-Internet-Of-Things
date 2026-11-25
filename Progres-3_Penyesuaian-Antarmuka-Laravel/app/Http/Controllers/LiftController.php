<?php

namespace App\Http\Controllers;

use App\Models\Location;
use App\Services\LiftService;
use App\Services\LiftUnavailableException;
use Illuminate\Http\JsonResponse;
use Illuminate\Http\Request;
use Illuminate\Support\Facades\Gate;

class LiftController extends Controller
{
    public function __construct(private LiftService $lift) {}

    /** Snapshot telemetri terakhir agar dashboard tidak kosong saat halaman dibuka. */
    public function status(): JsonResponse
    {
        $t = $this->lift->telemetry();
        if ($t) {
            $t['is_locked'] = $this->lift->isLocked();
        }

        return response()->json($t ?? ['state' => 'OFFLINE', 'info' => 'Belum ada telemetri']);
    }

    /**
     * Perintah manual dari dashboard. PUT_AWAY / PICKING tidak lewat sini:
     * keduanya dikirim otomatis oleh InventoryController setelah data stok tersimpan.
     */
    public function sendCommand(Request $request): JsonResponse
    {
        $data = $request->validate([
            'action'      => 'required|in:MANUAL,STOP',
            'target_rack' => 'required_if:action,MANUAL|nullable|string|exists:locations,location_code',
        ]);

        if ($data['action'] === 'STOP') {
            $this->lift->stop();

            return response()->json(['status' => 'stopped', 'message' => 'Perintah STOP dikirim ke ESP32.']);
        }

        // Gerak manual (tanpa data stok) hanya untuk admin. STOP boleh untuk semua pengguna.
        Gate::authorize('lift-admin');

        $location = Location::where('location_code', $data['target_rack'])->firstOrFail();

        try {
            $command = $this->lift->dispatchMove($location, 'manual');
        } catch (LiftUnavailableException $e) {
            return response()->json(['status' => 'rejected', 'message' => $e->getMessage()], $e->status);
        }

        return response()->json([
            'status'  => 'success',
            'message' => "Perintah gerak ke Lantai {$location->floor} ({$location->location_code}) dikirim.",
            'data'    => $command,
        ]);
    }

    /** Kirim ACK_ERROR (jalur admin MQTT). Setelah ini kalibrasi MODE x3 tetap wajib di perangkat. */
    public function ackError(): JsonResponse
    {
        $this->lift->ackError();

        return response()->json([
            'status'  => 'ack_sent',
            'message' => 'ACK_ERROR dikirim. Posisi tetap belum valid: lakukan kalibrasi MODE x3 di perangkat setelah mengecek lift secara fisik.',
        ]);
    }
}
