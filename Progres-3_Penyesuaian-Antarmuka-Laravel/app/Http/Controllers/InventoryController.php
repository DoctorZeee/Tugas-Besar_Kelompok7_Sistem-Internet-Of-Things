<?php

namespace App\Http\Controllers;

use App\Models\Item;
use App\Models\Location;
use Illuminate\Http\Request;

class InventoryController extends Controller
{
    /**
     * Menampilkan daftar stok beserta status Reorder Point (ROP).
     */
    public function index()
    {
        $items = Item::with('location')
            ->orderBy('created_at', 'asc') // Urutan FIFO (terlama di atas)
            ->get()
            ->map(function ($item) {
                $item->is_low_stock = $item->quantity <= $item->min_stock;
                return $item;
            });

        return response()->json($items);
    }

    /**
     * Barang Masuk (Put Away): Cari rak kosong otomatis dan simpan data barang.
     */
    public function putAway(Request $request)
    {
        $request->validate([
            'item_code' => 'required|string|unique:items,item_code',
            'item_name' => 'required|string',
            'quantity'  => 'required|integer|min:1',
            'min_stock' => 'nullable|integer|min:1',
        ]);

        // Cari slot rak kosong paling bawah terlebih dahulu
        $emptyLocation = Location::where('is_occupied', false)
            ->orderBy('floor', 'asc')
            ->first();

        if (!$emptyLocation) {
            return response()->json([
                'status'  => 'full',
                'message' => 'Semua rak gudang penuh!'
            ], 422);
        }

        $item = Item::create([
            'item_code'   => $request->item_code,
            'item_name'   => $request->item_name,
            'quantity'    => $request->quantity,
            'location_id' => $emptyLocation->id,
            'min_stock'   => $request->min_stock ?? 5,
        ]);

        $emptyLocation->update(['is_occupied' => true]);

        return response()->json([
            'status'      => 'success',
            'item'        => $item,
            'target_rack' => $emptyLocation->location_code,
        ]);
    }

    /**
     * Barang Keluar (Picking FIFO): Ambil batch paling lama masuk (created_at paling awal).
     */
    public function pickingFifo(Request $request)
    {
        $request->validate([
            'item_name' => 'required|string',
            'quantity'  => 'required|integer|min:1',
        ]);

        // Ambil item dengan nama sama yang paling pertama masuk (FIFO)
        $item = Item::with('location')
            ->where('item_name', $request->item_name)
            ->where('quantity', '>=', $request->quantity)
            ->orderBy('created_at', 'asc')
            ->first();

        if (!$item) {
            return response()->json([
                'status'  => 'insufficient',
                'message' => 'Stok tidak mencukupi dalam satu batch.'
            ], 422);
        }

        $targetRack = $item->location?->location_code;
        $item->decrement('quantity', $request->quantity);

        // Jika stok batch ini habis (0), kosongkan kembali status raknya
        if ($item->quantity === 0 && $item->location) {
            $item->location->update(['is_occupied' => false]);
            $item->update(['location_id' => null]);
        }

        return response()->json([
            'status'       => 'success',
            'picked_item'  => $item,
            'target_rack'  => $targetRack,
            'rop_warning'  => $item->quantity <= $item->min_stock,
        ]);
    }
}