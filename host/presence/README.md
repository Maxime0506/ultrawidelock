# presenced

`presenced` owns one Aliro reader serial port and serves fresh, signed presence
checks over an owner-only Unix socket. `presence-run` requests one check and
executes exact argument-vector input only after the enrolled credential verifies
inside the requested distance.

This is for rare, deliberate actions. A Wallet credential requires the phone to
be awake, and a successful authentication plus UWB round takes several seconds.
Proofs are not cached.

## Run

From the repository root, close every serial monitor and pin the current board
once:

```sh
host/presence/presence-enroll --port <serial-port>
host/presence/presenced --port <serial-port>
```

The local trust store is `~/.openaliro/enrolled`, mode `0600`. It is deliberately
separate from the repository's `.presence/enrolled` release-signing policy.
`presence-enroll` refuses to change an existing trust anchor unless
`--replace` is explicitly supplied.

Enrollment is trust on first use. Run it only with the intended board directly
attached, every serial monitor closed, and VM USB passthrough disconnected. Do
not enroll through an untrusted network serial bridge.

The daemon can instead read another compatible enrollment with
`--enrolled <path>`. If that file contains multiple entries, select one with
`--key-id <device-key-id>`.

In another terminal, wake the phone and hold it near the reader:

```sh
host/presence/presence-run --max-cm 40 -- printf '%s\n' 'presence accepted'
```

The default socket is `~/.openaliro/presenced.sock`. Override it on both
processes with `--socket <path>`. The daemon creates its socket directory as
owner-only and refuses insecure, unowned, non-socket, or already-active paths.

## Security boundaries

- The daemon, not the client, generates every random challenge.
- The attached device must match the selected enrolled public key and credential
  before the socket starts listening.
- Requests can only tighten the daemon's `--max-cm` policy.
- Proof transactions are serialized. Two clients cannot share one proof or
  interleave serial traffic.
- The client never invokes a shell. Everything after `--` is passed as exact
  process arguments.
- Socket responses contain no public point, credential ID, nonce, signature, or
  raw frame.

`presence-run` is a deliberate workflow gate for the current user, not a
privilege boundary against that same user. A process that can replace the client
or command can bypass it. Privileged enforcement would require the privileged
consumer to verify or redeem the presence result itself.

Do not connect this service to LoginWindow or change macOS authentication
configuration. Test authentication consumers only in the disposable VM after
the command-gating workflow is reliable.

## Minimal VM transfer

Build the runtime-only archive from the repository root:

```sh
make presence-runtime
```

Transfer `build/presence-runtime.tar.gz` to the VM and extract it:

```sh
tar -xzf presence-runtime.tar.gz
cd presence-runtime
```

The archive contains exactly the three presence entry points, their two shared
service modules, the two existing verifier modules, and `tools/piv_pin.py` for
hidden-input PIV PIN provisioning through macOS PC/SC. It contains no firmware
source, tests, repository enrollment, local enrollment, logs, or build
configuration. The guest still needs Python, OpenSSL, and the `pyserial`
package.
