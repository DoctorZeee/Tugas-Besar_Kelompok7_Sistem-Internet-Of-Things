<!DOCTYPE html>
<html lang="id" data-theme="light">
<head>
    <meta charset="UTF-8">
    <meta name="viewport" content="width=device-width, initial-scale=1.0">
    <meta name="theme-color" content="#f4f7fb">
    <meta name="description" content="Masuk ke dashboard Smart Warehouse AS/RS.">
    <title>Masuk — Smart Warehouse</title>
    @include('partials.theme-init')
    @include('partials.favicon')
    @vite(['resources/css/app.css', 'resources/js/app.js'])
</head>
<body class="auth-page">
    <main class="auth-shell">
        <div class="auth-topbar">
            <a href="{{ route('login') }}" class="auth-brand" aria-label="Smart Warehouse">
                <span class="brand__mark" aria-hidden="true">
                    <svg viewBox="0 0 24 24" fill="none">
                        <path d="M4 20V8.5L12 4l8 4.5V20H4Z" stroke="currentColor" stroke-width="1.8" stroke-linejoin="round"/>
                        <path d="M8 20v-5h8v5M8 10h.01M12 10h.01M16 10h.01" stroke="currentColor" stroke-width="1.8" stroke-linecap="round"/>
                    </svg>
                </span>
                <span>Smart Warehouse <small>AS/RS</small></span>
            </a>
            <button type="button" class="btn btn--icon btn--ghost theme-toggle" data-theme-toggle aria-label="Ubah tema">
                <span class="theme-toggle__icon" aria-hidden="true">
                        <svg class="icon-moon" viewBox="0 0 24 24" fill="none"><path d="M20 14.5A8 8 0 0 1 9.5 4 8 8 0 1 0 20 14.5Z" stroke="currentColor" stroke-width="1.8" stroke-linejoin="round"/></svg>
                        <svg class="icon-sun" viewBox="0 0 24 24" fill="none"><circle cx="12" cy="12" r="4" stroke="currentColor" stroke-width="1.8"/><path d="M12 3v2M12 19v2M3 12h2M19 12h2M5.6 5.6 7 7M17 17l1.4 1.4M5.6 18.4 7 17M17 7l1.4-1.4" stroke="currentColor" stroke-width="1.8" stroke-linecap="round"/></svg>
                    </span>
                <span class="theme-toggle__label">Tema</span>
            </button>
        </div>

        <section class="auth-card" aria-labelledby="login-title">
            <div class="auth-card__icon" aria-hidden="true">
                <svg viewBox="0 0 24 24" fill="none">
                    <path d="M12 12a4 4 0 1 0 0-8 4 4 0 0 0 0 8ZM4.5 20a7.5 7.5 0 0 1 15 0" stroke="currentColor" stroke-width="1.8" stroke-linecap="round"/>
                </svg>
            </div>
            <p class="eyebrow">SECURE ACCESS</p>
            <h1 id="login-title">Selamat datang</h1>
            <p class="auth-card__description">Masuk untuk memantau lift dan mengelola inventaris gudang.</p>

            @if ($errors->any())
                <div class="form-alert" role="alert">
                    <strong>Login belum berhasil</strong>
                    <span>{{ $errors->first() }}</span>
                </div>
            @endif

            <form method="POST" action="{{ route('login') }}" class="auth-form">
                @csrf
                <div class="field">
                    <label class="field__label" for="email">Alamat email</label>
                    <input id="email" name="email" type="email" value="{{ old('email') }}" placeholder="nama@perusahaan.com" autocomplete="username" required autofocus class="field__input">
                </div>
                <div class="field">
                    <label class="field__label" for="password">Kata sandi</label>
                    <input id="password" name="password" type="password" placeholder="Masukkan kata sandi" autocomplete="current-password" required class="field__input">
                </div>
                <label class="remember-field">
                    <input type="checkbox" name="remember" value="1">
                    <span>Ingat saya</span>
                </label>
                <button type="submit" class="btn btn--primary btn--block auth-submit">Masuk ke dashboard <span aria-hidden="true">→</span></button>
            </form>
            <p class="auth-card__security"><span aria-hidden="true">●</span> Akses dilindungi autentikasi dan sesi pengguna.</p>
        </section>

        <p class="auth-footer">Smart Warehouse AS/RS <span>·</span> Dashboard monitoring gudang</p>
    </main>
</body>
</html>
