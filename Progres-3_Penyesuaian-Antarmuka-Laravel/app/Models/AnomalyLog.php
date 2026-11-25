<?php

namespace App\Models;

use Illuminate\Database\Eloquent\Model;

class AnomalyLog extends Model
{
    protected $fillable = ['event_type', 'current_amp', 'encoder_position', 'description'];
}
