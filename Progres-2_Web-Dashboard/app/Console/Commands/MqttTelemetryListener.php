<?php

namespace App\Console\Commands;

use App\Events\LiftTelemetryUpdated;
use App\Models\AnomalyLog;
use App\Models\LiftCommand;
use Illuminate\Console\Command;
use Illuminate\Support\Facades\Cache;
use PhpMqtt\Client\Facades\MQTT;

class MqttTelemetryListener extends Command
{
    protected $signature = 'mqtt:listen';
    protected $description = 'Mendengarkan data telemetri ESP32 dari MQTT dan meneruskan ke WebSocket';

    public function handle(): void
    {
        $this->info('Menghubungkan ke MQTT Broker untuk mendengarkan topic/lift/telemetry...');

        $mqtt = MQTT::connection();

        $mqtt->subscribe('topic/lift/telemetry', function (string $topic, string $message) {
            $data = json_decode($message, true);

            if (!is_array($data)) {
                $this->warn("Payload JSON tidak valid: {$message}");
                return;
            }

            $status     = $data['status'] ?? 'UNKNOWN';
            $encoder    = (int) ($data['encoder'] ?? 0);
            $currentAmp = (float) ($data['current_amp'] ?? 0.0);
            $commandId  = Cache::get('active_command_id');

            // 1. Jika lift selesai bergerak (IDLE) atau mencapai target
            if ($status === 'IDLE' || $status === 'ARRIVED') {
                Cache::forget('lift_status');
                if ($commandId) {
                    LiftCommand::where('id', $commandId)->update(['status' => 'completed']);
                    Cache::forget('active_command_id');
                }
            }

            // 2. Deteksi Anomali Keselamatan (Overcurrent ACS712 atau Timeout 8 detik)
            if (in_array($status, ['OVERCURRENT', 'TIMEOUT', 'EMERGENCY_STOP'])) {
                Cache::forget('lift_status');

                if ($commandId) {
                    LiftCommand::where('id', $commandId)->update(['status' => 'failed']);
                    Cache::forget('active_command_id');
                }

                AnomalyLog::create([
                    'type'        => strtolower($status),
                    'description' => "Anomali terdeteksi pada encoder {$encoder} dengan arus {$currentAmp}A",
                    'current_amp' => $currentAmp,
                ]);

                $this->error("[ANOMALI] {$status} - Arus: {$currentAmp}A | Encoder: {$encoder}");
            }

            // 3. Siarkan data ke Dashboard Digital Twin via Laravel Reverb
            broadcast(new LiftTelemetryUpdated([
                'encoder'     => $encoder,
                'current_amp' => $currentAmp,
                'status'      => $status,
                'is_locked'   => Cache::get('lift_status') === 'LIFT_MOVING',
                'timestamp'   => now()->format('H:i:s'),
            ]));

            $this->line("[{$data['status']}] Encoder: {$encoder} | Arus: {$currentAmp}A");
        }, 0);

        $mqtt->loop(true);
    }
}