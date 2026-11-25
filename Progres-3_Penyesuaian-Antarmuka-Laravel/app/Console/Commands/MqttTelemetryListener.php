<?php

namespace App\Console\Commands;

use App\Events\LiftTelemetryUpdated;
use App\Models\AnomalyLog;
use App\Models\LiftCommand;
use App\Services\LiftService;
use Illuminate\Console\Command;
use Illuminate\Support\Facades\Cache;
use Illuminate\Support\Facades\Log;
use PhpMqtt\Client\Facades\MQTT;
use Throwable;

class MqttTelemetryListener extends Command
{
    protected $signature = 'mqtt:listen';
    protected $description = 'Mendengarkan telemetri ESP32 dari MQTT, memperbarui status perintah, dan meneruskan ke dashboard (Reverb)';

    public function handle(): int
    {
        $this->info('Listener MQTT dimulai. Topik: ' . config('warehouse.topics.telemetry'));

        // Jalankan di bawah supervisor. Bila koneksi putus, sambung ulang sendiri.
        while (true) {
            try {
                $mqtt = MQTT::connection();

                $mqtt->subscribe(config('warehouse.topics.telemetry'), function (string $topic, string $message) {
                    $this->onTelemetry($message);
                }, 0);

                $mqtt->subscribe(config('warehouse.topics.admin_result'), function (string $topic, string $message) {
                    Log::info('[lift admin/result] ' . $message);
                    $this->line("[ADMIN] {$message}");
                }, 0);

                $this->info('Tersambung ke broker.');
                $mqtt->loop(true);
            } catch (Throwable $e) {
                report($e);
                $this->error('Koneksi MQTT terputus: ' . $e->getMessage() . ' (coba lagi 5 detik)');
                try {
                    MQTT::disconnect();
                } catch (Throwable) {
                    // abaikan
                }
                sleep(5);
            }
        }
    }

    private function onTelemetry(string $message): void
    {
        $data = json_decode($message, true);
        if (! is_array($data)) {
            $this->warn("Payload JSON tidak valid: {$message}");

            return;
        }

        $t = [
            'state'           => (string) ($data['state'] ?? 'UNKNOWN'),
            'floor'           => (int) ($data['floor'] ?? 0),
            'ticks'           => (int) ($data['ticks'] ?? 0),
            'target'          => (int) ($data['target'] ?? 0),
            'dist1'           => (float) ($data['dist1'] ?? -1),
            'dist2'           => (float) ($data['dist2'] ?? -1),
            'rack1_status'    => (string) ($data['rack1_status'] ?? 'UNKNOWN'),
            'rack2_status'    => (string) ($data['rack2_status'] ?? 'UNKNOWN'),
            'rack1_near_full' => (bool) ($data['rack1_near_full'] ?? false),
            'rack2_near_full' => (bool) ($data['rack2_near_full'] ?? false),
            'estop'           => (bool) ($data['estop'] ?? false),
            'homed'           => (bool) ($data['homed'] ?? false),
            'position_valid'  => (bool) ($data['position_valid'] ?? false),
            'ready_to_move'   => (bool) ($data['ready_to_move'] ?? false),
            'floor2_ticks'    => (int) ($data['floor2_ticks'] ?? 0),
            'fault'           => (string) ($data['fault'] ?? ''),
            'info'            => (string) ($data['info'] ?? ''),
            'received_at'     => now()->timestamp,
        ];

        Cache::put(LiftService::TELEMETRY_KEY, $t, now()->addMinutes(10));

        $this->updateCommandState($t);

        $t['is_locked'] = Cache::get(LiftService::LOCK_KEY) === 'LIFT_MOVING';
        $t['timestamp'] = now()->format('H:i:s');
        unset($t['received_at']);

        try {
            broadcast(new LiftTelemetryUpdated($t));
        } catch (Throwable $e) {
            // Reverb mati tidak boleh menghentikan pencatatan status perintah.
            report($e);
        }

        $this->line("[{$t['state']}] Lantai: {$t['floor']} | Ticks: {$t['ticks']}/{$t['target']} | Info: {$t['info']}");
    }

    private function updateCommandState(array $t): void
    {
        $state  = $t['state'];
        $active = Cache::get(LiftService::ACTIVE_KEY);

        if (in_array($state, ['MOVING', 'MANUAL'], true)) {
            if ($active) {
                $active['seen_moving'] = true;
                Cache::put(LiftService::ACTIVE_KEY, $active, now()->addSeconds(config('warehouse.lock_seconds')));
            }
            Cache::put(LiftService::LOCK_KEY, 'LIFT_MOVING', now()->addSeconds(config('warehouse.lock_seconds')));

            return;
        }

        if (in_array($state, ['IDLE', 'ARRIVED'], true)) {
            if (! $active) {
                Cache::forget(LiftService::LOCK_KEY);

                return;
            }

            $age = now()->timestamp - (int) $active['at'];
            $atTarget = $t['floor'] === (int) $active['floor'];

            if ($atTarget && ($active['seen_moving'] || $age >= config('warehouse.settle_seconds'))) {
                LiftCommand::where('id', $active['id'])->update(['status' => 'done']);
                $this->releaseLock();
            } elseif (! $atTarget && ($active['seen_moving'] || $age >= config('warehouse.reject_seconds'))) {
                // Lift berhenti di luar target (STOP / ditolak firmware): jangan dianggap sukses.
                LiftCommand::where('id', $active['id'])->update([
                    'status' => 'failed',
                    'note'   => mb_substr('Lift berhenti di lantai ' . $t['floor'] . ($t['info'] ? " ({$t['info']})" : ''), 0, 250),
                ]);
                $this->releaseLock();
            }

            return;
        }

        // ESTOP / ERROR / OFFLINE / POSITION_UNREFERENCED / BOOT: perintah aktif pasti gagal.
        if ($active) {
            LiftCommand::where('id', $active['id'])->update([
                'status' => 'failed',
                'note'   => mb_substr("State {$state}" . ($t['fault'] ? " ({$t['fault']})" : ''), 0, 250),
            ]);
        }
        $this->releaseLock();

        if (in_array($state, ['ESTOP', 'ERROR'], true)) {
            $this->logAnomaly($t);
        }
    }

    private function releaseLock(): void
    {
        Cache::forget(LiftService::LOCK_KEY);
        Cache::forget(LiftService::ACTIVE_KEY);
    }

    private function logAnomaly(array $t): void
    {
        $key = "last_anomaly_{$t['state']}_{$t['fault']}";
        if (Cache::has($key)) {
            return; // hindari spam log untuk fault yang sama
        }
        Cache::put($key, true, now()->addSeconds(30));

        AnomalyLog::create([
            'event_type'       => strtolower($t['state']),
            'encoder_position' => $t['ticks'],
            'description'      => "State {$t['state']} pada ticks {$t['ticks']}"
                . ($t['fault'] ? " (Fault: {$t['fault']})" : '')
                . ($t['info'] ? " (Info: {$t['info']})" : ''),
        ]);

        $this->error("[ANOMALI] {$t['state']} | Ticks: {$t['ticks']} | Fault: {$t['fault']}");
    }
}
