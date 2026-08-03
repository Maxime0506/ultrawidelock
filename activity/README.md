# activity/: the web twin as a Discord Activity

Runs `web-twin/` inside Discord, so several people in a voice channel can step through
the Aliro walk-up together. The twin is not forked, reimplemented or restyled here. This
directory is a build that copies it and adds one script tag.

Phase 0 established that this is possible at all: Discord's Activity iframe grants both
`'wasm-unsafe-eval'` and `'unsafe-inline'`, so the twin's inline-embedded WASM
instantiates and the real `woz_uwb` responder runs in the sandbox. See
`docs/discord-activity-phase0.md` before changing anything about how the files are
served.

## What the build actually does

```
web-twin/index.html  ──copy──▶  dist/index.html   (+46 B: one <script> tag)
web-twin/twin.js     ──copy──▶  dist/twin.js      (byte-identical)
src/discord-boot.ts  ──vite──▶  dist/discord-boot.js
```

`twin.js` carries the WASM module inline as a byte array, so it is a **binary file
wearing a `.js` extension**. Vite never sees it as source. It is copied with `fs` and the
copy is compared back against the original before the build is allowed to succeed:
bundling, minifying or re-encoding it would corrupt the firmware. If you ever find
yourself adding it to a Rollup input, stop.

Two assertions run every build, in `vite.config.ts`:

1. **Drift guard.** Both source files must match the size and SHA-256 in
   `twin.lock.json`, or the build fails before doing any work. A rebuilt twin is then a
   reviewed commit rather than something that rides along in a deploy. After a deliberate
   `make twin-wasm`, run `npm run lock` and commit the new hashes.
2. **Fidelity check.** `dist/twin.js` must be byte-identical to the source, and
   `dist/index.html` with the injected tag deleted must reproduce the source exactly. The
   claim "the Activity and the standalone page cannot drift" is therefore proven on every
   build, not asserted in a comment.

## The one diff, stated

`dist/index.html` differs from `web-twin/index.html` by exactly 46 bytes, injected before
the single `</head>`:

```html
<script defer src="discord-boot.js"></script>
```

`defer` so it runs after parsing but before `DOMContentLoaded`, which lets the shim stamp
`<html>` before the twin has finished booting its WASM, without blocking the parser.

`web-twin/` itself is never modified. The standalone page opened straight off disk is
unaffected and still self-tests green.

## What the boot shim does, and what it refuses to do

`src/discord-boot.ts` is the entire Discord surface:

- reads `frame_id` from the query string, which is how Discord signals an embedded
  launch. Not user-agent sniffing, which would give a false negative on any client we
  have not seen
- sets `data-in-discord` on `<html>` so CSS can adapt: `connecting`, then `ready`,
  or `unconfigured` / `error`
- constructs `new DiscordSDK(clientId)` and awaits `ready()`

That is all of it. No OAuth scope, no token exchange, no backend, no analytics, no user
data. There is no client secret because there is nothing that could use one.

**A failed handshake must never take the twin down.** The simulation is entirely local
and needs Discord for nothing, so every failure path is caught and the page runs on.
Verified in all three states, self-test green in each:

| state | `data-in-discord` | self-test |
|---|---|---|
| no `frame_id` (standalone) | attribute absent | 22/22 |
| `frame_id`, no client id compiled in | `unconfigured` | 22/22 |
| `frame_id`, handshake fails | `error` | 22/22 |

## The "N watching" strip

`src/participants.ts` renders a small pill in the twin's own topbar showing how many
people have the Activity open. It uses `getInstanceConnectedParticipants()` plus the
`ACTIVITY_INSTANCE_PARTICIPANTS_UPDATE` event, so it costs no backend and no OAuth scope.
It is free social presence, and nothing more: each viewer still drives their own twin,
because Discord synchronises no state between instances.

It is inserted before `#themeBtn`, after the flexible `.sub`, so it cannot displace the
existing lockup, and it styles itself from the twin's own CSS custom properties, so it
follows the theme toggle without a light/dark branch of its own.

### Usernames are hostile input

A display name is a string another person chose, and Discord's docs say not to treat
what the SDK reports client-side as truth. Nothing in that file reaches `innerHTML`:
every user-derived string goes through `textContent` on a node built with
`createElement`. That stops markup, but two things markup-escaping does not stop are
handled explicitly:

- **bidi overrides** (`U+202A`-`U+202E`, `U+2066`-`U+2069`) can visually reverse the text
  around them, so a name can appear to say something it does not
- **zero-width characters** (`U+200B`-`U+200F`, `U+FEFF`) render as nothing while still
  counting toward a length check

Both are stripped, along with C0/C7F controls, before the name is measured or shown.

`npm test` drives the real module in a real browser DOM with names built to break out:
an `<img onerror>`, a `</span><script>` breakout, a bidi override, zero-width padding, a
300-character name and an empty one. It asserts no element was injected, no script ran,
the markup survives as literal text, the deceptive characters are gone, the length clamp
held, and the count reflects the whole list rather than the shown subset. 13 checks.

It is not part of `make verify`, because it needs a browser and the sweep's other gates
do not. Run it by hand when touching that file.

If the participants command ever needs a scope this Activity does not request, the strip
disappears and the twin carries on. Losing a headcount is the right outcome; asking for
an OAuth scope to render one is not.

## Checks

| Command | What it covers |
|---|---|
| `npm test` | hostile usernames against the participants strip, real browser DOM, 13 checks |
| `python3 scripts/boot-probe.py` | the boot shim's three states, self-test green in each |
| `python3 scripts/iframe-checks.py` | single-step leg by leg, and the theme toggle with storage refused, 12 checks |
| `npm run verify-deploy <url>` | what a host actually serves, byte for byte |
| `python3 ../web-twin/csp_probe.py` | which CSP directives the twin needs to run at all |

None are in `make verify`: they need a browser and the sweep's other gates do not. Run
them by hand when touching this directory.

Mobile is verified on a physical handset rather than an emulator: dragging the phone and
the sliders work, and the drag does not fight the client's own dismiss gesture.

## Local development

```bash
cd activity
npm install --ignore-scripts     # --ignore-scripts is required; see below
cp .env.example .env             # then paste your application id in
npm run build
npm run preview                  # serves dist/ on http://localhost:5173
```

In a second shell:

```bash
cloudflared tunnel --url http://localhost:5173 --no-autoupdate
```

Two things about that preview server, both measured rather than assumed:

- `vite.config.ts` sets `preview.allowedHosts` to `.trycloudflare.com`. Without it Vite
  rejects the tunnel's unfamiliar `Host` header with `403 Blocked request. This host
  ("...") is not allowed.`, and the Activity shows that instead of the twin, which looks
  like a mapping error and is not one. Verified both ways: a `*.trycloudflare.com` Host
  gets 200, `evil.example.com` gets 403.
- **`vite preview` binds IPv6 loopback only**, `[::1]:5173`. `http://127.0.0.1:5173`
  refuses the connection outright while `http://localhost:5173` works, so point
  `cloudflared` and any `curl` check at `localhost` or `[::1]`, never at the dotted-quad.
  A dotted-quad tunnel target fails in a way that reads like the tunnel being broken.

## Deploying

Cloudflare Pages, deployed from this machine. Once per account:

```bash
cd activity
npx wrangler login                          # opens a browser; only you can do this
npx wrangler pages project create openaliro-twin --production-branch main
```

Then, per deploy:

```bash
npm run deploy                              # builds, then uploads dist/
npm run verify-deploy https://openaliro-twin.pages.dev
```

`verify-deploy` refetches every deployed file and compares it byte for byte against the
local build, and prints the response headers. A CDN is entitled to compress, cache and
rewrite; a host that "helpfully" minified `twin.js` would corrupt the firmware while
still returning 200 and looking perfectly fine in a browser tab. It also surfaces any
`content-security-policy` the host injects, which matters because the twin needs both
`'wasm-unsafe-eval'` and `'unsafe-inline'` to run at all.

### What the deploy discloses, and to whom

The Cloudflare account behind a deploy may carry a real name. Nothing about that reaches
the public. Measured on `*.pages.dev`:

- the TLS certificate is a **wildcard** `CN=pages.dev`, SAN `pages.dev, *.pages.dev`,
  issued to Cloudflare by Google Trust Services. It is not per-project and not
  per-account, so it names nobody
- `whois pages.dev` returns Cloudflare and the Google registry. A `*.pages.dev`
  subdomain has no registration of its own, so there is no WHOIS record to expose
- response headers are generic: `server: cloudflare`, `cf-ray`, `date`, `content-type`
- the only account-specific thing served is the three files in `dist/`, which are
  checked on every build to be the twin plus one 46-byte tag

So the public surface is the twin and nothing else. Two things do travel to Cloudflare
privately, and both are suppressed in `npm run deploy` rather than left to default:

- **`wrangler` telemetry is on by default** (`wrangler telemetry status` says
  `Enabled`). The deploy script sets `WRANGLER_SEND_METRICS=false`. To turn it off for
  every project on the machine, run `npx wrangler telemetry disable`.
- **`wrangler pages deploy` shells out to git** and reads
  `git rev-parse --abbrev-ref HEAD`, `git rev-parse --short HEAD`,
  `git log -1 --format=%B` and `git status --porcelain`, attaching branch, commit hash,
  commit message and a dirty flag to the deployment. It reads **no author name or
  email**: `%B` is the message body alone. The script passes explicit neutral values so
  none of it is auto-detected from the working tree. This metadata is dashboard-only and
  is never served on the site, but there is no reason to send it.

Re-check both claims after any wrangler upgrade, since they are properties of its
current implementation rather than a documented contract.

The project lands on `openaliro-twin.pages.dev`, a domain **root**. That is deliberate:
it sidesteps the trap that the docs site is a GitHub *project* page at
`openaliro.github.io/openaliro/`, where a `/` URL mapping would resolve at the domain
root rather than under `/openaliro/`.

### Why there is no deploy workflow

The brief asked for a GitHub Actions workflow. There deliberately is not one. This
account was suspended for excessive CI load and the workflow set has since been trimmed
to six, none of which build anything on a push except `ci.yml`. Adding a seventh that
fires on every push to `main` reintroduces exactly the pattern that caused the problem,
and it would need a Cloudflare deploy token in GitHub secrets to do it.

A local `wrangler` deploy costs zero CI minutes, stores no secret on GitHub, and keeps
publishing a deliberate act rather than a side effect of merging. If that ever needs to
change, Cloudflare's own Git integration is the next step up: Cloudflare runs the build,
so it still costs no GitHub Actions minutes.

## Privacy Policy and Terms of Service

`static/privacy.html` and `static/terms.html` are copied into `dist/` the same way the
twin is: verbatim, with a byte-identity check on every build. Discord requires both,
publicly hosted, before an app can be verified or listed in the App Directory.

Cloudflare Pages canonicalises away the `.html` suffix, redirecting `/privacy.html` to
`/privacy` with a 308 and an empty body. **The URLs that actually serve content, and the
ones to paste into the Developer Portal, are the extensionless forms**:

```
https://openaliro-twin.pages.dev/privacy
https://openaliro-twin.pages.dev/terms
```

`verify-deploy` checks exactly these paths for that reason: a check against the `.html`
form would pass on a 308 with nothing behind it and call that a success.

Both pages state plainly that the application collects no personal data. That is true:
there is no backend to collect anything with. The one thing they document is the
participants strip reading a headcount from Discord and discarding it, and the theme
preference living in `localStorage` on the visitor's own device.

## Discord portal configuration

One-time, in the Developer Portal:

1. **Activities → Settings → Enable Activities.** This creates the default `Launch` entry
   point command by itself. Do not hand-make one.
2. **Activities → Settings → Supported Platforms.** The list is `web` / `ios` / `android`
   with **no desktop entry**; the desktop client renders Activities in an embedded web
   view, so **`web` is what makes it appear on desktop**. An unticked platform is the
   single most likely reason the activity picker says "No activities match your search".
3. **Activities → URL Mappings.** One row:

   | Prefix | Target |
   |---|---|
   | `/` | your host, e.g. `<name>.trycloudflare.com` |

   Bare hostname: no scheme, no trailing slash, no path. One root mapping covers the
   whole app because the page requests exactly two URLs, `/index.html` and `/twin.js`,
   and makes no other network request at runtime.

And once per machine, in the Discord client: **User Settings → Advanced → Developer
Mode**, which is what exposes the Developer Activity Shelf. An undistributed Activity is
launched from there; ownership is enough and the app does not need to be installed to a
guild. `Application Test Mode` in the same pane is unrelated: it simulates purchases for
monetised apps. Leave it off.

A quick tunnel's hostname is regenerated on every run, so step 3 has to be redone each
session until the Activity has a stable host.

## Why `--ignore-scripts`

`make security GATES="web"` fails any install command in a tracked file that does not
carry it (`scripts/security-web.sh:300`). Installing a package is arbitrary code
execution through `preinstall`/`postinstall`, and that gate closes it. The same gate
rejects `latest` and `*` version specifiers and any `trustedDependencies` key, so the
dependencies here are pinned exactly.

## Not in this phase

Shared state between participants, the "N watching" strip, mobile input auditing and
distribution are phases 3 to 5 of `docs/discord-activity-phase0.md`'s parent plan. The
participants strip in particular needs every user-controlled string sanitised before it
reaches the DOM; Discord's docs are explicit that usernames, avatar URLs and channel
names are untrusted.
