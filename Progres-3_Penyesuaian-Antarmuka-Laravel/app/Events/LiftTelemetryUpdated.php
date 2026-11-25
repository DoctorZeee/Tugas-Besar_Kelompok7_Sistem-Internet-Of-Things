<?php

namespace App\Events;

use Illuminate\Broadcasting\InteractsWithSockets;
use Illuminate\Broadcasting\PrivateChannel;
use Illuminate\Contracts\Broadcasting\ShouldBroadcastNow;
use Illuminate\Foundation\Events\Dispatchable;
use Illuminate\Queue\SerializesModels;

class LiftTelemetryUpdated implements ShouldBroadcastNow
{
    use Dispatchable, InteractsWithSockets, SerializesModels;

    public function __construct(public array $telemetry) {}

    /** Channel privat: hanya pengguna yang sudah login yang boleh mendengarkan (lihat routes/channels.php). */
    public function broadcastOn(): array
    {
        return [new PrivateChannel('warehouse-telemetry')];
    }

    public function broadcastAs(): string
    {
        return 'telemetry.updated';
    }
}
