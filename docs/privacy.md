# Privacy

What the Discord bot stores, where, and how to get rid of it.

You should be able to use this without it revealing more about you than you chose
to share, and the bot is built so that using it does not undo that. This page is
written from the schema in `bot/migrations/`, not from intent, so it can be
checked rather than believed.

## The short version

- The only identifier the bot keeps about you is your **Discord user ID** — a
  number, not a name. Linked Roles adds your GitHub numeric ID, also a number.
  No usernames are retained anywhere.
- Nothing is stored at all unless you run a command that registers something.
  Asking a question stores nothing.
- `/forget` is a hard delete, not a flag.
- No analytics, no advertising, no profiling, and no sale or sharing of anything
  to anyone.

## What is stored, by feature

Run nothing and nothing about you exists. Each row below appears only once you
use that feature.

### Asking questions

`/why`, `/spec`, `/size`, `/decode-devid`, `/context`, `/verify`, `/twin` and
`/ping` store **nothing**. They read the repository and answer.

`/help-me` stores nothing either. What you type goes into the forum thread it
opens, in the server, where you can see it and delete it like any other message.
The console paste is posted as a message, never written to the database.

The one exception is an idempotency record: the ID of the interaction and a
timestamp, so that a delivery Discord retries cannot open a second thread. It
contains no identifier of yours.

### The hardware registry — `/ihave`

One row per person per board, in a table called `rigs`, with eleven columns:

| Column | What it is |
|---|---|
| `discord_user_id` | your Discord user ID |
| `board`, `radio`, `nfc` | fixed choices from the command |
| `phone_model`, `ios_version`, `probe_serial` | text you typed |
| `utc_offset`, `awake_start`, `awake_end` | the hours you said you are reachable |
| `updated_at` | when you last ran `/ihave` |

No username, no display name, no server membership, no avatar.

`probe_serial` is the serial of a debug probe, which you may leave blank. It is
there so a maintainer can tell two identical boards apart, and it is the one
field worth thinking about before filling in, because a hardware serial is
durable in a way a phone model is not.

### Rate limiting

`/matrix` and `/build` each keep one row holding your Discord user ID and the
timestamp of your last use, so that a per-user cooldown can be enforced without
reading anything else.

### The test queue — `/test-request`, `/test-result`

A test request stores the requester's and claimer's Discord user IDs, the board
and iOS version, the free text of what needs testing, and the Discord channel,
message and thread IDs of its own card. Alongside it, one row per candidate who
was eligible to be pinged, holding that person's Discord user ID and whether
they were awake when the request went out.

A finished test stores the result — board, iOS version, pass or fail, the
tester's Discord user ID, and when. This is what `/matrix` renders.

### Linked Roles — `/linked-role`, entirely optional

Nothing here exists unless you choose to link your accounts, and once linking
finishes what remains is your Discord user ID, your GitHub **numeric** ID, and
two timestamps.

Linking is two browser redirects — authorise Discord, then authorise GitHub —
so some things have to survive the gap between them:

- Your Discord OAuth **access token**, encrypted with AES-256-GCM while it
  waits. The key lives in the Worker's secret store, never in the repository.
  Discord also issues a long-lived *refresh* token; this bot throws it away
  without ever writing it down.
- Your **GitHub login**, used once to count your merged pull requests for the
  badge.

Both are **deleted the moment the badge is pushed**, which is the last thing
that needs them. Discord keeps the badge on its own side, and re-running
`/linked-role` authorises again from scratch rather than reusing anything held
here. If you start the flow and abandon it halfway, a scheduled sweep deletes
the whole row within ten minutes — the same window after which the flow could
no longer be completed anyway.

The GitHub OAuth token is never written down at all. It is used once, in
memory, to ask GitHub who you are, and discarded.

What is kept is your GitHub numeric ID, which is an opaque number, and when you
linked. If linking a GitHub account to a Discord account is not a trade you
want, do not run `/linked-role`. Every other command works without it.

## Where it lives

One Cloudflare D1 database, reachable only by this bot's Worker. There is no
second copy, no third-party analytics service, and no export anywhere.
Wrangler's own telemetry is disabled in `bot/wrangler.toml` and in CI, so
neither a local command nor a workflow run reports usage to anyone.

## Getting rid of it

- **`/forget`** deletes every registry row you have, immediately. With a board
  named, it deletes that one. It is a `DELETE`, not a soft-delete flag.
- **Test results and queue rows** are project records rather than personal ones,
  and are not removed by `/forget`. Ask a maintainer if you want one dropped.
- **Linked Roles** leaves nothing sensitive to delete: the tokens and your
  GitHub login are gone as soon as the badge is pushed. What survives is your
  Discord user ID, your GitHub numeric ID and two timestamps. There is no
  self-service command to remove that last row yet — ask a maintainer and it
  will be deleted.

## What this page does not cover

Discord itself. Everything you type into a Discord client is subject to
Discord's privacy policy before this bot ever sees it, and the forum threads
`/help-me` opens are ordinary Discord messages living in Discord's systems.

## Reporting a problem

If something here does not match what the bot does, that is a bug in the bot or
a bug on this page, and both are worth an issue. `bot/migrations/` is the
authority: if this page and the schema disagree, the schema is what runs.
