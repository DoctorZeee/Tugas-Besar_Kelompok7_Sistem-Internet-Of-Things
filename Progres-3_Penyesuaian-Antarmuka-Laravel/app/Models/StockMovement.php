<?php

namespace App\Models;

use Illuminate\Database\Eloquent\Model;
use Illuminate\Database\Eloquent\Relations\BelongsTo;

class StockMovement extends Model
{
    protected $fillable = ['item_id', 'location_id', 'type', 'quantity'];

    public function item(): BelongsTo
    {
        return $this->belongsTo(Item::class);
    }
}
