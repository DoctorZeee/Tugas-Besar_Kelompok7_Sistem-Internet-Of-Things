<!DOCTYPE html>
<html lang="id">
<head>
    <meta charset="UTF-8">
    <meta name="viewport" content="width=device-width, initial-scale=1.0">
    <title>Smart Warehouse — Digital Twin Dashboard</title>
    <script src="https://cdn.tailwindcss.com"></script>
    @vite(['resources/js/app.js'])
</head>
<body class="bg-slate-950 text-slate-100 font-sans antialiased">
    <div class="max-w-7xl mx-auto p-6 space-y-6">
        <!-- Header -->
        <header class="flex flex-wrap items-center justify-between gap-4 border-b border-slate-800 pb-4">
            <div>
                <h1 class="text-2xl font-bold tracking-tight text-emerald-400">Smart Warehouse AS/RS</h1>
                <p class="text-sm text-slate-400">Digital Twin Telemetry, Locking Control & FIFO Inventory</p>
            </div>
            <div class="flex flex-wrap items-center gap-2">
                <span id="badge-homed" class="px-3 py-1 rounded-full text-xs font-semibold bg-slate-800 text-slate-300 border border-slate-700">
                    HOMED: UNKNOWN
                </span>
                <span id="badge-status" class="px-3 py-1 rounded-full text-xs font-semibold bg-emerald-500/20 text-emerald-300 border border-emerald-500/40">
                    STATE: IDLE
                </span>
                <button onclick="sendLiftCommand('STOP')" class="px-3 py-1.5 rounded-lg text-xs font-semibold bg-amber-600 hover:bg-amber-500 text-white transition">
                    STOP
                </button>
                <button onclick="unlockEmergency()" class="px-3 py-1.5 rounded-lg text-xs font-semibold bg-rose-600 hover:bg-rose-500 text-white transition">
                    Reset Lock & Home
                </button>
            </div>
        </header>

        <!-- Alert Info dari ESP32 -->
        <div id="info-banner" class="hidden px-4 py-2.5 rounded-lg bg-rose-500/15 border border-rose-500/40 text-rose-300 text-xs font-medium"></div>

        <!-- Grid Utama -->
        <div class="grid grid-cols-1 lg:grid-cols-3 gap-6">
            <!-- Kolom 1: Visualisasi Digital Twin Lift -->
            <div class="bg-slate-900 border border-slate-800 rounded-xl p-5 flex flex-col justify-between">
                <div>
                    <h2 class="text-sm font-semibold uppercase tracking-wider text-slate-400 mb-4">Digital Twin Lift (Encoder)</h2>
                    
                    <!-- Shaft Lift Visual (2 Lantai sesuai Config.h: 0 - 750 ticks) -->
                    <div class="relative h-64 bg-slate-950 border border-slate-800 rounded-lg p-4 flex flex-col justify-between overflow-hidden">
                        <div class="flex justify-between items-center border-b border-dashed border-slate-800 pb-2 text-xs text-slate-400">
                            <span>Lantai 2 (R1-L2-S1)</span>
                            <span>750 ticks</span>
                        </div>
                        <div class="flex justify-between items-center text-xs text-slate-400">
                            <span>Lantai 1 / Home (R1-L1-S1)</span>
                            <span>0 ticks</span>
                        </div>

                        <!-- Kereta Lift Bergerak -->
                        <div id="lift-carriage" style="bottom: 8px;" class="absolute left-1/2 -translate-x-1/2 w-40 h-11 bg-emerald-500/20 border-2 border-emerald-400 rounded-md flex flex-col items-center justify-center text-xs font-bold text-emerald-300 transition-all duration-300 shadow-lg shadow-emerald-500/10">
                            <span id="carriage-floor">LANTAI 1</span>
                        </div>
                    </div>
                </div>

                <!-- Metrik Telemetri -->
                <div class="grid grid-cols-3 gap-2.5 mt-4">
                    <div class="bg-slate-950 border border-slate-800 rounded-lg p-3">
                        <p class="text-[11px] text-slate-400">Encoder</p>
                        <p id="val-ticks" class="text-lg font-bold text-white mt-1">0 <span class="text-[11px] font-normal text-slate-500">ticks</span></p>
                    </div>
                    <div class="bg-slate-950 border border-slate-800 rounded-lg p-3">
                        <p class="text-[11px] text-slate-400">Sensor Lt. 1</p>
                        <p id="val-dist1" class="text-lg font-bold text-sky-400 mt-1">- <span class="text-[11px] font-normal text-slate-500">cm</span></p>
                    </div>
                    <div class="bg-slate-950 border border-slate-800 rounded-lg p-3">
                        <p class="text-[11px] text-slate-400">Sensor Lt. 2</p>
                        <p id="val-dist2" class="text-lg font-bold text-sky-400 mt-1">- <span class="text-[11px] font-normal text-slate-500">cm</span></p>
                    </div>
                </div>
            </div>

            <!-- Kolom 2: Kontrol Gerak & Form Put Away / Picking -->
            <div class="bg-slate-900 border border-slate-800 rounded-xl p-5 space-y-5">
                <div>
                    <h2 class="text-sm font-semibold uppercase tracking-wider text-slate-400 mb-3">Kontrol Manual Rak</h2>
                    <div class="grid grid-cols-3 gap-2">
                        <button onclick="sendLiftCommand('MANUAL', 'R1-L1-S1')" class="py-2.5 px-3 bg-slate-800 hover:bg-emerald-600 rounded-lg text-xs font-semibold transition">Lantai 1</button>
                        <button onclick="sendLiftCommand('MANUAL', 'R1-L2-S1')" class="py-2.5 px-3 bg-slate-800 hover:bg-emerald-600 rounded-lg text-xs font-semibold transition">Lantai 2</button>
                        <button onclick="sendLiftCommand('HOMING')" class="py-2.5 px-3 bg-slate-800 hover:bg-sky-600 rounded-lg text-xs font-semibold transition">Homing (0)</button>
                    </div>
                </div>

                <hr class="border-slate-800">

                <!-- Form Put Away (Barang Masuk) -->
                <form onsubmit="handlePutAway(event)" class="space-y-3">
                    <h3 class="text-sm font-semibold text-emerald-400">Barang Masuk (Put Away)</h3>
                    <div class="grid grid-cols-2 gap-2">
                        <input id="in-code" type="text" placeholder="Kode (mis. BRG-01)" required class="bg-slate-950 border border-slate-800 rounded-lg px-3 py-2 text-xs">
                        <input id="in-name" type="text" placeholder="Nama Barang" required class="bg-slate-950 border border-slate-800 rounded-lg px-3 py-2 text-xs">
                    </div>
                    <div class="grid grid-cols-2 gap-2">
                        <input id="in-qty" type="number" placeholder="Jumlah" min="1" required class="bg-slate-950 border border-slate-800 rounded-lg px-3 py-2 text-xs">
                        <input id="in-min" type="number" placeholder="Min Stok (ROP)" value="5" min="1" class="bg-slate-950 border border-slate-800 rounded-lg px-3 py-2 text-xs">
                    </div>
                    <button type="submit" class="w-full py-2 bg-emerald-600 hover:bg-emerald-500 rounded-lg text-xs font-semibold transition">
                        Simpan & Kirim Lift ke Rak Kosong
                    </button>
                </form>

                <hr class="border-slate-800">

                <!-- Form Picking (Barang Keluar FIFO) -->
                <form onsubmit="handlePicking(event)" class="space-y-3">
                    <h3 class="text-sm font-semibold text-sky-400">Barang Keluar (Picking FIFO)</h3>
                    <div class="grid grid-cols-2 gap-2">
                        <input id="out-name" type="text" placeholder="Nama Barang" required class="bg-slate-950 border border-slate-800 rounded-lg px-3 py-2 text-xs">
                        <input id="out-qty" type="number" placeholder="Jumlah Ambil" min="1" required class="bg-slate-950 border border-slate-800 rounded-lg px-3 py-2 text-xs">
                    </div>
                    <button type="submit" class="w-full py-2 bg-sky-600 hover:bg-sky-500 rounded-lg text-xs font-semibold transition">
                        Ambil Stok Terlama (FIFO)
                    </button>
                </form>
            </div>

            <!-- Kolom 3: Daftar Stok FIFO & Peringatan ROP -->
            <div class="bg-slate-900 border border-slate-800 rounded-xl p-5 flex flex-col">
                <div class="flex items-center justify-between mb-4">
                    <h2 class="text-sm font-semibold uppercase tracking-wider text-slate-400">Inventaris (Urutan FIFO)</h2>
                    <button onclick="loadInventory()" class="text-xs text-emerald-400 hover:underline">Refresh</button>
                </div>
                <div class="overflow-x-auto flex-1">
                    <table class="w-full text-left text-xs">
                        <thead class="text-slate-400 border-b border-slate-800">
                            <tr>
                                <th class="pb-2">Kode</th>
                                <th class="pb-2">Barang</th>
                                <th class="pb-2">Rak</th>
                                <th class="pb-2">Stok</th>
                                <th class="pb-2">Status</th>
                            </tr>
                        </thead>
                        <tbody id="inventory-body" class="divide-y divide-slate-800/60">
                            <tr><td colspan="5" class="py-4 text-center text-slate-500">Memuat data...</td></tr>
                        </tbody>
                    </table>
                </div>
            </div>
        </div>
    </div>

    <script>
        const MAX_ENCODER = 750; // Sesuai FLOOR_2_TICKS di Config.h

        function updateTelemetryUI(data) {
            document.getElementById('val-ticks').innerHTML = `${data.ticks} <span class="text-[11px] font-normal text-slate-500">ticks</span>`;
            document.getElementById('val-dist1').innerHTML = data.dist1 >= 0
                ? `${Number(data.dist1).toFixed(1)} <span class="text-[11px] font-normal text-slate-500">cm</span>`
                : `- <span class="text-[11px] font-normal text-slate-500">cm</span>`;
            document.getElementById('val-dist2').innerHTML = data.dist2 >= 0
                ? `${Number(data.dist2).toFixed(1)} <span class="text-[11px] font-normal text-slate-500">cm</span>`
                : `- <span class="text-[11px] font-normal text-slate-500">cm</span>`;

            document.getElementById('carriage-floor').textContent = `LANTAI ${data.floor}`;

            // Hitung posisi vertikal kereta lift (8px sampai 196px)
            const ratio = Math.min(Math.max(data.ticks / MAX_ENCODER, 0), 1);
            const bottomPx = 8 + (ratio * 188);
            document.getElementById('lift-carriage').style.bottom = `${bottomPx}px`;

            // Update Badge Homed
            const badgeHomed = document.getElementById('badge-homed');
            badgeHomed.textContent = data.homed ? 'HOMED: YES' : 'HOMED: NO';
            badgeHomed.className = data.homed
                ? 'px-3 py-1 rounded-full text-xs font-semibold bg-sky-500/20 text-sky-300 border border-sky-500/40'
                : 'px-3 py-1 rounded-full text-xs font-semibold bg-amber-500/20 text-amber-300 border border-amber-500/40';

            // Update Badge State
            const badge = document.getElementById('badge-status');
            badge.textContent = `STATE: ${data.state}`;
            if (['MOVING', 'HOMING', 'MANUAL'].includes(data.state) || data.is_locked) {
                badge.className = 'px-3 py-1 rounded-full text-xs font-semibold bg-amber-500/20 text-amber-300 border border-amber-500/40';
            } else if (['ESTOP', 'ERROR'].includes(data.state)) {
                badge.className = 'px-3 py-1 rounded-full text-xs font-semibold bg-rose-500/20 text-rose-300 border border-rose-500/40';
            } else {
                badge.className = 'px-3 py-1 rounded-full text-xs font-semibold bg-emerald-500/20 text-emerald-300 border border-emerald-500/40';
            }

            // Tampilkan pesan info dari ESP32 jika ada (misal: MOTION_TIMEOUT, ESTOP, BUSY)
            const infoBanner = document.getElementById('info-banner');
            if (data.info) {
                infoBanner.textContent = `Info Perangkat ESP32: ${data.info}`;
                infoBanner.classList.remove('hidden');
            } else {
                infoBanner.classList.add('hidden');
            }
        }

        async function sendLiftCommand(action, targetRack = null, itemCode = null) {
            const res = await fetch('/api/lift/command', {
                method: 'POST',
                headers: { 'Content-Type': 'application/json', 'Accept': 'application/json' },
                body: JSON.stringify({ action, target_rack: targetRack, item_code: itemCode })
            });
            const json = await res.json();
            if (!res.ok) alert(json.message || 'Perintah ditolak!');
        }

        async function unlockEmergency() {
            const res = await fetch('/api/lift/unlock', { method: 'POST', headers: { 'Accept': 'application/json' } });
            const json = await res.json();
            alert(json.message);
        }

        async function handlePutAway(e) {
            e.preventDefault();
            const payload = {
                item_code: document.getElementById('in-code').value,
                item_name: document.getElementById('in-name').value,
                quantity: parseInt(document.getElementById('in-qty').value),
                min_stock: parseInt(document.getElementById('in-min').value),
            };
            const res = await fetch('/api/inventory/put-away', {
                method: 'POST',
                headers: { 'Content-Type': 'application/json', 'Accept': 'application/json' },
                body: JSON.stringify(payload)
            });
            const json = await res.json();
            if (res.ok) {
                await sendLiftCommand('PUT_AWAY', json.target_rack, payload.item_code);
                e.target.reset();
                loadInventory();
            } else {
                alert(json.message || 'Gagal menyimpan barang');
            }
        }

        async function handlePicking(e) {
            e.preventDefault();
            const payload = {
                item_name: document.getElementById('out-name').value,
                quantity: parseInt(document.getElementById('out-qty').value),
            };
            const res = await fetch('/api/inventory/picking', {
                method: 'POST',
                headers: { 'Content-Type': 'application/json', 'Accept': 'application/json' },
                body: JSON.stringify(payload)
            });
            const json = await res.json();
            if (res.ok) {
                if (json.target_rack) await sendLiftCommand('PICKING', json.target_rack, json.picked_item.item_code);
                if (json.rop_warning) alert(`Peringatan ROP: Stok ${payload.item_name} mencapai batas minimum!`);
                e.target.reset();
                loadInventory();
            } else {
                alert(json.message || 'Stok tidak mencukupi');
            }
        }

        async function loadInventory() {
            const res = await fetch('/api/inventory');
            const items = await res.json();
            const tbody = document.getElementById('inventory-body');
            if (!items.length) {
                tbody.innerHTML = '<tr><td colspan="5" class="py-4 text-center text-slate-500">Belum ada barang di rak.</td></tr>';
                return;
            }
            tbody.innerHTML = items.map(item => `
                <tr>
                    <td class="py-2.5 font-mono text-slate-300">${item.item_code}</td>
                    <td class="py-2.5">${item.item_name}</td>
                    <td class="py-2.5 text-emerald-400">${item.location ? item.location.location_code : '-'}</td>
                    <td class="py-2.5 font-semibold">${item.quantity}</td>
                    <td class="py-2.5">
                        ${item.is_low_stock
                            ? '<span class="px-2 py-0.5 rounded bg-rose-500/20 text-rose-300 text-[10px]">ROP ALERT</span>'
                            : '<span class="px-2 py-0.5 rounded bg-emerald-500/20 text-emerald-300 text-[10px]">AMAN</span>'}
                    </td>
                </tr>
            `).join('');
        }

        document.addEventListener('DOMContentLoaded', () => {
            loadInventory();
            if (window.Echo) {
                window.Echo.channel('warehouse-telemetry')
                    .listen('.telemetry.updated', (e) => {
                        updateTelemetryUI(e.telemetry);
                    });
            }
        });
    </script>
</body>
</html>