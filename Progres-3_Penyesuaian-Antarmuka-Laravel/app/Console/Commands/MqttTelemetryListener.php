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

            // Key sesuai JSON dari ESP32: state, floor, ticks, target, dist1, dist2, estop, homed, info
            $state     = $data['state'] ?? 'UNKNOWN';
            $floor     = (int) ($data['floor'] ?? 1);
            $ticks     = (int) ($data['ticks'] ?? 0);
            $target    = (int) ($data['target'] ?? 0);
            $dist1     = (float) ($data['dist1'] ?? -1);
            $dist2     = (float) ($data['dist2'] ?? -1);
            $estop     = (bool) ($data['estop'] ?? false);
            $homed     = (bool) ($data['homed'] ?? false);
            $info      = (string) ($data['info'] ?? '');
            $commandId = Cache::get('active_command_id');

            // 1. Jika lift selesai bergerak (IDLE / ARRIVED)
            if (in_array($state, ['IDLE', 'ARRIVED'])) {
                Cache::forget('lift_status');
                if ($commandId) {
                    LiftCommand::where('id', $commandId)->update(['status' => 'completed']);
                    Cache::forget('active_command_id');
                }
            }

            // 2. Jika lift sedang bergerak / homing / manual
            if (in_array($state, ['MOVING', 'HOMING', 'MANUAL'])) {
                Cache::put('lift_status', 'LIFT_MOVING', now()->addSeconds(10));
            }

            // 3. Deteksi Anomali Keselamatan (ESTOP atau ERROR seperti MOTION_TIMEOUT / TOP_SOFT_LIMIT)
            if (in_array($state, ['ESTOP', 'ERROR'])) {
                Cache::forget('lift_status');

                if ($commandId) {
                    LiftCommand::where('id', $commandId)->update(['status' => 'failed']);
                    Cache::forget('active_command_id');
                }

                // Hindari spam log berulang untuk info error yang sama
                $cacheErrorKey = "last_anomaly_{$state}_{$info}";
                if (!Cache::has($cacheErrorKey)) {
                    AnomalyLog::create([
                        'type'        => strtolower($state),
                        'description' => "State {$state} pada ticks {$ticks}" . ($info ? " (Info: {$info})" : ''),
                        'current_amp' => 0,
                    ]);
                    Cache::put($cacheErrorKey, true, now()->addSeconds(10));
                }

                $this->error("[ANOMALI] {$state} | Ticks: {$ticks} | Info: {$info}");
            }

            // 4. Siarkan data ke Dashboard Digital Twin via Laravel Reverb
            broadcast(new LiftTelemetryUpdated([
                'state'     => $state,
                'floor'     => $floor,
                'ticks'     => $ticks,
                'target'    => $target,
                'dist1'     => $dist1,
                'dist2'     => $dist2,
                'estop'     => $estop,
                'homed'     => $homed,
                'info'      => $info,
                'is_locked' => Cache::get('lift_status') === 'LIFT_MOVING',
                'timestamp' => now()->format('H:i:s'),
            ]));

            $this->line("[{$state}] Lantai: {$floor} | Ticks: {$ticks}/{$target} | D1: {$dist1}cm | D2: {$dist2}cm | Info: {$info}");
        }, 0);

        $mqtt->loop(true);
    }
}