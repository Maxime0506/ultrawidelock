<!-- generated documentation — edit the source, not this file -->
# `activity/scripts/participants-test.mjs`

Hostile-input test for the "N watching" strip.
Usernames are strings other people chose, and Discord's docs say not to
trust what the SDK reports client-side. This drives src/participants.ts with
names designed to break out of the strip, in a real browser DOM, and asserts
that none of them do. Run: node scripts/participants-test.mjs
The strip is bundled with esbuild and handed a duck-typed SDK, so the code
under test is the real module rather than a copy of its logic.
