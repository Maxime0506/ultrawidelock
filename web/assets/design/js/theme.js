/* UltraWideLock — theme.
 *
 * One key, one event, for the whole site. Before this there were three
 * mechanisms: the landing page used 'uwl-theme', the twin persisted its own
 * choice under 'dm-theme' and so never inherited the site's, and the 3D graph
 * read the key but only honoured a saved *dark* preference — a reader who
 * chose light on a dark-preferring OS got dark anyway, because the light
 * branch worked by *not* setting the attribute and relying on a media query
 * that said the opposite.
 *
 * The inline bootstrap in each <head> has already applied the saved value
 * before this file runs; that is what stops the first paint being the wrong
 * theme. This adds the toggle, the system-preference follow, and the event.
 */
(function () {
  "use strict";

  var KEY = "uwl-theme";
  var root = document.documentElement;
  var media = window.matchMedia("(prefers-color-scheme: light)");
  var reduced = window.matchMedia("(prefers-reduced-motion: reduce)");

  function stored() {
    try { return localStorage.getItem(KEY); } catch (e) { return null; }
  }

  /* What is actually on screen right now, which is not the same as what is
   * stored: with nothing stored the OS decides. */
  function current() {
    var attr = root.getAttribute("data-theme");
    if (attr) return attr;
    return media.matches ? "light" : "dark";
  }

  function announce(theme) {
    document.querySelectorAll("[data-theme-toggle]").forEach(function (b) {
      var toLight = theme !== "light";
      b.setAttribute("aria-pressed", theme === "light" ? "true" : "false");
      b.setAttribute("title", toLight ? "Switch to light theme"
                                      : "Switch to dark theme");
      b.setAttribute("aria-label", toLight ? "Switch to light theme"
                                           : "Switch to dark theme");
    });
    /* Canvas and WebGL surfaces cannot inherit CSS custom properties. They
     * listen for this instead of re-reading getComputedStyle every frame,
     * which is a forced style recalculation at 60 fps. */
    window.dispatchEvent(new CustomEvent("uwl:theme", { detail: theme }));
  }

  function apply(theme, remember) {
    root.setAttribute("data-theme", theme);
    if (remember) { try { localStorage.setItem(KEY, theme); } catch (e) {} }
    announce(theme);
  }

  /* The swap, ranged rather than cut. A circle grows from the button that was
   * pressed: the incoming theme is revealed inside it, the outgoing one is
   * erased one feather-width behind its edge, and in the gap between the two
   * neither snapshot is opaque, so the accent underneath reads as a single
   * travelling ring. The geometry lives here because only JS knows where the
   * button is; the animation itself is CSS (see components.css).
   *
   * Every reason to not do this ends in the same instant swap as before:
   * no View Transitions API, a reader who asked for reduced motion (the
   * global duration cap in reset.css cannot reach the ::view-transition
   * pseudo tree, so this has to be refused in JS, not damped in CSS), a
   * keyboard activation with no element to grow from, or the call throwing.
   */
  var running = 0;

  function clearTransitionVars() {
    root.removeAttribute("data-uwl-vt");
    ["x", "y", "max", "f"].forEach(function (n) {
      root.style.removeProperty("--uwl-vt-" + n);
    });
  }

  function swap(theme, origin) {
    var swapNow = function () { apply(theme, true); };
    if (!document.startViewTransition || reduced.matches || !origin) {
      swapNow();
      return;
    }

    var box = origin.getBoundingClientRect();
    var x = box.left + box.width / 2;
    var y = box.top + box.height / 2;
    var w = window.innerWidth;
    var h = window.innerHeight;
    /* The far corner: the circle is done only once it has passed it. */
    var reach = Math.sqrt(Math.pow(Math.max(x, w - x), 2) +
                          Math.pow(Math.max(y, h - y), 2));
    var feather = Math.min(110, Math.max(56, reach * 0.045));

    root.style.setProperty("--uwl-vt-x", x + "px");
    root.style.setProperty("--uwl-vt-y", y + "px");
    root.style.setProperty("--uwl-vt-f", feather + "px");
    /* One feather past the corner, so the incoming theme ends fully opaque
     * and the ring leaves the viewport instead of parking on its edge. */
    root.style.setProperty("--uwl-vt-max", (reach + feather) + "px");
    root.setAttribute("data-uwl-vt", "");

    var t;
    try {
      t = document.startViewTransition(swapNow);
    } catch (e) {
      clearTransitionVars();
      swapNow();
      return;
    }

    /* A second click skips the first transition; only the last one owning the
     * attribute may take it away, or the survivor loses its animation. */
    var mine = ++running;
    var done = function () { if (mine === running) clearTransitionVars(); };
    t.finished.then(done, done);
  }

  function init() {
    announce(current());

    document.querySelectorAll("[data-theme-toggle]").forEach(function (b) {
      b.addEventListener("click", function () {
        swap(current() === "light" ? "dark" : "light", b);
      });
    });

    /* Only while the reader has expressed no preference of their own. Once
     * they have chosen, the OS changing is not a reason to override them. */
    var onSystem = function () { if (!stored()) announce(current()); };
    if (media.addEventListener) media.addEventListener("change", onSystem);
    else if (media.addListener) media.addListener(onSystem);
  }

  if (document.readyState === "loading") {
    document.addEventListener("DOMContentLoaded", init);
  } else {
    init();
  }
})();
