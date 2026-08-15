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

  function init() {
    announce(current());

    document.querySelectorAll("[data-theme-toggle]").forEach(function (b) {
      b.addEventListener("click", function () {
        apply(current() === "light" ? "dark" : "light", true);
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
