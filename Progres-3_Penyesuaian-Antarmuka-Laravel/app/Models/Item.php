<?php

namespace App\Models;

use Illuminate\Database\Eloquent\Model;
use Illuminate\Database\Eloquent\Relations\BelongsTo;

class Item extends Model
{
    protected $fillable = ['item_code', 'item_name', 'quantity', 'location_id', 'min_stock'];

    protected function casts(): array
    {
        return ['quantity' => 'integer', 'min_stock' => 'integer'];
    }

    public function location(): BelongsTo
    {
        return $this->belongsTo(Location::class);
    }
}
