# openaliro Discord bot

A Discord front-end for hardware support. Contributors test this firmware on boards and
iPhones the maintainer does not own, and the cost of that is three or four round trips per
report just to establish which board, which image, which toolchain, and what the console
actually said. This bot collects that once, matches the console output against failure
signatures that are already written down in this repository, and escalates to a thread with
the context attached when it does not recognise something.

It never invents firmware advice. Every diagnostic answer it gives carries a `file:line`
citation into this tree, and output it has no signature for is reported as exactly that.

## Why an HTTP endpoint and not a gateway bot

A gateway bot is a persistent WebSocket, which means a process somebody has to keep running,
on a machine with an uptime obligation and a reachable address. So this is an HTTP
interactions endpoint on a Cloudflare Worker: Discord POSTs a signed request, the Worker
answers, nothing stays connected. Everything the bot does works over that.

It also means no privileged Message Content intent and no message scanning, which is not a
limitation being worked around. Reading arbitrary channel content is not something this bot
should be able to do.

## Layout

```
bot/
  src/index.ts            the fetch handler: verify, then parse, then dispatch
  src/verify.ts           Ed25519 verification. Nothing parses before this passes
  src/discord.ts          the wire constants and response builders
  src/env.ts              the secret bindings
  src/commands/           one file per command, definition and handler together
  scripts/register-commands.ts   bulk-uploads the command list to Discord
  test/                   node:test suites, run against the Worker sources directly
```

The tests import `src/` and run under Node's type stripping, so there is no build step
between the code and the thing under test. `npm run verify` is typecheck plus tests.

## Setup

Once, in the Discord developer portal: create an application, then a bot user. You need three
values from it, and none of them belongs in this repository.

```sh
cd bot
npm ci --ignore-scripts

# The application's Ed25519 public key, from General Information in the portal.
npx wrangler secret put DISCORD_PUBLIC_KEY

# Discord user IDs allowed to run /who-has, comma separated. Unset means
# nobody, which is the direction that fails safely.
npx wrangler secret put MAINTAINER_IDS

# Creating a forum post is the one thing an interaction token cannot do.
npx wrangler secret put DISCORD_BOT_TOKEN

# Where /help-me opens threads: BOARD=channelId,BOARD=channelId. A board with
# no entry falls back to FORUM_CHANNEL_DEFAULT, and a board with neither gets
# its context block handed back to the reporter instead of being dropped.
npx wrangler secret put FORUM_CHANNELS
npx wrangler secret put FORUM_CHANNEL_DEFAULT

# Read-only, and shared by the two features that want the same thing: more
# headroom on public GitHub endpoints. /verify's attestation lookup and the
# Linked Roles merged-PR count both work without it, just rate limited.
npx wrangler secret put GITHUB_READ_TOKEN

# actions:write on this repo only, for /build. The one write-scoped credential
# this Worker holds, kept separate from the read-only token above on purpose.
# Nothing else needs it, and nothing else should be given it.
npx wrangler secret put GITHUB_ACTIONS_TOKEN

# Where /test-request posts its queue card, and how long a request may sit
# unaccepted before the asleep candidates are pinged too (default 30).
npx wrangler secret put TEST_QUEUE_CHANNEL_ID
npx wrangler secret put TEST_REQUEST_ESCALATE_MINUTES

# Linked Roles only. Two OAuth apps: a Discord one (whose client ID doubles as
# the application ID the role-connection endpoints need) and a GitHub one,
# scope-less — only `id`/`login` are ever read. Skip all five and every other
# command still works; only the badge gating goes away.
npx wrangler secret put DISCORD_CLIENT_ID
npx wrangler secret put DISCORD_CLIENT_SECRET
npx wrangler secret put GITHUB_CLIENT_ID
npx wrangler secret put GITHUB_CLIENT_SECRET

# "owner/repo" that merged PRs are counted against. Unset means merged_prs is
# always 0 — the same degrade as a failed search.
npx wrangler secret put GITHUB_REPO

# Base64, decoding to exactly 32 bytes: the AES-256-GCM key for the Discord
# access token, which exists in D1 only for the seconds between the two OAuth
# legs and is nulled the moment the badge push that needed it lands. Still
# required, because "briefly at rest" is still at rest.
npx wrangler secret put OAUTH_ENCRYPTION_KEY

# The registry. wrangler.toml has this project's own database_id committed --
# an identifier, not a credential -- so running your own copy means creating a
# database and replacing that value with the one this prints.
npx wrangler d1 create openaliro-bot
npx wrangler d1 migrations apply openaliro-bot --remote

npx wrangler deploy
```

This Worker answers on a custom domain, set in `wrangler.toml`:

```toml
workers_dev = false
routes = [{ pattern = "bot.example.org", custom_domain = true }]
```

A Worker's default hostname is `<worker-name>.<account-subdomain>.workers.dev`, where the
subdomain is a property of the Cloudflare account rather than of the project. `workers_dev`
defaults to true and the config wins over the dashboard, so turning that URL off has to
happen here or the next deploy turns it back on. Decide the hostname before the first
deploy: it goes into the Discord developer portal, and the Linked Roles flow puts it in a
contributor's address bar.

Nothing else about the account is disclosed by a deploy. Measured against wrangler 4.118.0:
`wrangler deploy` reads no git metadata at all, unlike `wrangler pages deploy`, which
attaches branch, commit hash, commit message and a dirty flag. The Workers-side equivalents
exist but are reached only by `wrangler preview` under `CI=1`. That is an observation about
the current implementation and not a promise, so it is worth re-checking after a wrangler
upgrade.

Take the URL that prints and paste it into **Interactions Endpoint URL** in the portal.
Discord will POST a deliberately invalid PING at that moment and refuse the URL unless the
endpoint rejects it. `src/verify.ts` is what makes
that pass, and `test/index.test.ts` covers the same case, so if the tests are green the URL
save should be too.

Then register the commands. Guild-scoped registration is instant; global registration can lag
by up to an hour, which during setup reads as the bot being broken.

```sh
DISCORD_APPLICATION_ID=<application id> \
DISCORD_BOT_TOKEN=<bot token> \
DISCORD_GUILD_ID=<your server id> \
  npm run register
```

The bot token is read from the environment, never from a file here, and is never printed.

### Local development

`npm run dev` runs the Worker locally, but it cannot receive real interactions: Discord needs
a public URL. Put `DISCORD_PUBLIC_KEY=<hex>` in `bot/.dev.vars` (gitignored) and drive it with
signed requests of your own, or run the test suite, which does exactly that.

`npm ci --ignore-scripts` will report that `esbuild` and `workerd` have postinstall scripts
that did not run. Leave it that way. This repository blocks installs that can execute
package code, and `scripts/security-web.sh` fails the `web` gate on an `npm ci` without the
flag, in this file as well as in the workflow. Neither tool needs its postinstall: both ship
the platform binary as an optional dependency, and `wrangler dev` and `wrangler deploy`
were both exercised here with the scripts skipped.

## Commands

This bot is two products sharing one Worker: **firmware triage** (answer a
contributor's "is this a bug?" from the tree, with a citation) and **hardware
compatibility tracking** (who owns what, who can test what, and what passed).
They were built separately and unified; `internal/bot-unification-plan.md`
records what was reconciled and why.

Sixteen commands, all registered from one table (`src/commands/index.ts`), so a
command cannot exist with no handler or a handler with nothing routing to it.

### Triage

| Command | What it does |
|---|---|
| `/ping` | Answers if the endpoint is up, the public key binding is right, and the response came back inside the 3 second deadline without deferring. Touches no binding, so it cannot fail for a second reason. |
| `/help-me` | Board and image as command options, then a modal for expected, actual and console output. On submit it runs the matcher, opens a forum thread for that board, and posts a context block: the fields, every match with its citation, and what is still unknown. The console paste goes in as a second message. Pings the maintainer on a no-match, or on request. |
| `/why` | Seven recurring "is this a bug?" answers, each with a `file:line` into this tree: no serial port on the CDK, no shell on the Matter image, a hang with no log on `RELEASE=1`, flash refusing with two probes, an NFC tap giving no distance, the HA agent not opening the port, and what `flash-erase` destroys. |
| `/decode-devid` | Reads a raw `DEV_ID` from `make selftest`. `0xDECA03xx` means the DW3110 is answering; `0x00000000` and `0xFFFFFFFF` are the two documented dead reads. Anything else is reported as unrecognised, never diagnosed. |
| `/context` | A paste-able block: every board you have registered, plus the repository's pinned NCS version. It is not a live `doctor`: it cannot inspect your machine, so host OS, installed NCS version and firmware commit are left as blanks to fill in, never guessed at. |
| `/spec <section>` | Which `docs/*.md` files cite an Aliro 1.0 section, e.g. `14` or `11.3.1`. Pointers only — a `file:line` list, never the cited prose. The index is a scan of the tracked tree (`src/spec-index.generated.ts`, `npm run spec-index`), not a hand-typed table, and a test re-scans the live tree on every run to catch it going stale. |
| `/verify <sha256>` | Looks up a GitHub build-provenance attestation for a digest, against `openaliro/openaliro`'s public attestations API. Answers "did CI build this", not "is this safe to run". |
| `/size` | The recorded CDK flash/RAM baseline for the shipping image (SMP + RELEASE + LTO), plus the commit it was measured at. Not a live measurement. |
| `/build <target>` | Dispatches `firmware-builds.yml` with one of its nine real `workflow_dispatch` choices (`all`, `nrf`, `esp32`, or one job by name). Deferred, idempotent on the interaction ID, and rate limited to one dispatch per user per 15 minutes via a guarded D1 upsert — the heaviest command here gets the hardest limit. |
| `/twin approach` | Runs the **real compiled firmware** — `modules/woz_uwb`'s responder, built to WASM from `web-twin/twin.js` — inside this Worker, and reports what the trust gate decided for a simulated walk-up at a given speed, noise level and packet-drop count. Answers "would the gate have opened", with a `fira_session.c` citation and its coverage limits stated on every reply. `/twin explain` is registered but deliberately unimplemented: decoding a raw ranging block needs a firmware entry point that does not exist yet, and inventing one would be guessing. |

### Hardware compatibility

| Command | What it does |
|---|---|
| `/ihave board: radio: nfc:` | Opens a modal for phone model, iOS version, an awake window and a UTC offset. Upserts on `(discord_user_id, board)`: running it again for the same board replaces that entry rather than accumulating rows. |
| `/who-has board: ios:` | Maintainer only — hidden from non-administrators, and gated again against `MAINTAINER_IDS` regardless of guild permissions. At least one of `board`/`ios` is required. Returns user IDs to ping, not a browsable roster. |
| `/forget [board]` | Hard delete, immediate. With no `board`, deletes every board you have registered, and reports how many rows went. |
| `/matrix` | Public, not ephemeral. Renders a PNG: rows are every board this bot knows about (including ones nobody owns — that row is the point), columns are every iOS version seen in the registry, most recent 12. Falls back to a monospace table if the render fails or the per-user 30 s cooldown is still active. All four glyphs are reachable: ✅/❌ come from `/test-result`, ⚠️/❓ from the registry alone. |
| `/test-request board: ios: what:` | Maintainer only. Posts a status Container to `TEST_QUEUE_CHANNEL_ID`, pings the awake candidates, and leaves an **Accept** button that claims the job (first click wins), opens a thread, and edits the card to CLAIMED in place. Asleep candidates get a follow-up ping after `TEST_REQUEST_ESCALATE_MINUTES` (default 30) if nobody has accepted. |
| `/test-result pass\|fail` | Run inside the claim thread, by whoever accepted it and nobody else. Closes the job and writes the ✅/❌ that `/matrix` renders. |

Plus `/linked-role`, which is not a slash command but three browser GET routes on
this same Worker: the Discord and GitHub OAuth callbacks behind Linked Roles
badge gating. They carry no Ed25519 signature, which is exactly why they are
separate routes rather than folded into the interactions endpoint.

## Design notes

### `/test-request`, the Accept button, and why two different message paths edit two different things

`/test-request` posts a Components V2 status card (a `Container`, accent-colored, per
the spec's aesthetic rules) to a fixed `TEST_QUEUE_CHANNEL_ID` channel — not wherever the
maintainer ran the command — because the point is one findable queue. That post, and the
one-time ping naming the awake candidates, both need a **bot token**
(`DISCORD_BOT_TOKEN`): the interaction's own token can only answer the interaction that
carried it, and posting to an arbitrary channel on a schedule the interaction knows
nothing about is not that.

The Accept button is different. A `MESSAGE_COMPONENT` interaction's own token *can* edit
the exact message the button is attached to (`PATCH .../messages/@original`, confirmed
against docs.discord.com 2026-08-04), so the in-place `PENDING -> CLAIMED` edit goes
through that, the same `editOriginal`-family follow-up path every other deferred command
uses — no bot token needed for the edit itself. The bot token only comes back in for
**starting the thread** (`POST .../messages/{id}/threads`), which is a channel operation
the component's own token cannot do.

Race handling is the same atomic-`UPDATE...WHERE`-guard pattern as the `/matrix`
cooldown (`src/cooldown.ts`): `claim()` in `src/testRequests.ts` is one statement gated
on `status = 'pending'`, so two simultaneous Accept clicks cannot both win. The loser
never touches the card — overwriting it would clobber whatever the winner's edit just
wrote — and instead gets a private, undocumented-by-name "someone already accepted this"
follow-up (a second, ordinary `POST` to the same webhook rather than a `PATCH` to
`@original`).

The persistent card never carries a mention: mentions only ever go in a **separate,
disposable** ping message (`buildAwakePing` / `buildEscalationPing` in
`src/testRequestContainer.ts`), each with an explicit `allowed_mentions.users` allow-list
holding exactly the candidate IDs this Worker itself looked up — never derived from the
free-text `what` field, so a maintainer typing `@everyone` into that field cannot make it
ping anyone. This also sidesteps ever needing to reconstruct mention text inside a
Components V2 message later, since `IS_COMPONENTS_V2` disables `content` outright and the
card is what gets edited in place.

"Asleep candidates get pinged on a follow-up if nobody accepts within a configurable
window" (`TEST_REQUEST_ESCALATE_MINUTES`, default 30) is the one part of this bot not
driven by any Discord interaction: `src/scheduled.ts` runs off a Cron Trigger
(`wrangler.toml`'s `[triggers]`, every 5 minutes) rather than an in-request timer, since a
Worker has no way to schedule work minutes after the request that created it has already
finished.

This has not been proven against a live Discord round trip — the same caveat as
`/ihave`'s modal below. In particular, the Section-with-button-accessory shape
(`src/testRequestContainer.ts`) is the one documented way a Components V2 message carries
a button, per docs.discord.com's component reference (checked 2026-08-04); that same page
shows no example of an Action Row nested inside a Container, so that path was not used,
but the docs do not explicitly rule it out either.

### `/test-result`, and why it is claimer-only rather than maintainer-or-claimer

The spec says `/test-result pass|fail` runs "in the thread" and "closes the job, writes
a validation row, and feeds the matrix", without saying who is allowed to run it. This
bot restricts it to whoever `claim()`-ed the request (`test_requests.claimed_by`): they
are the one who actually ran the physical test, and letting anyone else in the thread
close it out risks a maintainer or bystander recording a result nobody actually observed.
If that turns out to be too strict — a maintainer needing to correct a bad submission,
say — that is a deliberate gap for a later prompt to close, not an oversight here.

A request created without an `ios` filter has no `ios_version` to key a validation row
on (`/matrix`'s cells are per board+iOS-version), so `/test-result` still closes the
request and updates the card, but skips writing to `validations` and says so in its
reply — a result that cannot appear on the matrix is not a reason to block closing the
job.

### Linked Roles — two chained OAuth flows, and the one credential this bot actually stores

Everything before this phase stores only a Discord user ID — no username, no token, no
secret tied to a specific person. Linked Roles is different by necessity: Discord's own
metadata-push endpoint (`PUT /users/@me/applications/{id}/role-connection`) has to be
called with **the user's own OAuth access token**, not the bot token, so pushing an
updated badge later means holding onto that token. `src/tokenCipher.ts` encrypts both the
access and refresh token with AES-256-GCM (`OAUTH_ENCRYPTION_KEY`, a Worker secret D1
itself never holds) before they reach `oauth_links` — everything else this bot has ever
written to D1 was already safe to read off a raw table dump; this is the one table that
is not, so it gets the one column-level protection in the whole schema.

The flow is two OAuth2 authorization-code grants chained by one `state` value
(`src/oauthState.ts`), reusing the same state across both legs rather than minting a
second one, so `/github-oauth-callback` can find out which Discord user a GitHub account
belongs to without a cookie or session of any kind:

1. `GET /linked-role` — mints a state, redirects to Discord's own authorize URL
   (`identify role_connections.write`).
2. `GET /discord-oauth-callback` — exchanges the code, learns the caller's Discord user
   ID via `GET /users/@me`, **validates the state before persisting anything** (a bogus
   or replayed state must leave zero trace in `oauth_links` — this ordering was a real bug
   caught by a test, not a design decision made up front), stores the encrypted tokens,
   and redirects into GitHub's own authorize URL with the same state.
3. `GET /github-oauth-callback` — exchanges the GitHub code, calls `GET /user` for the
   account's stable `id`/`login` (no scope requested at all: the spec's "verifies the
   account, not the person" needs nothing beyond public identity), discards the GitHub
   token immediately rather than storing it, computes the five metadata fields, and
   pushes them.

Metadata is pushed once, right when both legs finish — re-running `/linked-role` is how a
contributor refreshes their badge later. There is no scheduled full-refresh sweep across
every linked user: that would spend every linked account's share of GitHub's search rate
limit on every tick whether or not anything about them changed, for a feature the spec
does not ask for. A natural follow-up, not built here.

Two things worth flagging plainly:

- **Not provable against a live round trip.** No Discord application and no GitHub OAuth
  app exist for this bot yet (same as everything else in this repo — nothing has been
  deployed). Every endpoint and body shape was checked against docs.discord.com and
  docs.github.com (2026-08-04) rather than guessed, including the one genuinely
  under-documented detail: the stringified-boolean convention (`"1"`/`"0"`) for
  `has_nfc`, inferred from the metadata type's own description of the *comparison* as
  integer-valued, since the docs never spell out the pushed value's exact string form.
- **`DELETE ... RETURNING` was deliberately not used** in `oauthState.ts` even though
  SQLite has supported it since 3.35 — Cloudflare's own D1 SQL-statements page does not
  confirm it, and the race a `RETURNING` clause would close here (two callbacks racing
  on the exact same unguessable random `state`) was not worth depending on an unverified
  feature for. A plain SELECT-then-DELETE is used instead.

### `/matrix`'s image, and the two things that don't fit a Worker natively

Satori needs raw font bytes (no system fonts in a Worker), and `@resvg/resvg-wasm` needs
its own WASM binary loaded as bytes. Both are vendored into `src/assets.generated.ts` as
base64 rather than imported as files: Wrangler can bundle `.wasm` and (with a module
rule) arbitrary binary imports, but this bot's tests run the TypeScript sources directly
under `node --test`, which understands neither — base64-in-a-.ts-file works identically
in both without an import-rule to keep in sync. Regenerate it with `npm run
generate-assets` after bumping `@resvg/resvg-wasm` or the vendored fonts (license and
provenance: `assets/fonts/NOTICE`). The generated file is large (~4.2 MB source, ~1.7 MB
gzipped) but the whole Worker still deploys at ~1.86 MB compressed — comfortably under
Cloudflare's 3 MB free-tier limit, confirmed against
developers.cloudflare.com/workers/platform/limits on 2026-08-04.

Colors reuse the spec's own four status-Container accents (validated/unvalidated/
known-broken/nobody-owns-it) rather than a new palette. Columns cap at 12 iOS versions
(the most recent ones) for image-size sanity; past that the message notes it was
truncated rather than only showing fewer columns with no explanation.

### `/ihave`'s shape, and why it deviates from a literal reading of the spec

The spec asks for one modal with string selects for board, radio, NFC and phone model,
text inputs for iOS version and the awake window, and a select for UTC offset. This bot
splits it instead: **board, radio and nfc are command options** (Discord's own
`choices`, filled in before the interaction reaches this Worker — a client-validated
dropdown, same as a modal select would give), and **the modal covers only phone model
(free text), iOS version (text), the awake window (text), and UTC offset (select)**. Two
reasons, both load-bearing rather than stylistic:

1. Discord's current docs (docs.discord.com, checked 2026-08-04) confirm the new
   Label-wrapped modal component system this needs, but do not state a cap on how many
   Label components a modal can hold. The historical Action-Row-based modal cap was 5;
   whether the newer system raised that is unconfirmed against a live round trip. Four
   fields is safely under any plausible cap; seven was a real gamble.
2. Phone model has no bounded enum in the schema, and the UWB-capable iPhone lineup
   already exceeds a 25-option select and keeps growing every September. A curated list
   would go stale by the time this ships.

Board, radio and nfc chosen at the command are not available on the follow-up
MODAL_SUBMIT interaction — it is a separate interaction — so they are carried through the
modal's own `custom_id` (`ihave:<board>:<radio>:<nfc>`) and re-validated on submit against
a crafted or stale payload.

This is also the one part of the bot that could not be proven against a live Discord round
trip; `test/modal.test.ts` and `test/registry.test.ts` prove this file's build/parse pair
agrees with itself and with Discord's documented shapes, not that Discord's client
actually renders it as expected.

## AutoMod: the credential-paste guard

**Not a bot feature.** No command, no intent, no gateway, nothing this Worker executes.
Discord's own AutoMod runs these as block-and-alert custom regex rules configured in
**Server Settings → AutoMod**, entirely inside Discord's infrastructure. The reason it lives
there and not here: message-content scanning needs the privileged Message Content intent,
and this bot is built specifically not to hold that (see "What it will never do" below).
AutoMod does not need it — it inspects a message before this bot, or any bot, ever sees it.

Every pattern below is scoped to what this repository's own bring-up flow actually produces,
not a generic secret-scanner list. Cross-checked against `security/gitleaks.toml`, this
repo's own pre-push credential gate, so the bot's guard and the repo's own do not disagree
about what counts as a leak.

| Rule | Blocks | Why |
|---|---|---|
| Session key literal | `(?i)\b(ursk\|upsk\|udsk\|sts_?key\|session_?key\|salted_?ursk)\b[^\n]{0,40}?["'`]([0-9a-fA-F]{32,}\|[A-Za-z0-9+/]{24,}={0,2})["'`]` | The exact rule id `openaliro-session-key-literal` from `security/gitleaks.toml:33-38`. A URSK, UPSK, UDSK or session key is derived at runtime everywhere it is legitimate; a literal one pasted into a bug report is a captured credential, not a hex dump worth debugging from. |
| Matter QR / manual pairing code | `MT:[A-Z0-9]{10,}` for the QR onboarding payload; `\b\d{11}\b\|\b\d{21}\b` near the word "pairing" for the manual code | `docs/configuring.md:108-109` and `:136` — every image prints one at boot or on `codes`/`make nrf-pairing-code`, and it is a live commissioning credential for as long as the fabric using it exists. |
| PEM key material | `-----BEGIN (EC )?(PRIVATE\|PUBLIC) KEY-----` | Standard PEM header. Nothing in this repo's own bring-up flow should ever produce one in console output; seeing one in a paste means a private key file was pasted by mistake. |
| Flight-recorder capture | attachment rule on `*.frc`; regex `\[FREC\]` in message text | `docs/configuring.md:165-168`: raw serial logs with `[FREC]` records and binary `.frc` files "include the full ephemeral URSK. Keep them private and delete unneeded copies." This is the single most specific leak vector this tree documents, because it is the one debugging artifact contributors are explicitly told to capture. |
| Firmware binaries | attachment rule on `*.hex`, `*.bin` | Not a credential leak on its own, but an unreviewed binary posted in a support channel is indistinguishable from a supply-chain drop. `/verify <sha256>` is the sanctioned way to attest one; pasting the file is not. |

**Action:** block the message and alert a moderator-only channel, not delete-and-say-nothing
— the reporter needs to know their paste was caught so they can re-paste the safe part (a
truncated fingerprint, a redacted hex string) rather than wonder why `/help-me` went quiet.

**Not covered, deliberately:** the `ALIRO_TRACE=1` truncated URSK fingerprint
(`docs/configuring.md:158-159`) is designed to be safe to share and is not a full key; a rule
that also caught it would train people to stop trusting the guard.

## What it will never do

- Connect to the gateway, or ask for a privileged intent.
- Read or store message content.
- Give firmware advice that does not trace to a citation in this repository. A confident
  wrong diagnosis costs somebody an evening, so an unrecognised paste escalates rather than
  guesses.
- Reproduce Aliro specification text, anywhere, through any command.
- Act on a GitHub issue without a human.

## Privacy

Contributors should be able to use this without it revealing more about them than they
chose to share, and the bot is built so that using it does not undo that.

- The only identifiers retained are numbers: the Discord user ID, and (for Linked Roles)
  the GitHub numeric ID. Never a username, never a display name. The GitHub login is read
  once to count merged PRs, held only for the seconds between the two OAuth legs, and
  scrubbed with the tokens the moment the badge push lands.
- `oauth_links` is the one table that ever touches a bearer credential, and every such
  column is in-flight only: nulled on success, and the whole row deleted by the scheduled
  sweep if a flow is abandoned between the two legs. The Discord *refresh* token is never
  stored at all — only the short-lived access token, and only until the badge push lands.
- The registry is `migrations/0001_rigs.sql`: eleven columns, one row per person per board.
  Three of them (`phone_model`, `ios_version`, `probe_serial`) are text the contributor
  typed; the rest are enums, an offset, two hours and a timestamp.
- Registry responses are ephemeral by default.
- `/forget` is a hard delete, not a flag.
- No analytics, no message-content storage, and no retention of a console paste beyond the
  user's own thread.
- Wrangler telemetry is off in `wrangler.toml` and in CI, so neither a local invocation nor a
  workflow run reports usage to a third party.

## Error handling

Every failure path returns something a person can act on, plus an eight-character correlation
ID that appears in the Worker log. No bare "something went wrong", and no stack trace into a
channel. Quote the ID when reporting.

## The triage table, and the drift gate

`src/citations.ts` is the whole of what the bot knows. Each entry is a plain reading, a next
command, and a `file:line` into this repository. It is a lookup table and not a model, and it
is built by reading the tree: a failure mode that is not documented here does not get an
entry, because a confident wrong diagnosis costs somebody an evening and no answer costs them
a question.

Each citation also carries an `expect` substring. `npm run drift` reads every cited line and
fails if the substring is no longer on it, so an edit to `mk/cdk.mk` that moves a line breaks
CI rather than leaving the bot to cite the wrong line at people for a year. Verified by
inserting one comment line above the cited block: six citations failed and the gate exited 1.

The gate has a second half that is easy to miss the point of. `bot.yml` only runs when its
`paths:` match, so a citation into `mk/cdk.mk` is unchecked unless `mk/cdk.mk` is one of those
paths, and editing the cited line would be precisely the change that skips the check. So the
drift script also fails when a cited file is absent from the workflow's `paths:` lists. Add a
citation into a new file and the gate tells you to add the trigger.

Do not loosen an `expect` string to make the gate pass. Fix the line number, or drop the
entry.

`/twin` is the reason `citations.ts` also holds the `TWIN` block, and it is worth explaining
why those entries look different. `/twin` prints `file:line` references into
`modules/woz_uwb` and `web-twin/twin_glue.c`, and re-types five firmware constants
(`BLOCK_MS`, `FIRA_RANGE_TRUST_K`, `FIRA_RANGE_SPREAD_CM`, `FIRA_RANGE_NEG_TOL_CM`,
`FIRA_RANGE_MAX_CM`) into TypeScript, because a `#define` in a C header cannot be imported
into a Worker. Those started life as string literals inside `src/twin.ts`, which put them
outside this table and so outside the gate. Every one of them was accurate — which is the
problem, not the reassurance: correct today with nothing watching.

For a `#define`, the `expect` deliberately includes the **value** and not just the name, so
`FIRA_RANGE_TRUST_K` going from 3 to 4 fails the gate rather than leaving `/twin` quietly
explaining a threshold the firmware no longer uses. Verified by making exactly that edit: the
gate exited 1 naming the constant and printing both the expected and the current line.
`src/twin.ts` and `src/commands/twin.ts` now format their references from these entries via
`cite()` rather than holding their own copies, so there is one string to be wrong, and it is
the one being checked. Ranges (`fira_session.c:187-199`) cite the anchor line here and write
the span at the point of use, since a `Citation` is a single line and a range has no one line
to re-read.

`src/spec-index.generated.ts` (`/spec`) is drift-checked differently, because it is not
hand-written: `scripts/build-spec-index.ts` scans every `docs/*.md` file for an Aliro 1.0
section reference and generates the whole table, committed so the Worker can bundle it
without reading the filesystem at request time. `spec-index.test.ts` re-runs that scan
against the live tree and asserts it matches the committed file byte for byte. Run
`npm run spec-index` after editing anything in `docs/` that cites the spec, and commit the
result.

`src/size-baseline.generated.ts` (`/size`) is the same pattern again, for a different
reason: importing `firmware/size-baseline.json` directly was tried first and rejected —
that file carries a per-symbol breakdown for every recorded config to answer a question
`/size` needs six numbers for, and bundling it whole nearly tripled the Worker (62 KiB gzip
to 193 KiB). `scripts/build-size-baseline.ts` extracts just those six numbers;
`size-baseline.test.ts` re-extracts from the live file and asserts a match. Run
`npm run size-baseline` after `make cdk-size-baseline` updates the source file.

`src/build-targets.ts` (`/build`) is hand-copied rather than generated, because a Worker
cannot parse `.github/workflows/firmware-builds.yml`'s `workflow_dispatch` choices at
request time and the list is short enough that generating it would be more code than it
saves. `build-targets.test.ts` re-parses the live workflow with the same list-scanner
`check-citations.ts` uses for `paths:` and asserts the two match, in order.

`src/signatures.ts` is the same idea applied to console output. Each entry is a literal
string this repository documents, and matching is ranked by how much text matched, longest
first. All matches are shown rather than the best one, because `URSK_Unavailable` has two
documented causes and choosing between them is the reader's job. A paste that matches
nothing is reported as matching nothing and pings the maintainer.

## Mentions

Every message the bot sends carries `allowed_mentions`. The default is `{ parse: [] }`,
which means nothing inside text a contributor typed can become a ping, including
`@everyone`. The single exception is `/help-me` pinging the maintainer on a no-match or on
request: that sends `{ parse: [], users: [<maintainer>] }`, where the ID comes from
configuration and never from a field. A test walks every outbound call and asserts no other
ID can reach that list.

## CI

`.github/workflows/bot.yml` runs typecheck, tests, the drift gate, and a `wrangler --dry-run`
bundle. It triggers on `bot/**` and on each file the triage table cites. It is deliberately not part of `make verify`: that sweep is one runner
carrying the firmware host gates, and it installs no Node toolchain. `tests/tooling/verify_test.sh`
records that decision in its `CI_MAP`, which is the file that fails when a workflow job has
neither a local gate nor a written reason.

Nothing in CI can deploy. Publishing the interactions endpoint needs a Cloudflare credential,
and a workflow that holds one is a workflow whose compromise redirects every contributor's
paste.
