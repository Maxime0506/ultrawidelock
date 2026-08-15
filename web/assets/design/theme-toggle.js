/* UltraWideLock — the only JavaScript in the design system.
   Sets data-theme on <html> and remembers the choice. Inline the first block
   in <head> before any stylesheet to avoid a flash of the wrong theme. */
(function () {
  var KEY = 'uwl-theme';
  var root = document.documentElement;
  try { var saved = localStorage.getItem(KEY); if (saved) root.setAttribute('data-theme', saved); } catch (e) {}

  function current() {
    var attr = root.getAttribute('data-theme');
    if (attr) return attr;
    return window.matchMedia('(prefers-color-scheme: light)').matches ? 'light' : 'dark';
  }
  function apply(theme) {
    root.setAttribute('data-theme', theme);
    try { localStorage.setItem(KEY, theme); } catch (e) {}
    document.querySelectorAll('[data-theme-toggle]').forEach(function (b) {
      b.setAttribute('aria-pressed', theme === 'light' ? 'true' : 'false');
      b.setAttribute('title', theme === 'light' ? 'Switch to dark theme' : 'Switch to light theme');
    });
  }
  function init() {
    apply(current());
    document.querySelectorAll('[data-theme-toggle]').forEach(function (b) {
      b.addEventListener('click', function () { apply(current() === 'light' ? 'dark' : 'light'); });
    });
  }
  if (document.readyState === 'loading') document.addEventListener('DOMContentLoaded', init); else init();
})();
