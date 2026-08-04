# Terms

What you are agreeing to by using the Discord bot, and what this project does
and does not promise about the firmware.

Written to be read rather than survived. If something here is unclear, ask
instead of guessing.

## What this covers

Two separate things, with different answers:

- **Doorman**, the Discord bot that answers questions about this repository and
  tracks who owns which hardware.
- **openaliro** itself — the firmware, the tooling, and these docs.

What the bot stores about you is on its own page: [privacy](privacy.md).

## The firmware is experimental, and it opens doors

This matters more than anything else here.

openaliro is a hobby reimplementation of a door lock protocol, written from a
published specification and tested on development boards on a bench. It has not
been through a security audit, a certification programme, or any independent
review. It is not a product.

**Do not make it the only thing between somebody and your home.** Keep a
mechanical key. Keep a way in that does not depend on this firmware booting,
ranging correctly, or having been flashed with the image you believe you
flashed. Treat an unlock it grants as a convenience rather than a guarantee, and
treat one it refuses as the expected behaviour of unfinished software.

Deploying it on a door that matters is your decision and your risk.

## No warranty

The software is provided as is, without warranty of any kind, express or
implied, including but not limited to the warranties of merchantability, fitness
for a particular purpose and non-infringement. In no event shall the authors or
copyright holders be liable for any claim, damages or other liability arising
from, out of, or in connection with the software or its use.

The licence files in the repository are the authoritative terms for the code,
and different parts of the tree are under different licences. In particular the
vendored Qorvo driver under `deps/dw3000/dwt_uwb_driver/` is **not** open
source: its use is tied to Qorvo hardware and reverse engineering of it is
prohibited. Read `LICENSE` before reusing any of this elsewhere.

## Not affiliated with anyone

openaliro is an independent open-source project. It is not affiliated with,
endorsed by, or connected to the Connectivity Standards Alliance, Apple, Qorvo,
Nordic Semiconductor, or any other company whose products, specifications or
trademarks it interoperates with or names. Those names appear only to say what
this project talks to.

"Aliro" is a trademark of its owner and is used here descriptively.

## Using the bot

Doorman is offered to contributors and users of this project as a convenience.
Reasonable expectations in both directions:

- **Do not use it to attack anything.** No attempts to break out of it, exhaust
  it, or use it as a relay to somewhere else.
- **Do not paste other people's secrets into it.** Console pastes end up in a
  public forum thread. There is an AutoMod guard for credential-shaped text, and
  it is a safety net rather than a promise.
- **What it says can be wrong.** Every answer carries a `file:line` into the tree
  at a commit precisely so you can check it. Check it before acting on it. An
  answer is a pointer to the source, not a substitute for reading it.
- **Some commands act on your behalf.** `/build` dispatches a CI workflow,
  `/help-me` opens a public forum thread, and `/ihave` writes to a registry other
  contributors can query. Nothing happens that a command you ran did not ask for.

Access can be withdrawn from anyone abusing it, without notice and without a
process, because this is a hobby project and there is nobody to staff an appeal.

## Availability

None is promised. The bot runs on a free tier, the maintainer is one person, and
it may be slow, broken, or switched off permanently at any time without warning.
Nothing here is a service you are entitled to.

## Other people's terms still apply

Using the bot means using Discord, and Discord's own terms and policies govern
that. Linking a GitHub account brings GitHub's terms with it. This page replaces
neither.

## Changes

These terms can change. The version that applies is the one published here, and
the repository's history is the record of what changed and when — there is no
separate changelog and no email you will be sent.

## Not legal advice

This page is written in plain language by the project's maintainer, not by a
lawyer, and it is not legal advice to you.
