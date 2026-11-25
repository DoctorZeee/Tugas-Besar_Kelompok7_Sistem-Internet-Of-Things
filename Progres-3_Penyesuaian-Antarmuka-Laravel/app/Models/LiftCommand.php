<?php

namespace App\Models;

use Illuminate\Database\Eloquent\Model;

class LiftCommand extends Model
{
    protected $fillable = ['type', 'item_code', 'target_floor', 'status', 'note'];
}
