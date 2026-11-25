<?php

namespace App\Http\Controllers;

use App\Models\Item;
use App\Models\Location;
use App\Models\StockMovement;
use App\Services\LiftService;
use App\Services\LiftUnavailableException;
use Illuminate\Http\JsonResponse;
use Illuminate\Http\Request;
use Illuminate\Support\Facades\DB;

class InventoryController extends Controller
{
    public function __construct(private LiftService $lift) {}

    /** Daftar stok berurutan FIFO beserta status Reorder Point (ROP). */
    public function index(): JsonResponse
    {
        $items = Item::with('location')
            ->orderBy('created_at', 'asc')
            ->orderBy('id', 'asc')
            ->get()
            ->map(function ($item) {
                $item->is_low_stock = $item->quantity <= $item->min_stock;

                return $item;
            });

        return response()->json($items);
    }

    /**
     * Barang masuk: pilih rak kosong, simpan stok, lalu kirim lift ke lantainya.
     * Semuanya satu transaksi: bila lift tidak siap / gagal dikirim, stok TIDAK tersimpan.
     */
    public function putAway(Request $request): JsonResponse
    {
        $data = $request->validate([
            'item_code' => 'required|string|max:50|unique:items,item_code',
            'item_name' => 'required|string|max:100',
            'quantity'  => 'required|integer|min:1',
            'min_stock' => 'nullable|integer|min:1',
        ]);

        try {
            $result = DB::transaction(function () use ($data) {
                $location = Location::where('is_occupied', false)
                    ->orderBy('floor', 'asc')
                    ->lockForUpdate()
                    ->first();

                if (! $location) {
                    throw new LiftUnavailableException('Semua rak gudang penuh!', 422);
                }

                $item = Item::create([
                    'item_code'   => $data['item_code'],
                    'item_name'   => $data['item_name'],
                    'quantity'    => $data['quantity'],
                    'location_id' => $location->id,
                    'min_stock'   => $data['min_stock'] ?? 5,
                ]);

                $location->update(['is_occupied' => true]);

                StockMovement::create([
                    'item_id'     => $item->id,
                    'location_id' => $location->id,
                    'type'        => 'in',
                    'quantity'    => $item->quantity,
                ]);

                $command = $this->lift->dispatchMove($location, 'put_away', $item->item_code);

                return compact('item', 'location', 'command');
            });
        } catch (LiftUnavailableException $e) {
            return response()->json(['status' => $e->status === 422 ? 'full' : 'rejected', 'message' => $e->getMessage()], $e->status);
        }

        return response()->json([
            'status'      => 'success',
            'item'        => $result['item'],
            'target_rack' => $result['location']->location_code,
            'message'     => "Barang disimpan di {$result['location']->location_code}. Lift menuju Lantai {$result['location']->floor}.",
        ]);
    }

    /** Barang keluar (FIFO): ambil batch paling lama, lalu kirim lift ke rak asalnya. */
    public function pickingFifo(Request $request): JsonResponse
    {
        $data = $request->validate([
            'item_name' => 'required|string|max:100',
            'quantity'  => 'required|integer|min:1',
        ]);

        try {
            $result = DB::transaction(function () use ($data) {
                $item = Item::with('location')
                    ->where('item_name', $data['item_name'])
                    ->where('quantity', '>=', $data['quantity'])
                    ->whereNotNull('location_id')
                    ->orderBy('created_at', 'asc')
                    ->orderBy('id', 'asc')
                    ->lockForUpdate()
                    ->first();

                if (! $item) {
                    throw new LiftUnavailableException('Stok tidak mencukupi dalam satu batch.', 422);
                }

                $location = $item->location;

                $item->decrement('quantity', $data['quantity']);
                $item->refresh();

                StockMovement::create([
                    'item_id'     => $item->id,
                    'location_id' => $location->id,
                    'type'        => 'out',
                    'quantity'    => $data['quantity'],
                ]);

                if ($item->quantity === 0) {
                    $location->update(['is_occupied' => false]);
                    $item->update(['location_id' => null]);
                }

                // Dikirim paling akhir: bila lift tidak siap / broker gagal, seluruh transaksi dibatalkan;
                // bila query DB gagal, lift tidak pernah digerakkan.
                $command = $this->lift->dispatchMove($location, 'picking', $item->item_code);

                return compact('item', 'location', 'command');
            });
        } catch (LiftUnavailableException $e) {
            return response()->json(['status' => $e->status === 422 ? 'insufficient' : 'rejected', 'message' => $e->getMessage()], $e->status);
        }

        $item = $result['item'];

        return response()->json([
            'status'      => 'success',
            'picked_item' => $item,
            'target_rack' => $result['location']->location_code,
            'rop_warning' => $item->quantity <= $item->min_stock,
            'message'     => "Ambil dari {$result['location']->location_code}. Lift menuju Lantai {$result['location']->floor}.",
        ]);
    }
}
