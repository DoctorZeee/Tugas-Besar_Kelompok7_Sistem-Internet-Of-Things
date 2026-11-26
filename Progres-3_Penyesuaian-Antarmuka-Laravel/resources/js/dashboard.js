const app = document.getElementById('dashboard-app');

if (app) {
    const csrf = document.querySelector('meta[name="csrf-token"]')?.content ?? '';
    const loginUrl = app.dataset.loginUrl || '/login';
    const urls = {
        status: app.dataset.statusUrl,
        command: app.dataset.commandUrl,
        ack: app.dataset.ackUrl,
        inventory: app.dataset.inventoryUrl,
        putAway: app.dataset.putAwayUrl,
        picking: app.dataset.pickingUrl,
    };

    const byId = (id) => document.getElementById(id);
    const toasts = byId('toasts');
    let floor2Ticks = 0;
    let inventoryLoading = false;
    let telemetryPolling = null;
    let inventoryCount = 0;
    let latestTelemetry = null;
    let lastTelemetryAt = 0;
    let lastRealtimeState = null;
    let telemetryMaxAgeMs = 16000;

    function showToast(message, tone = 'info') {
        if (!toasts) return;

        const toast = document.createElement('div');
        toast.className = 'toast';
        toast.dataset.tone = tone;
        toast.setAttribute('role', tone === 'danger' ? 'alert' : 'status');
        toast.textContent = String(message || 'Operasi selesai.');
        toasts.append(toast);

        window.setTimeout(() => toast.remove(), 5000);
    }

    async function request(method, url, payload = undefined) {
        try {
            const headers = {
                Accept: 'application/json',
                'X-Requested-With': 'XMLHttpRequest',
                'X-CSRF-TOKEN': csrf,
            };

            const options = {
                method,
                credentials: 'same-origin',
                cache: 'no-store',
                headers,
            };

            if (payload !== undefined) {
                headers['Content-Type'] = 'application/json';
                options.body = JSON.stringify(payload);
            }

            const response = await fetch(url, options);

            if ([401, 419].includes(response.status)) {
                window.location.assign(loginUrl);
                return { ok: false, status: response.status, json: { message: 'Sesi berakhir. Silakan masuk kembali.' } };
            }

            let json = {};
            try {
                json = await response.json();
            } catch (_) {
                json = {};
            }

            if (response.status === 422 && json.errors) {
                json.message = Object.values(json.errors).flat().join('\n');
            }

            return { ok: response.ok, status: response.status, json };
        } catch (error) {
            console.error(`Request ${method} ${url} gagal:`, error);
            return {
                ok: false,
                status: 0,
                json: { message: 'Tidak dapat terhubung ke server. Periksa koneksi lalu coba lagi.' },
            };
        }
    }

    function setPill(id, text, tone) {
        const element = byId(id);
        if (!element) return;

        element.textContent = text;
        element.dataset.tone = tone;
    }

    function setMetric(id, value, decimals = 0) {
        const element = byId(id)?.querySelector('[data-value]');
        if (!element) return;

        const numericValue = Number(value);
        element.textContent = Number.isFinite(numericValue)
            ? numericValue.toFixed(decimals)
            : '—';
    }

    function updateShaftPosition(ticks) {
        const shaft = byId('shaft');
        const carriage = byId('lift-carriage');
        if (!shaft || !carriage) return;

        const ratio = floor2Ticks > 0
            ? Math.min(Math.max(Number(ticks) / floor2Ticks, 0), 1)
            : 0;
        const travelHeight = Math.max(0, shaft.clientHeight - 82);
        carriage.style.top = `${16 + (1 - ratio) * travelHeight}px`;
    }

    function updateTelemetry(telemetry = {}) {
        latestTelemetry = telemetry;
        if (telemetry.telemetry_age_limit !== undefined) {
            telemetryMaxAgeMs = Math.max(5000, Number(telemetry.telemetry_age_limit) * 1000 + 1000);
        }
        lastTelemetryAt = Number(telemetry.received_at) > 0
            ? Number(telemetry.received_at) * 1000
            : Date.now();

        const snapshotIsStale = Boolean(telemetry.is_stale)
            || (Number(telemetry.received_at) > 0 && Date.now() - lastTelemetryAt > telemetryMaxAgeMs);
        if (snapshotIsStale) {
            telemetry = {
                ...telemetry,
                state: 'OFFLINE',
                ready_to_move: false,
                is_locked: false,
                info: 'Telemetri ESP32 sudah kedaluwarsa. Periksa koneksi perangkat dan listener MQTT.',
            };
        }

        const state = String(telemetry.state ?? 'OFFLINE').toUpperCase();
        const offline = state === 'OFFLINE' || state === 'UNKNOWN';
        const fatal = ['ESTOP', 'ERROR'].includes(state) || Boolean(telemetry.estop);
        const busy = ['MOVING', 'MANUAL'].includes(state) || Boolean(telemetry.is_locked);
        const ticks = Number(telemetry.ticks ?? 0);

        setMetric('val-ticks', offline ? NaN : ticks);
        setMetric('val-dist1', telemetry.dist1 >= 0 ? telemetry.dist1 : NaN, 1);
        setMetric('val-dist2', telemetry.dist2 >= 0 ? telemetry.dist2 : NaN, 1);

        const floor2Value = Number(telemetry.floor2_ticks ?? 0);
        if (Number.isFinite(floor2Value) && floor2Value > 0) floor2Ticks = floor2Value;

        const floor2Label = byId('label-floor2');
        if (floor2Label) {
            floor2Label.textContent = floor2Ticks > 0
                ? `${floor2Ticks.toLocaleString('id-ID')} ticks`
                : 'Belum dikalibrasi';
        }

        const carriage = byId('lift-carriage');
        const carriageLabel = byId('carriage-floor');
        if (carriageLabel) {
            carriageLabel.textContent = offline
                ? 'OFFLINE'
                : Number(telemetry.floor) > 0
                    ? `LANTAI ${telemetry.floor}`
                    : telemetry.position_valid ? 'ANTAR LANTAI' : 'BELUM REFERENSI';
        }
        if (carriage) {
            carriage.dataset.tone = offline ? 'muted' : fatal ? 'danger' : busy ? 'warn' : 'ok';
        }

        updateShaftPosition(ticks);

        setPill(
            'badge-homed',
            telemetry.position_valid ? 'POSISI VALID' : 'POSISI BELUM VALID',
            telemetry.position_valid ? 'info' : 'warn',
        );
        setPill(
            'badge-ready',
            telemetry.ready_to_move ? 'SIAP GERAK' : 'BELUM SIAP',
            telemetry.ready_to_move ? 'ok' : 'muted',
        );
        setPill(
            'badge-status',
            `STATE: ${state}`,
            offline || fatal ? 'danger' : busy ? 'warn' : 'ok',
        );

        const messages = [];
        if (telemetry.fault) messages.push(`Fault: ${telemetry.fault}`);
        if (telemetry.info) messages.push(`Info perangkat: ${telemetry.info}`);
        if (telemetry.rack1_near_full) messages.push('Rak lantai 1 hampir penuh');
        if (telemetry.rack2_near_full) messages.push('Rak lantai 2 hampir penuh');
        if (!offline && !telemetry.position_valid) {
            messages.push('Kalibrasi MODE x3 di perangkat diperlukan setelah ESP32 restart.');
        }
        if (telemetry.estop) messages.push('E-STOP aktif. Periksa perangkat secara fisik.');

        const banner = byId('info-banner');
        if (banner) {
            banner.dataset.tone = fatal || offline || telemetry.fault ? 'danger' : 'warn';
            const icon = document.createElement('span');
            icon.className = 'alert__icon';
            icon.textContent = '!';
            const messageNodes = messages.map((message) => {
                const item = document.createElement('span');
                item.className = 'alert__item';
                item.textContent = message;
                return item;
            });
            banner.replaceChildren(icon, ...messageNodes);
            banner.hidden = messages.length === 0;
        }

        const lastUpdated = byId('last-updated');
        if (lastUpdated) {
            const receivedTime = Number.isFinite(lastTelemetryAt) && lastTelemetryAt > 0
                ? new Date(lastTelemetryAt)
                : new Date();
            const label = snapshotIsStale ? 'Data terakhir' : 'Diterima';
            lastUpdated.textContent = `${label} ${new Intl.DateTimeFormat('id-ID', {
                hour: '2-digit', minute: '2-digit', second: '2-digit',
            }).format(receivedTime)}`;
        }
    }

    /** Nonaktifkan tombol selama request berjalan agar perintah tidak terkirim ganda. */
    async function withBusy(button, task) {
        if (button?.disabled) return;
        if (button) button.disabled = true;
        try {
            await task();
        } finally {
            if (button) button.disabled = false;
        }
    }

    async function sendLiftCommand(action, targetRack = null) {
        const response = await request('POST', urls.command, {
            action,
            target_rack: targetRack,
        });

        showToast(
            response.json.message || (response.ok ? 'Perintah berhasil dikirim.' : 'Perintah ditolak.'),
            response.ok ? 'ok' : 'danger',
        );
    }

    function confirmAck() {
        const dialog = byId('ack-dialog');
        if (!dialog || typeof dialog.showModal !== 'function') {
            return Promise.resolve(window.confirm(
                'Kirim ACK_ERROR ke ESP32? Pastikan lift sudah diperiksa secara fisik. Kalibrasi MODE x3 tetap wajib di perangkat.',
            ));
        }

        return new Promise((resolve) => {
            dialog.addEventListener('close', () => resolve(dialog.returnValue === 'confirm'), { once: true });
            dialog.returnValue = 'cancel';
            dialog.showModal();
        });
    }

    async function acknowledgeError() {
        if (!(await confirmAck())) return;

        const response = await request('POST', urls.ack);
        showToast(
            response.json.message || (response.ok ? 'ACK_ERROR dikirim.' : 'ACK_ERROR gagal dikirim.'),
            response.ok ? 'warn' : 'danger',
        );
    }

    function setFormBusy(form, busy) {
        const submit = form.querySelector('button[type="submit"]');
        if (!submit) return;

        submit.disabled = busy;
        submit.dataset.originalText ??= submit.textContent.trim();
        submit.textContent = busy ? 'Memproses…' : submit.dataset.originalText;
    }

    async function handlePutAway(event) {
        event.preventDefault();
        const form = event.currentTarget;
        if (!form.reportValidity()) return;

        const formData = new FormData(form);
        const payload = {
            item_code: String(formData.get('item_code') ?? '').trim(),
            item_name: String(formData.get('item_name') ?? '').trim(),
            quantity: Number.parseInt(formData.get('quantity'), 10),
            min_stock: Number.parseInt(formData.get('min_stock'), 10) || 5,
        };

        setFormBusy(form, true);
        const response = await request('POST', urls.putAway, payload);
        setFormBusy(form, false);

        if (response.ok) {
            form.reset();
            byId('in-min').value = '5';
            showToast(response.json.message || 'Barang berhasil dicatat.', 'ok');
            await loadInventory();
        } else {
            showToast(response.json.message || 'Gagal menyimpan barang.', 'danger');
        }
    }

    async function handlePicking(event) {
        event.preventDefault();
        const form = event.currentTarget;
        if (!form.reportValidity()) return;

        const formData = new FormData(form);
        const payload = {
            item_name: String(formData.get('item_name') ?? '').trim(),
            quantity: Number.parseInt(formData.get('quantity'), 10),
        };

        setFormBusy(form, true);
        const response = await request('POST', urls.picking, payload);
        setFormBusy(form, false);

        if (response.ok) {
            form.reset();
            const warning = Boolean(response.json.rop_warning);
            showToast(
                response.json.message || 'Picking berhasil diproses.',
                warning ? 'warn' : 'ok',
            );
            await loadInventory();
        } else {
            showToast(response.json.message || 'Stok tidak mencukupi atau proses gagal.', 'danger');
        }
    }

    function stockStatus(item) {
        const quantity = Number(item.quantity ?? 0);
        if (quantity <= 0) return ['HABIS', 'danger'];
        if (item.is_low_stock) return ['ROP ALERT', 'warn'];
        return ['AMAN', 'ok'];
    }

    function createCell(content, className = '', label = '') {
        const cell = document.createElement('td');
        if (className) cell.className = className;
        if (label) cell.dataset.label = label;
        cell.textContent = content;
        return cell;
    }

    function renderInventory(items) {
        const tbody = byId('inventory-body');
        if (!tbody) return;

        if (!items.length) {
            const row = document.createElement('tr');
            const cell = createCell('Belum ada barang di inventaris.', 'table__empty');
            cell.colSpan = 5;
            row.append(cell);
            tbody.replaceChildren(row);
            return;
        }

        const rows = items.map((item) => {
            const row = document.createElement('tr');
            const [label, tone] = stockStatus(item);
            const locationCode = item.location?.location_code ?? '—';

            row.append(
                createCell(item.item_code ?? '—', 'table__code mono', 'Kode barang'),
                createCell(item.item_name ?? '—', 'table__name', 'Nama barang'),
            );

            const locationCell = document.createElement('td');
            locationCell.dataset.label = 'Lokasi rak';
            const locationChip = document.createElement('span');
            locationChip.className = 'chip mono';
            locationChip.dataset.tone = locationCode === '—' ? 'muted' : 'ok';
            locationChip.textContent = locationCode;
            locationCell.append(locationChip);
            row.append(locationCell);

            row.append(createCell(Number(item.quantity ?? 0).toLocaleString('id-ID'), 'table__num table__stock', 'Jumlah stok'));

            const statusCell = document.createElement('td');
            statusCell.dataset.label = 'Status';
            const statusChip = document.createElement('span');
            statusChip.className = 'chip';
            statusChip.dataset.tone = tone;
            statusChip.textContent = label;
            statusCell.append(statusChip);
            row.append(statusCell);

            return row;
        });

        tbody.replaceChildren(...rows);
    }

    async function loadInventory() {
        if (inventoryLoading) return;
        inventoryLoading = true;

        const refreshButton = byId('refresh-inventory');
        if (refreshButton) refreshButton.disabled = true;

        const response = await request('GET', urls.inventory);
        if (response.ok && Array.isArray(response.json)) {
            inventoryCount = response.json.length;
            renderInventory(response.json);
            const count = byId('inventory-count');
            if (count) count.textContent = `${inventoryCount.toLocaleString('id-ID')} batch barang`;
        } else {
            showToast(response.json.message || 'Data inventaris tidak dapat dimuat.', 'danger');
            const count = byId('inventory-count');
            if (count) count.textContent = 'Gagal memuat data';
        }

        inventoryLoading = false;
        if (refreshButton) refreshButton.disabled = false;
    }

    async function loadTelemetry() {
        const response = await request('GET', urls.status);
        if (response.ok) updateTelemetry(response.json);
    }

    function startTelemetryFallback() {
        if (telemetryPolling !== null) return;
        telemetryPolling = window.setInterval(loadTelemetry, 10000);
    }

    function stopTelemetryFallback() {
        if (telemetryPolling === null) return;
        window.clearInterval(telemetryPolling);
        telemetryPolling = null;
    }

    function monitorTelemetryFreshness() {
        if (!latestTelemetry || !lastTelemetryAt) return;
        if (Date.now() - lastTelemetryAt <= telemetryMaxAgeMs) return;
        if (String(latestTelemetry.state ?? '').toUpperCase() === 'OFFLINE') return;

        updateTelemetry({
            ...latestTelemetry,
            state: 'OFFLINE',
            ready_to_move: false,
            is_locked: false,
            is_stale: true,
            info: 'Telemetri ESP32 terputus atau listener MQTT tidak aktif.',
        });
    }

    async function connectRealtime() {
        if (!import.meta.env.VITE_REVERB_APP_KEY || !import.meta.env.VITE_REVERB_HOST) {
            startTelemetryFallback();
            return;
        }

        try {
            const { createEcho } = await import('./echo.js');
            const echo = window.Echo ?? createEcho();
            if (!echo) {
                startTelemetryFallback();
                return;
            }
            window.Echo = echo;

            echo.private('warehouse-telemetry')
                .listen('.telemetry.updated', (event) => {
                    if (!event?.telemetry) return;
                    const currentState = String(event.telemetry.state ?? 'UNKNOWN').toUpperCase();
                    const wasMoving = ['MOVING', 'MANUAL'].includes(lastRealtimeState);

                    updateTelemetry(event.telemetry);

                    // Refresh stok hanya saat gerakan selesai, bukan pada setiap heartbeat IDLE.
                    if (wasMoving && ['IDLE', 'ARRIVED'].includes(currentState)) {
                        loadInventory();
                    }
                    lastRealtimeState = currentState;
                });

            const connection = echo.connector?.pusher?.connection;
            if (connection?.bind) {
                connection.bind('connected', stopTelemetryFallback);
                connection.bind('disconnected', startTelemetryFallback);
                connection.bind('unavailable', startTelemetryFallback);
                if (connection.state === 'connected') stopTelemetryFallback();
                else startTelemetryFallback();
            }
        } catch (error) {
            console.error('Realtime connection gagal diinisialisasi:', error);
            startTelemetryFallback();
        }
    }

    byId('put-away-form')?.addEventListener('submit', handlePutAway);
    byId('picking-form')?.addEventListener('submit', handlePicking);
    byId('refresh-inventory')?.addEventListener('click', loadInventory);

    const stopButton = document.querySelector('[data-action="stop"]');
    stopButton?.addEventListener('click', () => withBusy(stopButton, () => sendLiftCommand('STOP')));

    const ackButton = document.querySelector('[data-action="ack-error"]');
    ackButton?.addEventListener('click', () => withBusy(ackButton, acknowledgeError));

    document.querySelectorAll('[data-manual-rack]').forEach((button) => {
        button.addEventListener('click', () => {
            withBusy(button, () => sendLiftCommand('MANUAL', button.dataset.manualRack));
        });
    });

    // First render loads the snapshot and stock table; realtime takes over when Reverb is connected.
    Promise.all([loadInventory(), loadTelemetry()]).finally(() => {
        connectRealtime();
        window.setInterval(monitorTelemetryFreshness, 2000);
    });

    window.addEventListener('resize', () => {
        const ticks = byId('val-ticks')?.querySelector('[data-value]')?.textContent;
        const numericTicks = Number(ticks);
        if (Number.isFinite(numericTicks)) updateShaftPosition(numericTicks);
    }, { passive: true });
}
