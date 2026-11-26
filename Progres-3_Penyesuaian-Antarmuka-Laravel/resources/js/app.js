const THEME_KEY = 'smart-warehouse-theme';
const THEME_COLORS = { light: '#f4f7fb', dark: '#0b1220' };
const root = document.documentElement;

function getTheme() {
    return root.dataset.theme === 'dark' ? 'dark' : 'light';
}

function updateThemeControls(theme) {
    document.querySelectorAll('[data-theme-toggle]').forEach((button) => {
        const nextTheme = theme === 'dark' ? 'terang' : 'gelap';

        button.setAttribute('aria-label', `Ubah ke mode ${nextTheme}`);
        button.setAttribute('title', `Ubah ke mode ${nextTheme}`);
    });
}

function setTheme(theme, persist = true) {
    const nextTheme = theme === 'dark' ? 'dark' : 'light';
    root.dataset.theme = nextTheme;

    if (persist) {
        try {
            localStorage.setItem(THEME_KEY, nextTheme);
        } catch (_) {
            // Theme tetap berubah untuk sesi ini ketika storage tidak tersedia.
        }
    }

    document.querySelector('meta[name="theme-color"]')?.setAttribute('content', THEME_COLORS[nextTheme]);
    updateThemeControls(nextTheme);
}

document.addEventListener('DOMContentLoaded', () => {
    updateThemeControls(getTheme());

    document.querySelectorAll('[data-theme-toggle]').forEach((button) => {
        button.addEventListener('click', () => {
            setTheme(getTheme() === 'dark' ? 'light' : 'dark');
        });
    });
});
