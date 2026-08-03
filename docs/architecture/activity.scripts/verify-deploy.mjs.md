<!-- generated documentation — edit the source, not this file -->
# `activity/scripts/verify-deploy.mjs`

Check that what a host actually serves is what we built.
A CDN is entitled to compress, cache and rewrite. twin.js is a binary file
wearing a .js extension, so a host that "helpfully" minified or re-encoded it
would corrupt the firmware while still returning 200 and looking fine in a
browser tab. This fetches the deployed files and compares them byte for byte
against the local build, and reports the response headers so an injected CSP
cannot arrive unnoticed.
Usage: node scripts/verify-deploy.mjs https://your-host.example
