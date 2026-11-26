<!DOCTYPE html>
<html lang="id" data-theme="light">
<head>
    <meta charset="UTF-8">
    <meta name="viewport" content="width=device-width, initial-scale=1.0">
    <meta name="csrf-token" content="{{ csrf_token() }}">
    <meta name="theme-color" content="#f4f7fb">
    <meta name="description" content="Dashboard pemantauan dan pengelolaan Smart Warehouse AS/RS.">
    <title>Dashboard — Smart Warehouse</title>
    @include('partials.theme-init')
    @include('partials.favicon')
    @vite(['resources/css/app.css', 'resources/js/app.js', 'resources/js/dashboard.js'])
</head>
<body class="dashboard">
    <div class="container" id="dashboard-app"
         data-login-url="{{ route('login') }}"
         data-status-url="{{ url('/lift/status') }}"
         data-command-url="{{ url('/lift/command') }}"
         data-ack-url="{{ url('/lift/ack-error') }}"
         data-inventory-url="{{ url('/inventory') }}"
         data-put-away-url="{{ url('/inventory/put-away') }}"
         data-picking-url="{{ url('/inventory/picking') }}">

        <header class="topbar">
            <a class="brand" href="{{ route('dashboard') }}" aria-label="Smart Warehouse, beranda">
                <span class="brand__mark" aria-hidden="true">
                    <svg viewBox="0 0 24 24" fill="none">
                        <path d="M4 20V8.5L12 4l8 4.5V20H4Z" stroke="currentColor" stroke-width="1.8" stroke-linejoin="round"/>
                        <path d="M8 20v-5h8v5M8 10h.01M12 10h.01M16 10h.01" stroke="currentColor" stroke-width="1.8" stroke-linecap="round"/>
                    </svg>
                </span>
                <span class="brand__copy">
                    <span class="brand__eyebrow">WAREHOUSE AUTOMATION</span>
                    <span class="brand__title">Smart Warehouse <span>AS/RS</span></span>
                    <span class="brand__subtitle">Monitoring &amp; inventory control</span>
                </span>
            </a>

            <div class="topbar__right">
                <div class="pill-group" role="group" aria-label="Status sistem lift">
                    <span id="badge-homed" class="pill" data-tone="muted">POSISI BELUM VALID</span>
                    <span id="badge-ready" class="pill" data-tone="muted">BELUM SIAP</span>
                    <span id="badge-status" class="pill" data-tone="muted">STATE: OFFLINE</span>
                </div>
                <div class="action-group">
                    <button type="button" class="btn btn--icon btn--ghost theme-toggle" data-theme-toggle aria-label="Ubah tema">
                        <span class="theme-toggle__icon" aria-hidden="true">
                        <svg class="icon-moon" viewBox="0 0 24 24" fill="none"><path d="M20 14.5A8 8 0 0 1 9.5 4 8 8 0 1 0 20 14.5Z" stroke="currentColor" stroke-width="1.8" stroke-linejoin="round"/></svg>
                        <svg class="icon-sun" viewBox="0 0 24 24" fill="none"><circle cx="12" cy="12" r="4" stroke="currentColor" stroke-width="1.8"/><path d="M12 3v2M12 19v2M3 12h2M19 12h2M5.6 5.6 7 7M17 17l1.4 1.4M5.6 18.4 7 17M17 7l1.4-1.4" stroke="currentColor" stroke-width="1.8" stroke-linecap="round"/></svg>
                    </span>
                        <span class="theme-toggle__label">Tema</span>
                    </button>
                    <button type="button" class="btn btn--danger" data-action="stop" title="Kirim perintah berhenti ke lift">
                        <span aria-hidden="true">■</span> STOP
                    </button>
                    @can('lift-admin')
                        <button type="button" class="btn btn--ghost btn--ack" data-action="ack-error"
                                title="Hapus status fault di ESP32. Kalibrasi MODE x3 tetap wajib di perangkat.">
                            ACK Error
                        </button>
                    @endcan
                    <form method="POST" action="{{ route('logout') }}">
                        @csrf
                        <button type="submit" class="btn btn--ghost">Keluar</button>
                    </form>
                </div>
            </div>
        </header>

        <main id="main-content">
            <section class="page-heading" aria-labelledby="page-title">
                <div>
                    <p class="eyebrow">OVERVIEW</p>
                    <h1 id="page-title">Monitoring operasional</h1>
                    <p class="page-heading__description">Pantau posisi lift, kelola barang, dan periksa kondisi gudang dari satu tempat.</p>
                </div>
                <div class="connection-note">
                    <span class="connection-note__dot" aria-hidden="true"></span>
                    <span>Telemetri ESP32</span>
                    <span class="connection-note__time" id="last-updated">Menunggu data</span>
                </div>
            </section>

            <div id="info-banner" class="alert" data-tone="warn" role="status" aria-live="polite" hidden></div>

            <div class="dashboard-grid">
                <section class="card lift-card" aria-labelledby="lift-title">
                    <div class="card__head">
                        <div>
                            <p class="eyebrow">LIVE MONITORING</p>
                            <h2 id="lift-title" class="card__heading">Digital twin lift</h2>
                        </div>
                        <span class="card__icon" aria-hidden="true">
                            <svg viewBox="0 0 24 24" fill="none">
                                <path d="M7 3h10v18H7zM10 7l2-2 2 2M10 17l2 2 2-2" stroke="currentColor" stroke-width="1.7" stroke-linecap="round" stroke-linejoin="round"/>
                            </svg>
                        </span>
                    </div>

                    <div id="shaft" class="shaft" role="img" aria-label="Visualisasi posisi lift pada poros dua lantai">
                        <div class="shaft__label shaft__label--top">
                            <span class="shaft__floor">Lantai 2</span>
                            <span class="shaft__code mono">R1-L2-S1</span>
                            <span id="label-floor2" class="shaft__ticks">Belum dikalibrasi</span>
                        </div>
                        <div class="shaft__label shaft__label--bottom">
                            <span class="shaft__floor">Lantai 1</span>
                            <span class="shaft__code mono">R1-L1-S1</span>
                            <span class="shaft__ticks">0 ticks</span>
                        </div>
                        <div class="shaft__track" aria-hidden="true">
                            <div class="shaft__rail"></div>
                            <div class="shaft__line shaft__line--top"></div>
                            <div class="shaft__line shaft__line--bottom"></div>
                            <div id="lift-carriage" class="carriage" data-tone="muted">
                                <span id="carriage-floor">MENUNGGU DATA</span>
                            </div>
                        </div>
                    </div>

                    <div class="metrics" role="group" aria-label="Pembacaan sensor dan encoder">
                        <div class="metric">
                            <span class="metric__label">Posisi encoder</span>
                            <strong id="val-ticks" class="metric__value"><span data-value>—</span><span class="metric__unit">ticks</span></strong>
                        </div>
                        <div class="metric">
                            <span class="metric__label">Sensor lantai 1</span>
                            <strong id="val-dist1" class="metric__value metric__value--info"><span data-value>—</span><span class="metric__unit">cm</span></strong>
                        </div>
                        <div class="metric">
                            <span class="metric__label">Sensor lantai 2</span>
                            <strong id="val-dist2" class="metric__value metric__value--info"><span data-value>—</span><span class="metric__unit">cm</span></strong>
                        </div>
                    </div>
                </section>

                <section class="card operations-card" aria-labelledby="operations-title">
                    <div class="card__head">
                        <div>
                            <p class="eyebrow">INVENTORY WORKFLOW</p>
                            <h2 id="operations-title" class="card__heading">Operasional barang</h2>
                        </div>
                        <span class="card__icon card__icon--green" aria-hidden="true">
                            <svg viewBox="0 0 24 24" fill="none">
                                <path d="M4 7.5 12 3l8 4.5v9L12 21l-8-4.5v-9Z" stroke="currentColor" stroke-width="1.7" stroke-linejoin="round"/>
                                <path d="m4.5 7.8 7.5 4.3 7.5-4.3M12 12v8.5" stroke="currentColor" stroke-width="1.7" stroke-linejoin="round"/>
                            </svg>
                        </span>
                    </div>

                    @can('lift-admin')
                        <div class="manual-control">
                            <div class="manual-control__copy">
                                <strong>Kontrol manual lift</strong>
                                <span>Gerakkan lift tanpa mencatat pergerakan stok. Khusus admin.</span>
                            </div>
                            <div class="floor-buttons">
                                <button type="button" class="floor-btn" data-manual-rack="R1-L1-S1">
                                    <span class="floor-btn__number">01</span>
                                    <span class="floor-btn__copy"><strong>Lantai 1</strong><small class="mono">R1-L1-S1</small></span>
                                    <span class="floor-btn__arrow" aria-hidden="true">↗</span>
                                </button>
                                <button type="button" class="floor-btn" data-manual-rack="R1-L2-S1">
                                    <span class="floor-btn__number">02</span>
                                    <span class="floor-btn__copy"><strong>Lantai 2</strong><small class="mono">R1-L2-S1</small></span>
                                    <span class="floor-btn__arrow" aria-hidden="true">↗</span>
                                </button>
                            </div>
                        </div>
                    @endcan

                    <div class="workflow-grid">
                        <form id="put-away-form" class="workflow workflow--in">
                            <div class="workflow__heading">
                                <span class="workflow__icon" aria-hidden="true">↓</span>
                                <div>
                                    <h3>Barang masuk</h3>
                                    <p>Put away ke rak kosong</p>
                                </div>
                            </div>
                            <div class="field">
                                <label class="field__label" for="in-code">Kode barang</label>
                                <input id="in-code" name="item_code" class="field__input" type="text" maxlength="50" placeholder="Contoh: BRG-001" autocomplete="off" required>
                            </div>
                            <div class="field">
                                <label class="field__label" for="in-name">Nama barang</label>
                                <input id="in-name" name="item_name" class="field__input" type="text" maxlength="100" placeholder="Nama barang" autocomplete="off" required>
                            </div>
                            <div class="form-grid">
                                <div class="field">
                                    <label class="field__label" for="in-qty">Jumlah masuk</label>
                                    <input id="in-qty" name="quantity" class="field__input" type="number" min="1" step="1" placeholder="0" required>
                                </div>
                                <div class="field">
                                    <label class="field__label" for="in-min">Minimum stok</label>
                                    <input id="in-min" name="min_stock" class="field__input" type="number" min="1" step="1" value="5">
                                </div>
                            </div>
                            <button type="submit" class="btn btn--primary btn--block">
                                Simpan barang <span aria-hidden="true">→</span>
                            </button>
                        </form>

                        <form id="picking-form" class="workflow workflow--out">
                            <div class="workflow__heading">
                                <span class="workflow__icon" aria-hidden="true">↑</span>
                                <div>
                                    <h3>Barang keluar</h3>
                                    <p>Pengambilan berdasarkan FIFO</p>
                                </div>
                            </div>
                            <div class="field">
                                <label class="field__label" for="out-name">Nama barang</label>
                                <input id="out-name" name="item_name" class="field__input" type="text" maxlength="100" placeholder="Nama barang" autocomplete="off" required>
                            </div>
                            <div class="field">
                                <label class="field__label" for="out-qty">Jumlah diambil</label>
                                <input id="out-qty" name="quantity" class="field__input" type="number" min="1" step="1" placeholder="0" required>
                            </div>
                            <p class="workflow__hint">Sistem memilih batch barang tertua yang memiliki jumlah stok mencukupi.</p>
                            <button type="submit" class="btn btn--info btn--block">
                                Proses picking <span aria-hidden="true">→</span>
                            </button>
                        </form>
                    </div>
                </section>

                <section class="card inventory-card" aria-labelledby="inventory-title">
                    <div class="card__head inventory-card__head">
                        <div>
                            <p class="eyebrow">STOCK OVERVIEW</p>
                            <h2 id="inventory-title" class="card__heading">Inventaris gudang</h2>
                            <p class="card__description">Daftar batch barang berdasarkan urutan FIFO dan status stok minimum.</p>
                        </div>
                        <button type="button" class="btn btn--secondary" id="refresh-inventory">
                            <span aria-hidden="true">↻</span> Refresh
                        </button>
                    </div>
                    <div class="table-wrap">
                        <table class="table">
                            <caption class="sr-only">Daftar inventaris gudang</caption>
                            <thead>
                                <tr>
                                    <th scope="col">Kode barang</th>
                                    <th scope="col">Nama barang</th>
                                    <th scope="col">Lokasi rak</th>
                                    <th scope="col" class="table__num">Jumlah stok</th>
                                    <th scope="col">Status</th>
                                </tr>
                            </thead>
                            <tbody id="inventory-body" aria-live="polite">
                                <tr><td colspan="5" class="table__empty">Memuat data inventaris…</td></tr>
                            </tbody>
                        </table>
                    </div>
                    <div class="table-footer">
                        <span id="inventory-count">Menyiapkan data…</span>
                        <span>Urut berdasarkan barang masuk terlama</span>
                    </div>
                </section>
            </div>
        </main>

        <footer class="app-footer">
            <span>Smart Warehouse AS/RS</span>
            <span>Dashboard monitoring &amp; inventory</span>
        </footer>
    </div>

    @can('lift-admin')
        <dialog id="ack-dialog" class="dialog" aria-labelledby="ack-dialog-title">
            <form method="dialog" class="dialog__body">
                <h2 id="ack-dialog-title" class="dialog__title">Kirim ACK Error?</h2>
                <p class="dialog__text">Perintah ini menghapus status fault di ESP32. Pastikan lift sudah diperiksa secara fisik. Setelah ACK, kalibrasi MODE x3 tetap wajib dilakukan di perangkat.</p>
                <div class="dialog__actions">
                    <button type="submit" value="cancel" class="btn btn--ghost" autofocus>Batal</button>
                    <button type="submit" value="confirm" class="btn btn--danger">Kirim ACK Error</button>
                </div>
            </form>
        </dialog>
    @endcan

    <div id="toasts" class="toasts" aria-live="polite" aria-atomic="false"></div>
</body>
</html>
