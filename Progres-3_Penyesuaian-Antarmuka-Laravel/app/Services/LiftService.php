<?php

namespace App\Services;

use App\Models\LiftCommand;
use App\Models\Location;
use Illuminate\Support\Facades\Cache;
use PhpMqtt\Client\Facades\MQTT;
use Throwable;

/**
 * Satu-satunya pintu Laravel -> ESP32. Semua perintah gerak memeriksa kesiapan lift
 * (berdasarkan telemetri terakhir) dan memakai kunci atomik agar tidak ada dua perintah bersamaan.
 */
class LiftService
{
    public const LOCK_KEY = 'lift_status';
    public const ACTIVE_KEY = 'active_command';
    public const TELEMETRY_KEY = 'lift_telemetry';

    public function telemetry(): ?array
    {
        return Cache::get(self::TELEMETRY_KEY);
    }

    public function isLocked(): bool
    {
        return Cache::get(self::LOCK_KEY) === 'LIFT_MOVING';
    }

    /** Lempar LiftUnavailableException bila lift tidak siap menerima perintah gerak. */
    public function assertReady(): void
    {
        $t = $this->telemetry();

        if (! $t) {
            throw new LiftUnavailableException('Belum ada telemetri dari ESP32. Pastikan perangkat menyala dan tersambung ke broker.', 503);
        }

        $age = now()->timestamp - (int) ($t['received_at'] ?? 0);
        if ($age > config('warehouse.telemetry_max_age') || ($t['state'] ?? '') === 'OFFLINE') {
            throw new LiftUnavailableException('ESP32 offline (tidak ada telemetri terbaru).', 503);
        }

        $state = $t['state'] ?? 'UNKNOWN';

        if ($state === 'ESTOP') {
            throw new LiftUnavailableException('E-STOP aktif. Lepas E-STOP di perangkat terlebih dahulu.', 409);
        }
        if ($state === 'ERROR') {
            throw new LiftUnavailableException('Lift dalam status ERROR' . (($t['fault'] ?? '') ? " ({$t['fault']})" : '') . '. Kirim ACK Error lalu kalibrasi ulang di perangkat (MODE x3).', 409);
        }
        if (! ($t['ready_to_move'] ?? false)) {
            throw new LiftUnavailableException('Posisi lift belum terreferensi / travel belum dikalibrasi. Lakukan kalibrasi MODE x3 di perangkat.', 409);
        }
        if (! in_array($state, ['IDLE', 'ARRIVED'], true)) {
            throw new LiftUnavailableException("Lift sedang {$state}. Tunggu hingga IDLE.", 409);
        }
        if ($this->isLocked()) {
            throw new LiftUnavailableException('Lift sedang bergerak. Tunggu hingga status IDLE.', 409);
        }
    }

    /** Kirim lift ke lantai milik $location dan catat perintahnya. */
    public function dispatchMove(Location $location, string $type, ?string $itemCode = null): LiftCommand
    {
        $this->assertReady();

        // Cache::add atomik: hanya satu pemanggil yang berhasil memegang kunci.
        if (! Cache::add(self::LOCK_KEY, 'LIFT_MOVING', now()->addSeconds(config('warehouse.lock_seconds')))) {
            throw new LiftUnavailableException('Lift sedang bergerak. Tunggu hingga status IDLE.', 409);
        }

        try {
            $command = LiftCommand::create([
                'type'         => $type,
                'item_code'    => $itemCode,
                'target_floor' => $location->floor,
                'status'       => 'moving',
            ]);

            Cache::put(self::ACTIVE_KEY, [
                'id'          => $command->id,
                'floor'       => (int) $location->floor,
                'at'          => now()->timestamp,
                'seen_moving' => false,
            ], now()->addSeconds(config('warehouse.lock_seconds')));

            // Firmware menerima teks "1" / "2" di topik command.
            $this->publish(config('warehouse.topics.command'), (string) $location->floor);
        } catch (Throwable $e) {
            $this->release();
            report($e);
            throw new LiftUnavailableException('Gagal mengirim perintah ke broker MQTT.', 502);
        }

        return $command;
    }

    public function stop(): void
    {
        $this->publish(config('warehouse.topics.command'), 'STOP');
        $this->failActive('Dihentikan operator (STOP)');
        $this->release();
    }

    /** ACK_ERROR hanya menghapus status fault di firmware; posisi tetap harus dikalibrasi ulang manual. */
    public function ackError(): void
    {
        $this->publish(config('warehouse.topics.admin_command'), 'ACK_ERROR');
        $this->failActive('ACK_ERROR dikirim operator');
        $this->release();
    }

    public function release(): void
    {
        Cache::forget(self::LOCK_KEY);
        Cache::forget(self::ACTIVE_KEY);
    }

    public function failActive(string $note): void
    {
        $active = Cache::get(self::ACTIVE_KEY);
        if ($active) {
            LiftCommand::where('id', $active['id'])->where('status', 'moving')
                ->update(['status' => 'failed', 'note' => mb_substr($note, 0, 250)]);
        }
    }

    private function publish(string $topic, string $payload): void
    {
        // QoS 0, tanpa retain: firmware mengabaikan pesan retained dan hanya subscribe QoS 0.
        MQTT::publish($topic, $payload, 0, false);
    }
}
