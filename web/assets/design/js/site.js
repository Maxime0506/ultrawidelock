/* UltraWideLock — shared page behaviour.
 *
 * Everything here is progressive: the page is complete and readable with this
 * file blocked, and each block feature-detects rather than assuming. Loaded
 * with `defer`, so it never blocks the first paint.
 */
(function () {
  "use strict";

  var reduced = window.matchMedia("(prefers-reduced-motion: reduce)").matches;
  var $ = function (sel, root) { return (root || document).querySelector(sel); };
  var $$ = function (sel, root) {
    return Array.prototype.slice.call((root || document).querySelectorAll(sel));
  };

  /* ------------------------------------------------------------- reveal -- */
  /* One-shot rise-and-fade as each block reaches the viewport. The elements
   * start hidden only when the `js` class is on <html> (set by an inline
   * script in <head>), so with scripting off nothing is ever invisible. */
  function reveal() {
    var items = $$("[data-reveal]");
    if (!items.length) return;

    if (reduced || !("IntersectionObserver" in window)) {
      items.forEach(function (el) { el.classList.add("is-in"); });
      return;
    }

    var io = new IntersectionObserver(function (entries) {
      entries.forEach(function (e) {
        if (!e.isIntersecting) return;
        /* Stagger siblings so a row of cards arrives as a sequence rather
         * than a single flash. Read off the element, not a global counter,
         * so scroll order never matters. */
        var i = +(e.target.getAttribute("data-reveal") || 0);
        e.target.style.transitionDelay = (i * 60) + "ms";
        e.target.classList.add("is-in");
        io.unobserve(e.target);              /* one-shot: never re-hide */
      });
    }, { rootMargin: "0px 0px -8% 0px", threshold: 0.08 });

    items.forEach(function (el) { io.observe(el); });
  }

  /* ----------------------------------------------------------- mobile nav -- */
  function mobileNav() {
    var btn = $("[data-nav-toggle]");
    var links = $("#navlinks");
    if (!btn || !links) return;

    function close() {
      links.classList.remove("on");
      btn.setAttribute("aria-expanded", "false");
    }
    btn.addEventListener("click", function () {
      var open = links.classList.toggle("on");
      btn.setAttribute("aria-expanded", open ? "true" : "false");
    });
    /* A tap outside, a followed link, or Escape all close it. Without the
     * first two the sheet stays open over the page you just navigated to. */
    document.addEventListener("click", function (e) {
      if (!links.contains(e.target) && !btn.contains(e.target)) close();
    });
    links.addEventListener("click", function (e) {
      if (e.target.closest("a")) close();
    });
    document.addEventListener("keydown", function (e) {
      if (e.key === "Escape") close();
    });
  }

  /* --------------------------------------------------------- copy buttons -- */
  function copyButtons() {
    $$(".prewrap").forEach(function (wrap) {
      var pre = $("pre", wrap);
      if (!pre || $(".copy", wrap)) return;

      var btn = document.createElement("button");
      btn.type = "button";
      btn.className = "btn btn-sm copy";
      btn.textContent = "Copy";
      btn.setAttribute("aria-label", "Copy code to clipboard");

      btn.addEventListener("click", function () {
        var text = pre.innerText;
        var done = function (ok) {
          btn.textContent = ok ? "Copied" : "Press ⌘C";
          btn.classList.toggle("done", ok);
          setTimeout(function () {
            btn.textContent = "Copy";
            btn.classList.remove("done");
          }, 1600);
        };
        /* navigator.clipboard is undefined on an insecure origin, which
         * includes anyone who opened the built site over file://. Selecting
         * the block is the honest fallback: it tells them what to press. */
        if (navigator.clipboard && window.isSecureContext) {
          navigator.clipboard.writeText(text).then(function () { done(true); },
                                                   function () { done(false); });
        } else {
          var sel = window.getSelection();
          var range = document.createRange();
          range.selectNodeContents(pre);
          sel.removeAllRanges();
          sel.addRange(range);
          done(false);
        }
      });
      wrap.appendChild(btn);
    });
  }

  /* -------------------------------------------------------------- tabs ---- */
  function tabs() {
    $$(".tabs").forEach(function (group) {
      var list = $$(".tab", group);
      var panels = $$(".tabpanel", group);
      if (list.length < 2 || list.length !== panels.length) return;

      function show(i) {
        list.forEach(function (t, j) {
          t.setAttribute("aria-selected", j === i ? "true" : "false");
          t.tabIndex = j === i ? 0 : -1;
          panels[j].hidden = j !== i;
        });
      }
      list.forEach(function (t, i) {
        t.addEventListener("click", function () { show(i); });
        /* Arrow keys move between tabs, which is what the pattern requires
         * and what a roving tabindex is for. */
        t.addEventListener("keydown", function (e) {
          var d = e.key === "ArrowRight" ? 1 : e.key === "ArrowLeft" ? -1 : 0;
          if (!d) return;
          e.preventDefault();
          var next = (i + d + list.length) % list.length;
          show(next);
          list[next].focus();
        });
      });
      show(0);
    });
  }

  /* ---------------------------------------------------------- toc spy ----- */
  function tocSpy() {
    var links = $$(".toc-link");
    if (!links.length || !("IntersectionObserver" in window)) return;

    var byId = {};
    var targets = [];
    links.forEach(function (a) {
      var id = a.getAttribute("href").slice(1);
      var el = document.getElementById(id);
      if (el) { byId[id] = a; targets.push(el); }
    });
    if (!targets.length) return;

    var visible = new Set();
    var io = new IntersectionObserver(function (entries) {
      entries.forEach(function (e) {
        if (e.isIntersecting) visible.add(e.target.id);
        else visible.delete(e.target.id);
      });
      /* Highlight the first heading still on screen, in document order.
       * Using "last one that scrolled past" instead makes the marker jump
       * backwards when you scroll up through a long section. */
      var on = null;
      for (var i = 0; i < targets.length; i++) {
        if (visible.has(targets[i].id)) { on = targets[i].id; break; }
      }
      links.forEach(function (a) { a.classList.remove("on"); });
      if (on && byId[on]) byId[on].classList.add("on");
    }, { rootMargin: "-12% 0px -70% 0px" });

    targets.forEach(function (t) { io.observe(t); });
  }

  /* ------------------------------------------------------------ search ---- */
  function search() {
    var pal = $("#palette");
    var scrim = $("[data-palette-scrim]");
    var input = $("#pal-q");
    var out = $("#pal-results");
    if (!pal || !input || !out) return;

    var index = null;
    var loading = false;
    var lastFocus = null;
    var sel = 0;

    function load() {
      if (index || loading) return;
      loading = true;
      /* Fetched on first open, not on page load: the guides are readable
       * without ever opening search, and this keeps it off the critical path. */
      fetch(pal.getAttribute("data-index") || "search.json")
        .then(function (r) { return r.json(); })
        .then(function (data) { index = data; loading = false; run(); })
        .catch(function () {
          loading = false;
          out.innerHTML = '<div class="res-empty">Search index unavailable. ' +
            'The guides are all listed in the sidebar.</div>';
        });
    }

    function score(entry, q) {
      var t = entry.t.toLowerCase();
      var c = (entry.c || "").toLowerCase();
      if (t === q) return 100;
      if (t.indexOf(q) === 0) return 80;
      if (t.indexOf(q) > -1) return 60;
      if (c.indexOf(q) > -1) return 30;
      return 0;
    }

    function run() {
      var q = input.value.trim().toLowerCase();
      if (!index) { load(); return; }
      if (!q) {
        out.innerHTML = '<div class="res-empty">Type to search ' +
          index.length + ' sections across the guides.</div>';
        return;
      }
      var hits = index
        .map(function (e) { return { e: e, s: score(e, q) }; })
        .filter(function (x) { return x.s > 0; })
        .sort(function (a, b) { return b.s - a.s; })
        .slice(0, 12);

      if (!hits.length) {
        out.innerHTML = '<div class="res-empty">No match for &ldquo;' +
          esc(input.value) + '&rdquo;.</div>';
        return;
      }
      sel = 0;
      out.innerHTML = hits.map(function (h, i) {
        return '<a class="res' + (i === 0 ? " sel" : "") + '" href="' +
          esc(h.e.u) + '" role="option"><span class="kind">' + esc(h.e.k) +
          '</span><span class="nm">' + esc(h.e.t) + '</span><span class="ctx">' +
          esc(h.e.c || "") + "</span></a>";
      }).join("");
    }

    function esc(s) {
      var d = document.createElement("div");
      d.textContent = s == null ? "" : String(s);
      return d.innerHTML;
    }

    function move(d) {
      var items = $$(".res", out);
      if (!items.length) return;
      items[sel] && items[sel].classList.remove("sel");
      sel = (sel + d + items.length) % items.length;
      items[sel].classList.add("sel");
      items[sel].scrollIntoView({ block: "nearest" });
    }

    function open() {
      lastFocus = document.activeElement;
      pal.hidden = false;
      if (scrim) { scrim.hidden = false; scrim.classList.add("on"); }
      pal.classList.add("on");
      input.value = "";
      load();
      run();
      input.focus();
    }

    function close() {
      pal.classList.remove("on");
      if (scrim) scrim.classList.remove("on");
      /* Wait out the transition before hiding, or it snaps shut. */
      setTimeout(function () {
        pal.hidden = true;
        if (scrim) scrim.hidden = true;
      }, reduced ? 0 : 200);
      if (lastFocus && lastFocus.focus) lastFocus.focus();
    }

    $$("[data-search-open]").forEach(function (b) {
      b.addEventListener("click", open);
    });
    if (scrim) scrim.addEventListener("click", close);
    input.addEventListener("input", run);

    pal.addEventListener("keydown", function (e) {
      if (e.key === "ArrowDown") { e.preventDefault(); move(1); }
      else if (e.key === "ArrowUp") { e.preventDefault(); move(-1); }
      else if (e.key === "Enter") {
        var hit = $(".res.sel", out);
        if (hit) { e.preventDefault(); window.location.href = hit.href; }
      } else if (e.key === "Tab") {
        /* Minimal focus trap: the dialog holds one input and a result list,
         * so keeping focus on the input is both correct and enough. */
        e.preventDefault();
        input.focus();
      }
    });

    document.addEventListener("keydown", function (e) {
      var open_ = !pal.hidden;
      if (e.key === "Escape" && open_) { e.preventDefault(); close(); return; }
      if (open_) return;
      var typing = /^(INPUT|TEXTAREA|SELECT)$/.test(
        (e.target.tagName || "")) || e.target.isContentEditable;
      if (typing) return;
      if (e.key === "/" || ((e.metaKey || e.ctrlKey) && e.key === "k")) {
        e.preventDefault();
        open();
      }
    });
  }

  /* --------------------------------------------------------------- go ----- */
  function init() {
    reveal();
    mobileNav();
    copyButtons();
    tabs();
    tocSpy();
    search();
  }

  if (document.readyState === "loading") {
    document.addEventListener("DOMContentLoaded", init);
  } else {
    init();
  }
})();
