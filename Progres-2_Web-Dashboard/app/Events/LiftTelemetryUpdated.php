<?php

namespace App\Events;

use Illuminate\Broadcasting\Channel;
use Illuminate\Broadcasting\InteractsWithSockets;
use Illuminate\Contracts\Broadcasting\ShouldBroadcastNow;
use Illuminate\Foundation\Events\Dispatchable;
use Illuminate\Queue\SerializesModels;

class LiftTelemetryUpdated implements ShouldBroadcastNow
{
    use Dispatchable, InteractsWithSockets, SerializesModels;

    public array $telemetry;

    public function __construct(array $telemetry)
    {
        $this->telemetry =$telemetry;
    }

    public function broadcastOn(): array
    {
        return [
            new Channel('warehouse-telemetry'),
        ];
    }

    public function broadcastAs(): string
    {
        return 'telemetry.updated';
    }
}