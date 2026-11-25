<?php

namespace App\Providers;

use App\Models\User;
use Illuminate\Support\Facades\Gate;
use Illuminate\Support\Facades\URL;
use Illuminate\Support\ServiceProvider;

class AppServiceProvider extends ServiceProvider
{
    public function register(): void
    {
        //
    }

    public function boot(): void
    {
        Gate::define('lift-admin', fn (User $user) => $user->isAdmin());

        if ($this->app->environment('production')) {
            URL::forceScheme('https');
        }
    }
}
