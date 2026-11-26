<script>
    (() => {
        const THEME_COLORS = { light: '#f4f7fb', dark: '#0b1220' };
        let theme = 'light';

        try {
            const preferred = window.matchMedia('(prefers-color-scheme: dark)').matches ? 'dark' : 'light';
            theme = preferred;
            const saved = localStorage.getItem('smart-warehouse-theme');
            if (saved === 'dark' || saved === 'light') theme = saved;
        } catch (_) {
            // Storage diblokir: tetap pakai preferensi OS yang sudah terbaca.
        }

        document.documentElement.dataset.theme = theme;
        document.querySelector('meta[name="theme-color"]')?.setAttribute('content', THEME_COLORS[theme]);
    })();
</script>
