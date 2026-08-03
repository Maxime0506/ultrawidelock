<!-- generated documentation — edit the source, not this file -->
# `scripts/security-ct.sh`

security-ct.sh — secret-dependent branches and table lookups in the CCC key ladder.
Every other gate in this repo asks whether the code computes the right answer. This one asks
whether it takes the same amount of time doing it, which no test, no sanitizer and no fuzzer in
the tree can see: a KDF that early-outs on a key byte passes every existing check with a green
tick, and hands an attacker the key one byte at a time.
The mechanism is ctgrind's, and it is almost free. Memcheck already reports a branch or an
array index that depends on undefined memory. Poison the URSK instead of leaving it
uninitialised and that same report becomes "this branched on the key". Nothing new is
instrumented; the harness (tests/host/ct/ct_main.c) just marks the secret and runs the ladder.
Scope, stated up front because a green run means nothing without it: the AES primitive is
suppressed. tests/host/aes_ref.c is an S-box implementation and is variable-time by
construction, and it is not the primitive that ships — nRF5340 uses CryptoCell through PSA,
ESP32 uses mbedTLS over the AES peripheral. So this gate covers the ladder and the SP0 wrapper,
which is the code this project wrote, and says nothing about the cipher underneath, which it
did not. tests/host/ct/host-aes.supp is where that boundary is drawn.
scripts/security-ct.sh          # build + run under memcheck
CT_DOCKER=1 scripts/security-ct.sh
make security-ct
On Apple silicon there is no valgrind, and there will not be one. That is a real hole in the
pre-push sweep rather than something to paper over, so the script says so on stdout, exits 2
(distinct from a finding's 1), and offers CT_DOCKER=1 to run the identical command inside the
linux/amd64 image CI uses. verify.sh turns the 2 into a row that reads "not run here, runs in
CI", the same shape cbmc already has.
Env:
CT_DOCKER=1     run inside docker (linux/amd64) instead of natively
CT_CC=clang     compiler (default: cc)
NO_COLOR=1      plain output

**discussed in** [`security/README.md`](../../../security/README.md)

## API

### `have()`
`scripts/security-ct.sh:51`

Test whether a command is available in PATH.
