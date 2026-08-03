# The digital twin as a Discord Activity

The walk-up simulator that runs at `web-twin/` also runs inside Discord, as an Activity.
Launch it in a voice channel and everyone in the call can watch the same handshake being
stepped through: the BLE connect radius, the Aliro session, the encrypted DS-TWR ranging
blocks, the range-integrity trust gate, and the bolt.

## What you are actually looking at

The twin is not a mock or an animation. `modules/woz_uwb`, the CCC DS-TWR responder, the
range store and the trust gate, is compiled unmodified to WebAssembly, and every ranging
block on the page is a genuinely CCM*-encrypted Pre-POLL / POLL / Response / Final /
Final_Data exchange decoded by the firmware's own RX state machine. The strip along the
bottom of the page runs that firmware against the scenarios `tests/host/test_twin.c` and
`tests/host/test_approach.c` assert, on every load. If it does not say 22/22 in green,
do not trust anything else on the page.

That is the whole reason the Activity port was worth doing: Discord runs apps in a
sandboxed iframe, and a sandbox that blocks WebAssembly would have left only a picture of
a lock. It does not. See `docs/discord-activity-phase0.md` for how that was established
and what it cost.

## Using it

Launch it from the activity picker in a voice channel, then either drag the phone toward
the door or press **Walk up**. The controls change walk speed, approach angle, unlock
threshold and BLE radius; **Ghost-Peak spoof** injects a negative time-of-flight block to
show the trust gate refusing to shorten the range. Single-step mode advances one leg of
the ranging exchange at a time, which is the useful mode if you are explaining what a
Final_Data frame is to someone.

Each viewer drives their own copy. Discord provides no state synchronisation, so nothing
you do is mirrored onto anyone else's screen; you are watching the same program, not the
same session.

## What it collects

Nothing. There is no backend, no OAuth scope, no analytics and no user data. The Activity
completes Discord's `ready()` handshake and does nothing else with the SDK. The
simulation is entirely local to your client, and a failed handshake does not stop it: the
twin runs on regardless.

## Availability

Until the app is distributed through Discord's review process, only the owner and
developer-team members can launch it. Everyone else in the voice channel can watch a
screen share of it, which for a walkthrough is most of the value anyway.

The same page is on the web with no Discord involved, and behaves identically: the
Activity build is the standalone page plus a single 46-byte script tag, and the build
fails if that is ever not true.

## For maintainers

`activity/README.md` covers the build, the byte-fidelity assertions, local development
with a tunnel, the Discord portal settings, and deployment. Read
`docs/discord-activity-phase0.md` before changing how the files are served: the twin
needs both `'wasm-unsafe-eval'` and `'unsafe-inline'`, and a policy that grants the first
but withholds the second fails silently, leaving the self-test showing its placeholder
rather than a visible error.
