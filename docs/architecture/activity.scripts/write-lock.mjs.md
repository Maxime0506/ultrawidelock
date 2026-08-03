<!-- generated documentation — edit the source, not this file -->
# `activity/scripts/write-lock.mjs`

Regenerate activity/twin.lock.json from the current web-twin/ sources.
Run this only when the twin was rebuilt on purpose (`make twin-wasm`). The
lock exists so that a changed firmware blob has to pass through a reviewed
commit instead of riding along in a deploy, so refreshing it without looking
at what changed defeats the point.
