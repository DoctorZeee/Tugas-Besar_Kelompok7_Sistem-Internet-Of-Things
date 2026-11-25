<?php

namespace Database\Seeders;

use App\Models\User;
use Illuminate\Database\Seeder;

class DatabaseSeeder extends Seeder
{
    public function run(): void
    {
        $this->call(LocationSeeder::class);

        // Akun dari .env. Tidak ada password bawaan.
        $this->makeUser('ADMIN', 'admin', 'Admin');
        $this->makeUser('OPERATOR', 'operator', 'Operator');
    }

    private function makeUser(string $prefix, string $role, string $defaultName): void
    {
        $email = env("{$prefix}_EMAIL");
        $password = env("{$prefix}_PASSWORD");

        if (! $email || ! $password) {
            $this->command?->warn("{$prefix}_EMAIL / {$prefix}_PASSWORD belum diisi di .env: akun {$role} tidak dibuat.");

            return;
        }

        $user = User::updateOrCreate(
            ['email' => $email],
            ['name' => env("{$prefix}_NAME", $defaultName), 'password' => $password],
        );
        $user->forceFill(['role' => $role])->save(); // role sengaja tidak mass-assignable
    }
}
