<!-- generated documentation — edit the source, not this file -->
# `tools/docs_motion.py`

The motion layer: choreograph the arrival, and make the page answer back.

docs_theme.py sets the surfaces and docs_hero.py builds the rooms. Both are
still, though: the site arrives all at once and then sits there. This pass
adds time and reaction to what those two already drew.

Arrival, in order. The hero's parts rise and unblur on a 60ms beat, the
wordmark a character at a time behind them, the terminal card last and from
further away. Then the terminal types itself out: the same two commands the
page already showed, entered rather than printed, with the caret walking
down the lines. Nothing here is new content — every one of those characters
was in the markup already, and is again the moment the animation ends.

Reaction, everywhere after that. The hero tracks the pointer: a warm spot
follows it across the band, the ranging rings lean away from it, and the
terminal turns to face it. The explore cards tilt under it. On scroll the
band's decoration drifts up behind the type and the topbar lifts off the
page. The "On this page" rail grows a marker that slides between sections
instead of the accent jumping. Copy buttons pop green when they land, the
section rules draw themselves in, and where the browser supports it, one
page cross-fades into the next.

Two rules hold all of it together:

  * Nothing may end up hidden. Every entrance is a CSS animation with
    `both` fill from the stylesheet in `<head>`, so it cannot be left
    half-applied by a script that failed to load, and every JS effect only
    ever sets a custom property or adds a class to something already
    visible. The one exception is the terminal text, which the typist
    blanks — so the typist writes it back on the same tick it starts, and
    only ever runs when it found the text itself.
  * `prefers-reduced-motion: reduce` removes the entrances, the parallax,
    the tilt, the typing and the page transition, and leaves the site as
    docs_hero.py drew it. That is checked in the media query for the CSS
    and at the top of each function for the script, because a class that
    was already added cannot be un-added by a media query.

The CSS is appended to site/style.css rather than injected into the pages:
it is the one stylesheet in `<head>`, and an entrance that starts at
opacity 0 has to be parsed before the thing it hides is painted. The script
goes in with the other passes' scripts at the end of the body.

Idempotent by marker. Run from the repo root after docs_hero.py.

## API

### `main() -> int`
`tools/docs_motion.py:375`

Append the motion stylesheet to the rendered site's shell and wire its script into every generated page; report what was touched.
