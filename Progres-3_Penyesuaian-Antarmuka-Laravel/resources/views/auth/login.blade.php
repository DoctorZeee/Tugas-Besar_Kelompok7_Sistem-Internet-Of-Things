<!DOCTYPE html>
<html lang="id">
<head>
    <meta charset="UTF-8">
    <meta name="viewport" content="width=device-width, initial-scale=1.0">
    <title>Login — Smart Warehouse</title>
    @vite(['resources/css/app.css'])
</head>
<body class="bg-slate-950 text-slate-100 font-sans antialiased min-h-screen flex items-center justify-center p-6">
    <form method="POST" action="{{ route('login') }}" class="w-full max-w-sm bg-slate-900 border border-slate-800 rounded-xl p-6 space-y-4">
        @csrf
        <div>
            <h1 class="text-xl font-bold text-emerald-400">Smart Warehouse AS/RS</h1>
            <p class="text-xs text-slate-400">Masuk untuk mengakses dashboard lift.</p>
        </div>
        @error('email')
            <div class="px-3 py-2 rounded-lg bg-rose-500/15 border border-rose-500/40 text-rose-300 text-xs">{{ $message }}</div>
        @enderror
        <input name="email" type="email" value="{{ old('email') }}" placeholder="Email" required autofocus
               class="w-full bg-slate-950 border border-slate-800 rounded-lg px-3 py-2 text-sm">
        <input name="password" type="password" placeholder="Password" required
               class="w-full bg-slate-950 border border-slate-800 rounded-lg px-3 py-2 text-sm">
        <label class="flex items-center gap-2 text-xs text-slate-400">
            <input type="checkbox" name="remember" value="1"> Ingat saya
        </label>
        <button type="submit" class="w-full py-2 bg-emerald-600 hover:bg-emerald-500 rounded-lg text-sm font-semibold transition">Masuk</button>
    </form>
</body>
</html>
