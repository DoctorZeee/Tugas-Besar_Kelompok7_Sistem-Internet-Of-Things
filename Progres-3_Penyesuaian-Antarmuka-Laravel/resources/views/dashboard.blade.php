<!DOCTYPE html>
<html lang="id">
<head>
    <meta charset="UTF-8">
    <meta name="viewport" content="width=device-width, initial-scale=1.0">
    <title>Smart Warehouse — Digital Twin Dashboard</title>
    <meta name="csrf-token" content="{{ csrf_token() }}">
    @vite(['resources/css/app.css', 'resources/js/app.js'])
</head>
<body class="dashboard">
    <div class="container">
        <!-- Header -->
        <header class="topbar">
            <div class="brand">
                <h1 class="brand__title">Smart Warehouse AS/RS</h1>
                <p class="brand__subtitle">Digital Twin Telemetry, Locking Control &amp; FIFO Inventory</p>
            </div>
            <div class="topbar__right">
                <div class="pill-group">
                    <span id="badge-homed" class="pill" data-tone="muted">POSISI: -</span>
                    <span id="badge-ready" class="pill" data-tone="muted">SIAP: -</span>
                    <span id="badge-status" class="pill" data-tone="muted">STATE: -</span>
                </div>
                <div class="action-group">
                    <button onclick="sendLiftCommand('STOP')" class="btn btn--warn">■ STOP</button>
                    @can('lift-admin')
                    <button onclick="ackError()" title="Hapus status fault di ESP32. Kalibrasi MODE x3 tetap wajib di perangkat." class="btn btn--danger">ACK Error</button>
                    @endcan
                    <form method="POST" action="{{ route('logout') }}">
                        @csrf
                        <button type="submit" class="btn btn--ghost">Keluar</button>
                    </form>
                </div>
            </div>
        </header>

        <!-- Alert info dari ESP32 -->
        <div id="info-banner" class="alert" data-tone="warn" role="status" hidden></div>

        <!-- Grid utama -->
        <main class="grid">
            <!-- Kartu 1: Digital twin lift -->
            <section class="card">
                <h2 class="card__title">Digital Twin Lift</h2>

                <div id="shaft" class="shaft" role="img" aria-label="Posisi lift pada poros dua lantai">
                    <div class="shaft__label shaft__label--top">
                        <span class="shaft__floor">Lantai 2</span>
                        <span class="shaft__code mono">R1-L2-S1</span>
                        <span id="label-floor2" class="shaft__ticks">belum terkalibrasi</span>
                    </div>
                    <div class="shaft__label shaft__label--bottom">
                        <span class="shaft__floor">Lantai 1</span>
                        <span class="shaft__code mono">R1-L1-S1</span>
                        <span class="shaft__ticks">0 ticks</span>
                    </div>

                    <div class="shaft__track">
                        <div class="shaft__rail"></div>
                        <div class="shaft__line shaft__line--top"></div>
                        <div class="shaft__line shaft__line--bottom"></div>
                        <div id="lift-carriage" class="carriage" data-tone="muted">
                            <span id="carriage-floor">MENUNGGU DATA</span>
                        </div>
                    </div>
                </div>

                <div class="metrics">
                    <div class="metric">
                        <p class="metric__label">Encoder</p>
                        <p id="val-ticks" class="metric__value">0<span class="metric__unit">ticks</span></p>
                    </div>
                    <div class="metric">
                        <p class="metric__label">Sensor Lt. 1</p>
                        <p id="val-dist1" class="metric__value metric__value--info">-<span class="metric__unit">cm</span></p>
                    </div>
                    <div class="metric">
                        <p class="metric__label">Sensor Lt. 2</p>
                        <p id="val-dist2" class="metric__value metric__value--info">-<span class="metric__unit">cm</span></p>
                    </div>
                </div>
            </section>

            <!-- Kartu 2: Kontrol manual, put away, picking -->
            <section class="card">
                @can('lift-admin')
                <div class="section">
                    <h2 class="card__title">Kontrol Manual Rak</h2>
                    <p class="section__hint">Gerakkan lift tanpa mencatat stok (khusus admin).</p>
                    <div class="floor-buttons">
                        <button onclick="sendLiftCommand('MANUAL', 'R1-L1-S1')" class="floor-btn">
                            <span class="floor-btn__name">Lantai 1</span>
                            <span class="floor-btn__code mono">R1-L1-S1</span>
                        </button>
                        <button onclick="sendLiftCommand('MANUAL', 'R1-L2-S1')" class="floor-btn">
                            <span class="floor-btn__name">Lantai 2</span>
                            <span class="floor-btn__code mono">R1-L2-S1</span>
                        </button>
                    </div>
                </div>

                <hr class="divider">
                @endcan

                <!-- Put away (barang masuk) -->
                <form onsubmit="handlePutAway(event)" class="section">
                    <h3 class="section__title section__title--ok">Barang Masuk (Put Away)</h3>
                    <div class="form-grid">
                        <div class="field">
                            <label class="field__label" for="in-code">Kode barang</label>
                            <input id="in-code" class="field__input" type="text" placeholder="mis. BRG-01" autocomplete="off" required>
                        </div>
                        <div class="field">
                            <label class="field__label" for="in-name">Nama barang</label>
                            <input id="in-name" class="field__input" type="text" placeholder="mis. Kardus" autocomplete="off" required>
                        </div>
                        <div class="field">
                            <label class="field__label" for="in-qty">Jumlah masuk</label>
                            <input id="in-qty" class="field__input" type="number" placeholder="0" min="1" required>
                        </div>
                        <div class="field">
                            <label class="field__label" for="in-min">Batas minimum (ROP)</label>
                            <input id="in-min" class="field__input" type="number" value="5" min="1">
                        </div>
                    </div>
                    <button type="submit" class="btn btn--primary btn--block">Simpan &amp; Kirim Lift ke Rak Kosong</button>
                </form>

                <hr class="divider">

                <!-- Picking (barang keluar FIFO) -->
                <form onsubmit="handlePicking(event)" class="section">
                    <h3 class="section__title section__title--info">Barang Keluar (Picking FIFO)</h3>
                    <div class="form-grid">
                        <div class="field">
                            <label class="field__label" for="out-name">Nama barang</label>
                            <input id="out-name" class="field__input" type="text" placeholder="mis. Kardus" autocomplete="off" required>
                        </div>
                        <div class="field">
                            <label class="field__label" for="out-qty">Jumlah diambil</label>
                            <input id="out-qty" class="field__input" type="number" placeholder="0" min="1" required>
                        </div>
                    </div>
                    <button type="submit" class="btn btn--info btn--block">Ambil Stok Terlama (FIFO)</button>
                </form>
            </section>

            <!-- Kartu 3: Inventaris -->
            <section class="card card--wide">
                <div class="card__head">
                    <h2 class="card__title">Inventaris (Urutan FIFO)</h2>
                    <button onclick="loadInventory()" class="link-btn">Refresh</button>
                </div>
                <div class="table-wrap">
                    <table class="table">
                        <thead>
                            <tr>
                                <th>Kode</th>
                                <th>Barang</th>
                                <th>Rak</th>
                                <th class="table__num">Stok</th>
                                <th>Status</th>
                            </tr>
                        </thead>
                        <tbody id="inventory-body">
                            <tr><td colspan="5" class="table__empty">Memuat data...</td></tr>
                        </tbody>
                    </table>
                </div>
            </section>
        </main>
    </div>

    <div id="toasts" class="toasts" aria-live="polite"></div>

    <script>
        const csrf = document.querySelector('meta[name="csrf-token"]').content;
        let floor2Ticks = 0;

        const esc = (v) => String(v ?? '').replace(/[&<>"']/g, (c) => ({'&':'&amp;','<':'&lt;','>':'&gt;','"':'&quot;',"'":'&#39;'}[c]));
        const unit = (u) => `<span class="metric__unit">${u}</span>`;

        function toast(message, tone = 'info') {
            const el = document.createElement('div');
            el.className = 'toast';
            el.dataset.tone = tone;
            el.textContent = message;
            document.getElementById('toasts').append(el);
            setTimeout(() => el.remove(), 5000);
        }

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

        function setPill(id, text, tone) {
            const el = document.getElementById(id);
            el.textContent = text;
            el.dataset.tone = tone;
        }

        function setCm(id, v) {
            const value = (v !== undefined && v >= 0) ? Number(v).toFixed(1) : '-';
            document.getElementById(id).innerHTML = value + unit('cm');
        }

        function updateTelemetryUI(d) {
            const offline = d.state === 'OFFLINE' || d.state === undefined;
            const fatal = ['ESTOP', 'ERROR'].includes(d.state);
            const busy = ['MOVING', 'MANUAL'].includes(d.state) || d.is_locked;

            document.getElementById('val-ticks').innerHTML = (d.ticks ?? 0) + unit('ticks');
            setCm('val-dist1', d.dist1);
            setCm('val-dist2', d.dist2);

            if (d.floor2_ticks > 0) floor2Ticks = d.floor2_ticks;
            document.getElementById('label-floor2').textContent = floor2Ticks > 0 ? `${floor2Ticks} ticks` : 'belum terkalibrasi';

            const carriage = document.getElementById('lift-carriage');
            document.getElementById('carriage-floor').textContent =
                offline ? 'OFFLINE'
                : d.floor > 0 ? `LANTAI ${d.floor}`
                : d.position_valid ? 'ANTAR LANTAI' : 'BELUM REFERENSI';
            carriage.dataset.tone = offline ? 'muted' : fatal ? 'danger' : busy ? 'warn' : 'ok';

            const ratio = floor2Ticks > 0 ? Math.min(Math.max((d.ticks ?? 0) / floor2Ticks, 0), 1) : 0;
            document.getElementById('shaft').style.setProperty('--pos', ratio);

            setPill('badge-homed',
                d.position_valid ? 'POSISI: VALID' : 'POSISI: BELUM REFERENSI',
                d.position_valid ? 'info' : 'warn');
            setPill('badge-ready',
                d.ready_to_move ? 'SIAP GERAK' : 'BELUM SIAP',
                d.ready_to_move ? 'ok' : 'muted');
            setPill('badge-status',
                `STATE: ${d.state ?? 'OFFLINE'}`,
                (offline || fatal) ? 'danger' : busy ? 'warn' : 'ok');

            const msgs = [];
            if (d.fault) msgs.push(`Fault: ${d.fault}`);
            if (d.info) msgs.push(`Info ESP32: ${d.info}`);
            if (d.rack1_near_full) msgs.push('Rak Lantai 1 hampir penuh');
            if (d.rack2_near_full) msgs.push('Rak Lantai 2 hampir penuh');
            if (!offline && !d.position_valid) msgs.push('Kalibrasi MODE x3 di perangkat diperlukan setiap kali ESP32 restart.');

            const banner = document.getElementById('info-banner');
            banner.dataset.tone = (d.fault || fatal || offline) ? 'danger' : 'warn';
            banner.replaceChildren(
                Object.assign(document.createElement('span'), { className: 'alert__icon', textContent: '!' }),
                ...msgs.map((m) => Object.assign(document.createElement('span'), { className: 'alert__item', textContent: m })),
            );
            banner.hidden = msgs.length === 0;
        }

        async function sendLiftCommand(action, targetRack = null) {
            const r = await api('POST', '/lift/command', { action, target_rack: targetRack });
            if (r.ok) toast(r.json.message || 'Perintah terkirim', 'ok');
            else toast(r.json.message || 'Perintah ditolak!', 'danger');
        }

        async function ackError() {
            if (!confirm('Kirim ACK_ERROR? Pastikan lift sudah dicek secara fisik. Kalibrasi MODE x3 tetap wajib di perangkat.')) return;
            const r = await api('POST', '/lift/ack-error');
            toast(r.json.message || 'Selesai', r.ok ? 'warn' : 'danger');
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
                toast(r.json.message, 'ok');
            } else {
                toast(r.json.message || 'Gagal menyimpan barang', 'danger');
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
                const rop = r.json.rop_warning ? `\nPeringatan ROP: stok ${payload.item_name} mencapai batas minimum!` : '';
                toast(r.json.message + rop, r.json.rop_warning ? 'warn' : 'ok');
            } else {
                toast(r.json.message || 'Stok tidak mencukupi', 'danger');
            }
            loadInventory();
        }

        function stockStatus(item) {
            if (item.quantity <= 0) return ['HABIS', 'danger'];
            if (item.is_low_stock) return ['ROP ALERT', 'warn'];
            return ['AMAN', 'ok'];
        }

        async function loadInventory() {
            const r = await api('GET', '/inventory');
            const tbody = document.getElementById('inventory-body');
            const items = Array.isArray(r.json) ? r.json : [];
            if (!items.length) {
                tbody.innerHTML = '<tr><td colspan="5" class="table__empty">Belum ada barang di rak.</td></tr>';
                return;
            }
            tbody.innerHTML = items.map((item) => {
                const [label, tone] = stockStatus(item);
                const rack = item.location
                    ? `<span class="chip mono" data-tone="ok">${esc(item.location.location_code)}</span>`
                    : '<span class="chip" data-tone="muted">—</span>';
                return `
                    <tr>
                        <td class="table__code mono">${esc(item.item_code)}</td>
                        <td class="table__name">${esc(item.item_name)}</td>
                        <td>${rack}</td>
                        <td class="table__num table__stock">${esc(item.quantity)}</td>
                        <td><span class="chip" data-tone="${tone}">${label}</span></td>
                    </tr>`;
            }).join('');
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
