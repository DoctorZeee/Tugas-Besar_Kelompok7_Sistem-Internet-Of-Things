<?php

namespace Tests\Feature;

use App\Models\Item;
use App\Models\Location;
use App\Models\User;
use App\Services\LiftService;
use Illuminate\Foundation\Testing\RefreshDatabase;
use Illuminate\Support\Facades\Cache;
use PhpMqtt\Client\Facades\MQTT;
use Tests\TestCase;

class ExampleTest extends TestCase
{
    use RefreshDatabase;

    private function readyTelemetry(array $override = []): void
    {
        Cache::put(LiftService::TELEMETRY_KEY, array_merge([
            'state' => 'IDLE', 'floor' => 1, 'ticks' => 0, 'ready_to_move' => true,
            'position_valid' => true, 'fault' => '', 'info' => '', 'received_at' => now()->timestamp,
        ], $override), now()->addMinutes(5));
    }

    private function seedLocations(): void
    {
        Location::create(['location_code' => 'R1-L1-S1', 'floor' => 1, 'is_occupied' => false]);
        Location::create(['location_code' => 'R1-L2-S1', 'floor' => 2, 'is_occupied' => false]);
    }

    public function test_guest_is_redirected_to_login(): void
    {
        $this->get('/')->assertRedirect('/login');
        $this->postJson('/lift/command', ['action' => 'STOP'])->assertStatus(401);
    }

    public function test_put_away_sends_floor_command_to_correct_topic(): void
    {
        $this->seedLocations();
        $this->readyTelemetry();

        MQTT::shouldReceive('publish')->once()
            ->with('kelompok7/smartwarehouse/lift/command', '1', 0, false);

        $this->actingAs(User::factory()->create())
            ->postJson('/inventory/put-away', ['item_code' => 'A1', 'item_name' => 'Kardus', 'quantity' => 10])
            ->assertOk()->assertJsonPath('target_rack', 'R1-L1-S1');

        $this->assertDatabaseHas('items', ['item_code' => 'A1']);
        $this->assertDatabaseHas('lift_commands', ['target_floor' => 1, 'status' => 'moving']);
    }

    public function test_put_away_is_rolled_back_when_lift_not_ready(): void
    {
        $this->seedLocations();
        $this->readyTelemetry(['state' => 'ESTOP']);

        MQTT::shouldReceive('publish')->never();

        $this->actingAs(User::factory()->create())
            ->postJson('/inventory/put-away', ['item_code' => 'A1', 'item_name' => 'Kardus', 'quantity' => 10])
            ->assertStatus(409);

        $this->assertSame(0, Item::count());
        $this->assertFalse(Location::where('is_occupied', true)->exists());
    }

    public function test_command_rejected_without_telemetry(): void
    {
        $this->seedLocations();
        MQTT::shouldReceive('publish')->never();

        $this->actingAs(User::factory()->create())
            ->postJson('/lift/command', ['action' => 'MANUAL', 'target_rack' => 'R1-L2-S1'])
            ->assertStatus(503);
    }

    public function test_operator_cannot_manual_move_or_ack_error(): void
    {
        $this->seedLocations();
        $this->readyTelemetry();
        MQTT::shouldReceive('publish')->never();

        $operator = User::factory()->create();

        $this->actingAs($operator)
            ->postJson('/lift/command', ['action' => 'MANUAL', 'target_rack' => 'R1-L2-S1'])
            ->assertForbidden();
        $this->actingAs($operator)->postJson('/lift/ack-error')->assertForbidden();
    }

    public function test_operator_can_stop_and_admin_can_ack_error(): void
    {
        MQTT::shouldReceive('publish')->once()->with('kelompok7/smartwarehouse/lift/command', 'STOP', 0, false);
        MQTT::shouldReceive('publish')->once()->with('kelompok7/smartwarehouse/lift/admin/command', 'ACK_ERROR', 0, false);

        $this->actingAs(User::factory()->create())
            ->postJson('/lift/command', ['action' => 'STOP'])->assertOk();

        $admin = User::factory()->create();
        $admin->forceFill(['role' => 'admin'])->save();
        $this->actingAs($admin)->postJson('/lift/ack-error')->assertOk();
    }
}
