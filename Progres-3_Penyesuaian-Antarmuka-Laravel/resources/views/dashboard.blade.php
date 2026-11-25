<!DOCTYPE html>
<html lang="id">
<head>
    <meta charset="UTF-8">
    <meta name="viewport" content="width=device-width, initial-scale=1.0">
    <title>Smart Warehouse — Digital Twin Dashboard</title>
    <meta name="csrf-token" content="{{ csrf_token() }}">
    @vite(['resources/css/app.css', 'resources/js/app.js'])
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
                <span id="badge-ready" class="px-3 py-1 rounded-full text-xs font-semibold bg-slate-800 text-slate-300 border border-slate-700">
                    SIAP: -
                </span>
                <span id="badge-status" class="px-3 py-1 rounded-full text-xs font-semibold bg-emerald-500/20 text-emerald-300 border border-emerald-500/40">
                    STATE: IDLE
                </span>
                <button onclick="sendLiftCommand('STOP')" class="px-3 py-1.5 rounded-lg text-xs font-semibold bg-amber-600 hover:bg-amber-500 text-white transition">
                    STOP
                </button>
                @can('lift-admin')
                <button onclick="ackError()" title="Hapus status fault di ESP32. Kalibrasi MODE x3 tetap wajib di perangkat." class="px-3 py-1.5 rounded-lg text-xs font-semibold bg-rose-600 hover:bg-rose-500 text-white transition">
                    ACK Error
                </button>
                @endcan
                <form method="POST" action="{{ route('logout') }}">
                    @csrf
                    <button type="submit" class="px-3 py-1.5 rounded-lg text-xs font-semibold bg-slate-800 hover:bg-slate-700 text-slate-200 transition">Keluar</button>
                </form>
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
                    
                    <!-- Shaft Lift Visual (2 lantai; tick Lantai 2 dari kalibrasi di ESP32) -->
                    <div class="relative h-64 bg-slate-950 border border-slate-800 rounded-lg p-4 flex flex-col justify-between overflow-hidden">
                        <div class="flex justify-between items-center border-b border-dashed border-slate-800 pb-2 text-xs text-slate-400">
                            <span>Lantai 2 (R1-L2-S1)</span>
                            <span id="label-floor2">belum terkalibrasi</span>
                        </div>
                        <div class="flex justify-between items-center text-xs text-slate-400">
                            <span>Lantai 1 (R1-L1-S1)</span>
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
                @can('lift-admin')
                <div>
                    <h2 class="text-sm font-semibold uppercase tracking-wider text-slate-400 mb-3">Kontrol Manual Rak</h2>
                    <div class="grid grid-cols-2 gap-2">
                        <button onclick="sendLiftCommand('MANUAL', 'R1-L1-S1')" class="py-2.5 px-3 bg-slate-800 hover:bg-emerald-600 rounded-lg text-xs font-semibold transition">Lantai 1</button>
                        <button onclick="sendLiftCommand('MANUAL', 'R1-L2-S1')" class="py-2.5 px-3 bg-slate-800 hover:bg-emerald-600 rounded-lg text-xs font-semibold transition">Lantai 2</button>
                    </div>
                </div>

                <hr class="border-slate-800">
                @endcan

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
        const csrf = document.querySelector('meta[name="csrf-token"]').content;
        let floor2Ticks = 0;

        const esc = (v) => String(v ?? '').replace(/[&<>"']/g, (c) => ({'&':'&amp;','<':'&lt;','>':'&gt;','"':'&quot;',"'":'&#39;'}[c]));
        const BADGE = 'px-3 py-1 rounded-full text-xs font-semibold border ';

        async function api(method, url, body) {
            const res = await fetch(url, {
                method,
                credentials: 'same-origin',
                headers: {
                    'Content-Type': 'application/json',
                    'Accept': 'application/json',
                    'X-CSRF-TOKEN': csrf,
                    'X-Requested-With': 'XMLHttpRequest',
                },
                body: body ? JSON.stringify(body) : undefined,
            });
            if (res.status === 401 || res.status === 419) { location.href = '/login'; }
            let json = {};
            try { json = await res.json(); } catch (_) {}
            if (res.status === 422 && json.errors) {
                json.message = Object.values(json.errors).flat().join('\n');
            }
            return { ok: res.ok, status: res.status, json };
        }

        function setCm(id, v) {
            document.getElementById(id).innerHTML = (v !== undefined && v >= 0)
                ? `${Number(v).toFixed(1)} <span class="text-[11px] font-normal text-slate-500">cm</span>`
                : `- <span class="text-[11px] font-normal text-slate-500">cm</span>`;
        }

        function updateTelemetryUI(d) {
            const offline = d.state === 'OFFLINE' || d.state === undefined;
            document.getElementById('val-ticks').innerHTML = `${d.ticks ?? 0} <span class="text-[11px] font-normal text-slate-500">ticks</span>`;
            setCm('val-dist1', d.dist1);
            setCm('val-dist2', d.dist2);

            if (d.floor2_ticks > 0) floor2Ticks = d.floor2_ticks;
            document.getElementById('label-floor2').textContent = floor2Ticks > 0 ? `${floor2Ticks} ticks` : 'belum terkalibrasi';

            document.getElementById('carriage-floor').textContent =
                offline ? 'OFFLINE' : (d.floor > 0 ? `LANTAI ${d.floor}` : 'ANTAR LANTAI / BELUM REF.');

            const ratio = floor2Ticks > 0 ? Math.min(Math.max((d.ticks ?? 0) / floor2Ticks, 0), 1) : 0;
            document.getElementById('lift-carriage').style.bottom = `${8 + ratio * 188}px`;

            const homed = document.getElementById('badge-homed');
            homed.textContent = d.position_valid ? 'POSISI: VALID' : 'POSISI: BELUM REFERENSI';
            homed.className = BADGE + (d.position_valid
                ? 'bg-sky-500/20 text-sky-300 border-sky-500/40'
                : 'bg-amber-500/20 text-amber-300 border-amber-500/40');

            const ready = document.getElementById('badge-ready');
            ready.textContent = d.ready_to_move ? 'SIAP GERAK' : 'BELUM SIAP';
            ready.className = BADGE + (d.ready_to_move
                ? 'bg-emerald-500/20 text-emerald-300 border-emerald-500/40'
                : 'bg-slate-800 text-slate-300 border-slate-700');

            const badge = document.getElementById('badge-status');
            badge.textContent = `STATE: ${d.state ?? 'OFFLINE'}`;
            if (['ESTOP', 'ERROR', 'OFFLINE'].includes(d.state) || offline) {
                badge.className = BADGE + 'bg-rose-500/20 text-rose-300 border-rose-500/40';
            } else if (['MOVING', 'MANUAL'].includes(d.state) || d.is_locked) {
                badge.className = BADGE + 'bg-amber-500/20 text-amber-300 border-amber-500/40';
            } else {
                badge.className = BADGE + 'bg-emerald-500/20 text-emerald-300 border-emerald-500/40';
            }

            const msgs = [];
            if (d.fault) msgs.push(`Fault: ${d.fault}`);
            if (d.info) msgs.push(`Info ESP32: ${d.info}`);
            if (d.rack1_near_full) msgs.push('Rak Lantai 1 hampir penuh');
            if (d.rack2_near_full) msgs.push('Rak Lantai 2 hampir penuh');
            if (!offline && !d.position_valid) msgs.push('Kalibrasi MODE x3 di perangkat diperlukan setiap kali ESP32 restart.');
            const banner = document.getElementById('info-banner');
            banner.textContent = msgs.join(' • ');
            banner.classList.toggle('hidden', msgs.length === 0);
        }

        async function sendLiftCommand(action, targetRack = null) {
            const r = await api('POST', '/lift/command', { action, target_rack: targetRack });
            if (!r.ok) alert(r.json.message || 'Perintah ditolak!');
        }

        async function ackError() {
            if (!confirm('Kirim ACK_ERROR? Pastikan lift sudah dicek secara fisik. Kalibrasi MODE x3 tetap wajib di perangkat.')) return;
            const r = await api('POST', '/lift/ack-error');
            alert(r.json.message || 'Selesai');
        }

        async function handlePutAway(e) {
            e.preventDefault();
            const payload = {
                item_code: document.getElementById('in-code').value.trim(),
                item_name: document.getElementById('in-name').value.trim(),
                quantity: parseInt(document.getElementById('in-qty').value, 10),
                min_stock: parseInt(document.getElementById('in-min').value, 10) || 5,
            };
            const r = await api('POST', '/inventory/put-away', payload);
            if (r.ok) {
                e.target.reset();
                document.getElementById('in-min').value = 5;
                alert(r.json.message);
            } else {
                alert(r.json.message || 'Gagal menyimpan barang');
            }
            loadInventory();
        }

        async function handlePicking(e) {
            e.preventDefault();
            const payload = {
                item_name: document.getElementById('out-name').value.trim(),
                quantity: parseInt(document.getElementById('out-qty').value, 10),
            };
            const r = await api('POST', '/inventory/picking', payload);
            if (r.ok) {
                e.target.reset();
                alert(r.json.message + (r.json.rop_warning ? `\nPeringatan ROP: stok ${payload.item_name} mencapai batas minimum!` : ''));
            } else {
                alert(r.json.message || 'Stok tidak mencukupi');
            }
            loadInventory();
        }

        async function loadInventory() {
            const r = await api('GET', '/inventory');
            const tbody = document.getElementById('inventory-body');
            const items = Array.isArray(r.json) ? r.json : [];
            if (!items.length) {
                tbody.innerHTML = '<tr><td colspan="5" class="py-4 text-center text-slate-500">Belum ada barang di rak.</td></tr>';
                return;
            }
            tbody.innerHTML = items.map(item => `
                <tr>
                    <td class="py-2.5 font-mono text-slate-300">${esc(item.item_code)}</td>
                    <td class="py-2.5">${esc(item.item_name)}</td>
                    <td class="py-2.5 text-emerald-400">${item.location ? esc(item.location.location_code) : '-'}</td>
                    <td class="py-2.5 font-semibold">${esc(item.quantity)}</td>
                    <td class="py-2.5">
                        ${item.is_low_stock
                            ? '<span class="px-2 py-0.5 rounded bg-rose-500/20 text-rose-300 text-[10px]">ROP ALERT</span>'
                            : '<span class="px-2 py-0.5 rounded bg-emerald-500/20 text-emerald-300 text-[10px]">AMAN</span>'}
                    </td>
                </tr>
            `).join('');
        }

        document.addEventListener('DOMContentLoaded', async () => {
            loadInventory();
            const snap = await api('GET', '/lift/status');
            if (snap.ok) updateTelemetryUI(snap.json);
            if (window.Echo) {
                window.Echo.private('warehouse-telemetry')
                    .listen('.telemetry.updated', (e) => {
                        updateTelemetryUI(e.telemetry);
                        if (['IDLE', 'ARRIVED'].includes(e.telemetry.state)) loadInventory();
                    });
            }
        });
    </script>
</body>
</html>