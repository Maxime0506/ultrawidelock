<!-- generated documentation — edit the source, not this file -->
# `scripts/security-diff.sh`

security-diff.sh — the structural half of the malicious-change gate.
security/semgrep-malicious.yml asks what a diff SAYS. This asks what a diff DOES to the shape
of the tree: a binary appearing where only source lives, a file quietly gaining its executable
bit, a symlink pointing out of the checkout, a submodule nobody discussed, a capture file that
SECURITY.md says carries the session URSK. None of those are expressible as a source pattern,
because in every case the payload is opaque to a text scanner — that is the point of using
them. So they are checked here, against `git diff --raw`, which reports mode and blob type
whatever the bytes happen to be.
scripts/security-diff.sh                 # merge-base with origin/main .. HEAD
scripts/security-diff.sh <base>          # <base> .. HEAD
scripts/security-diff.sh <base> <head>   # explicit range, what CI passes
Exit 0 clean or warnings only, 1 if anything blocking was found, 2 on bad usage.
Two severities, and the split is deliberate. BLOCK is for changes with no legitimate form in
this repository — checked against the tree as it stands, which has zero symlinks, zero
gitlinks, five binary files (two in assets/, three fuzz corpus seeds) and thirty executables
that are every one of them a shell or python script. WARN is for changes that are usually
fine but are worth a reviewer's eye: a new dependency, a workflow edit, a new remote URL.
Warnings do not fail the gate, because a gate that cries wolf on a Dependabot bump is a gate
that gets bypassed, and then the blocking half goes with it.
Env:
SECDIFF_MAX_KB=512   size above which an added file is blocking (see BINARY_OK_DIRS)
NO_COLOR=1           plain output

**discussed in** [`security/README.md`](../../../security/README.md)

## API

### `block()`
`scripts/security-diff.sh:52`

Print a blocking issue to stderr in red with the given title and detail, and increment the block
counter.

**called by** `inspect`

### `warn()`
`scripts/security-diff.sh:59`

Print a warning to stderr in yellow with the given title and detail, and increment the warning
counter.

**called by** `inspect`

### `binary_ok()`
`scripts/security-diff.sh:124`

---- allowlists -----------------------------------------------------------
Directories where a binary is the expected content rather than a surprise. Kept as a prefix
list rather than a glob so a nested path cannot slip in under a matching leaf name.
bot/src/twin.wasm is named exactly, not a bot/src/* glob: it is the single WASM module
extracted from web-twin/twin.js's embedded bytes (bot/scripts/twin-wasm-extract.ts, never
hand-edited), the one way `/twin` can run that firmware inside workerd rather than a browser
(docs/twin-worker-phase0.md — workerd refuses runtime WASM codegen from bytes). Its size and
sha256 are pinned in bot/src/twin.lock.json and re-checked every run by
bot/test/twin-wasm-drift.test.ts, so a swapped or stale blob fails that gate before this one
would ever need to catch it. A bare bot/src/* entry would let any other surprise binary in
unreviewed; this does not.
bot/assets/fonts/*.ttf are the two Inter weights satori lays text out with for /matrix's PNG.
Restricted to *.ttf inside that one directory rather than bot/assets/*, so the folder cannot
become a general dumping ground. Note this is NOT covered by the assets/* prefix above, which
is anchored at the repository root.
bot/src/assets.generated.ts is text, not a blob, but trips the size gate at ~4 MB: it is
`npm run generate-assets` base64-embedding those fonts plus @resvg/resvg-wasm's WASM, so that
Wrangler and `node --test` receive byte-identical assets without a bundler in between
(bot/scripts/generate-assets.ts explains why an import rule cannot span both). Generated,
never hand-edited, and reproducible by re-running that script — which is the review path for
it, since nobody reads 4 MB of base64.

**called by** `inspect`

### `exec_ok()`
`scripts/security-diff.sh:136`

A file allowed to carry the executable bit: something with a shebang, in other words a script.
The tree's thirty executables are 23 *.sh, 4 *.py and 3 extensionless launchers under
host/presence/. Anything else gaining +x is either a compiled artifact that should not be
tracked, or a payload waiting for something to run it.

**called by** `inspect`

### `is_binary()`
`scripts/security-diff.sh:191`

is_binary PATH SHA — git's own rule, applied directly: a blob is binary if a NUL byte appears
in its first 8000 bytes. Never a guess from the extension.
Implemented by counting bytes rather than by piping git into grep, and that is not a style
choice. This script runs under `set -o pipefail`, and `git diff --no-index` exits 1 whenever the
two inputs differ — which is always, here. Under pipefail the pipeline then reports git's 1
rather than grep's 0, so the obvious `git diff … | grep -q` form returns "not binary" for every
file on earth. It read as correct and silently disabled the check.
An all-zero sha means the content exists only in the working tree (an unstaged edit, or an
untracked file), so the bytes come from disk instead of from a blob.

**called by** `inspect`

### `size_of()`
`scripts/security-diff.sh:205`

size_of PATH SHA — same split, for the same reason.

**called by** `inspect`

### `inspect()`
`scripts/security-diff.sh:215`

inspect PATH DSTMODE STATUS DSTSHA — every structural check, for one file. Factored out so an
untracked file gets exactly the same treatment as a committed one; when this lived inline, the
working-tree path would have quietly received a weaker set of checks than CI applies.

**calls** `binary_ok`, `block`, `exec_ok`, `is_binary`, `size_of`, `warn`
