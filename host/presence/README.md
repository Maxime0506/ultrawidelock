# aliro-presence — a distance-bounded second factor

Use the Aliro reader as a proximity factor for host authentication: a small USB
dongle (an ESP32-S3 + DW3000 running the presence firmware) runs an Aliro
transaction plus a UWB ranging round with a phone you already provisioned, and
tells the host "a trusted credential is within N cm right now." A PAM module
turns that into a second factor for `sudo` — no app, no pairing dance, because
the dongle carries a copy of a real door reader's identity, so your existing
Wallet credential just works.

## What this is, and is not

- It is a **distance-bounded presence factor.** The UWB ranging is
  cryptographically bound to the Aliro session (the STS keys derive from it), so
  the distance is relay-resistant — that is Aliro's whole point.
- It is **a second factor, never sole authentication.** The pam config below
  keeps your password/Touch-ID first and adds presence on top. Presence proves
  *your phone is here*, not *you are here*.
- The host side is **presence-gated policy, not key sealing.** Your login secret
  is not wrapped by the phone. Do not describe it as such. (An age/SSH keystore
  that derives wrapping material from the transaction is possible future work; it
  is deliberately out of scope for v1.)
- The USB link is treated as **hostile.** A spoofed serial device cannot assert
  presence: every assertion is HMAC-SHA256-authenticated under a key paired at
  install time, bound to a fresh per-challenge nonce (`aliro_assert.c`).

## Threat model (read before deploying)

- Anti-forgery: the pairing key gates authenticity. Keep `keyfile` root-only
  (0600). Anyone with the key can forge presence.
- Anti-replay: the host mints a fresh random nonce per challenge and accepts one
  response; a captured assertion carries a stale nonce and is rejected.
- Cache: a success is trusted for `cache_ttl_s` (default 30 s) so sudo stays
  usable. The stamp is a root-only file; an attacker who can write it is already
  root.
- Not covered: an attacker who steals *both* your unlocked phone and your first
  factor. Presence is a factor, not a panacea.

## Build & install

```sh
make            # builds build/presence/pam_aliro.so + aliro-presence-setup
make test       # runs the unit tests (also: SAN=1 ./run.sh)
sudo make install
```

Then pair the dongle and host to one key:

```sh
sudo aliro-presence-setup keygen           # writes /etc/aliro-presence/key (0600)
# copy the printed `presence-key <hex>` line onto the dongle's console once
sudo cp config.sample /etc/aliro-presence/config   # then edit device=, threshold=
```

## PAM wiring (second factor for sudo)

Linux `/etc/pam.d/sudo` — password first, presence second (both required):

```
auth       required   pam_unix.so
auth       required   pam_aliro.so config=/etc/aliro-presence/config
```

macOS `/etc/pam.d/sudo` rides the Touch-ID precedent:

```
auth       sufficient pam_tid.so
auth       required   pam_aliro.so config=/etc/aliro-presence/config
auth       required   pam_unix.so
```

Control-flag notes:

- `required` after your password = true two-factor: both must pass.
- Use `pam_aliro.so` as `sufficient` only if you intend presence to *replace* the
  password — that is a single factor and not recommended.
- On a dongle/config error the module returns `PAM_AUTHINFO_UNAVAIL`, so a
  following `required` factor still decides; it never fails *open*.

Test in a throwaway shell before editing the real sudo stack, so a mistake does
not lock you out — keep a root shell open.

## Configuration

See `config.sample`. Keys: `device`, `keyfile`, `stampfile`, `threshold_cm`
(default 40), `cache_ttl_s` (30), `timeout_ms` (3000), and optional `cred_id` to
bind to a single enrolled iPhone.

## Layout

- `aliro_presence.{c,h}` — all the logic: config, pairing key, wire framing, the
  fd transport, verification, and the freshness cache. Transport works on any fd,
  so the tests drive it over a socketpair.
- `pam_aliro.c` — a thin PAM shim over `presence_check()`.
- `aliro-presence-setup.c` — mint the pairing key; derive a `cred_id`.
- `test_presence.c` — unit tests (no hardware, no PAM).

The signed-assertion codec + verifier live in `modules/woz_aliro/aliro_assert.*`
(host-KAT'd and fuzzed in `tests/host/`).
