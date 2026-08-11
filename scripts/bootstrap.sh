#!/usr/bin/env bash
#
# bootstrap.sh — build a self-contained west workspace, PRISTINE from upstream.
#
# Fetches everything the build needs from public GitHub into ./workspace
# (git-ignored), then applies our integration patches on top. It never reads from
# any other local checkout — a clean upstream fetch every time.
#
# Fetches (all public):
#   - Nordic add-on  ncs-door-lock-and-access-control @ the pin below
#   - NCS v3.3.0 + Zephyr + every module (via the add-on's own west manifest)
#
# The NCS v3.3.0 toolchain it needs is installed here too, once per machine, so
# a clone reaches a build in one command instead of three.
#
# Usage:  scripts/bootstrap.sh                       # workspace in ./workspace
#         ALIRO_WS=/big/disk/ws scripts/bootstrap.sh # put the multi-GB workspace elsewhere
set -euo pipefail

TREE="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
WS="${ALIRO_WS:-$TREE/workspace}"
NCS_VER="${NCS_VER:-v3.3.0}"
PIN="a5ad7fde1041d81690710a949c98eda1985fee0b"     # ncs-door-lock-and-access-control (public)
ADDON_URL="https://github.com/nrfconnect/ncs-door-lock-and-access-control"
ADDON="$WS/ncs-door-lock-and-access-control"
P="$TREE/integrations/nrfconnect-door-lock/patches"
PATCH_STATE="$WS/.openaliro-patches.sha256"

# Launch the nRF Util SDK manager toolchain with the configured NCS version, passing through all remaining arguments.
# ALIRO_TOOLCHAIN=env skips that wrapper and runs the command directly — for
# environments with the toolchain already on PATH (the NCS toolchain container
# in CI, where nrfutil's toolchain index is not reachable).
if [ "${ALIRO_TOOLCHAIN:-}" = env ]; then
  launch() { "$@"; }
else
  # Execute a command inside the nRF Connect SDK toolchain environment for NCS_VER, forwarding all arguments.
  # Wrapper around `nrfutil sdk-manager toolchain launch`.
  launch() { nrfutil sdk-manager toolchain launch --ncs-version "$NCS_VER" -- "$@"; }
fi

# 0. The NCS toolchain (compiler, west, ccache — about 2 GB), once per machine.
#    This was the one prerequisite the script documented and did not do, which
#    left a manual step in the middle of getting from a clone to a build. Its
#    cost when skipped was not the typing: it surfaced as a failure AFTER the
#    multi-GB fetch below, which is the worst place to learn about it.
#
#    Safe to run every time. `toolchain list` prints one row per installed
#    version, so an existing toolchain costs a query and nothing else.
#
#    It asks nrfutil rather than looking at a path, which is what makes a
#    toolchain installed somewhere unusual findable: `list` reports whatever is
#    in nrfutil's configured install-dir (`nrfutil sdk-manager config show`),
#    default or not. And it cannot disagree with the build, because build.sh
#    reaches the compiler the same way — `toolchain launch` resolves through the
#    same configuration. A toolchain nrfutil cannot see is one no build here
#    could have used either.
#
#    ALIRO_TOOLCHAIN=env is the way out for a toolchain nrfutil does not manage
#    at all: it means one is already on PATH — the NCS container CI builds in,
#    where nrfutil's toolchain index is not even reachable — so there is nothing
#    to install and nothing to ask. NO_TOOLCHAIN=1 skips just this phase.
#
#    --styling never because this output is parsed, not read: colour is off when
#    piped today, and that is a default rather than a promise.
if [ "${ALIRO_TOOLCHAIN:-}" != env ] && [ -z "${NO_TOOLCHAIN:-}" ]; then
  echo "==> NCS $NCS_VER toolchain"
  if ! command -v nrfutil >/dev/null 2>&1; then
    echo "ERROR: nrfutil not found on PATH — it is what installs the toolchain." >&2
    echo "       get it: https://www.nordicsemi.com/Products/Development-tools/nrf-util" >&2
    echo "       already have a Zephyr toolchain on PATH? ALIRO_TOOLCHAIN=env make bootstrap" >&2
    exit 1
  fi
  if nrfutil sdk-manager toolchain list --styling never 2>/dev/null \
     | grep -q "^${NCS_VER}[[:space:]]"; then
    echo "    already installed — nothing to fetch"
  else
    # If that match ever goes stale, this is the cost: `install` without
    # --force does not replace an installation that is already there (that is
    # what --force is documented to do), so the fallback is a no-op, not a
    # repeat download.
    echo "    installing (~2 GB, once per machine)"
    nrfutil sdk-manager toolchain install --ncs-version "$NCS_VER"
  fi
fi

# 1. Fetch pristine upstream into $WS. A sentinel marks a completed fetch so an
#    interrupted `west update` resumes on the next run — without it, a partial
#    fetch would be skipped forever because the add-on clone already exists.
FETCHED="$WS/.aliro-fetch-done"
echo "==> workspace: $WS   (add-on pin ${PIN:0:10}…, NCS $NCS_VER)"
if [ ! -d "$ADDON/.git" ]; then
  # Clone the manifest repo, checkout the pinned SHA, then `west init -l`.
  # (`west init -m … --mr <SHA>` is wrong: it runs `git clone --branch <SHA>`, and
  #  --branch only accepts a tag or branch name, never a commit SHA.)
  mkdir -p "$WS"
  if [ "${ALIRO_SHALLOW:-0}" = 1 ]; then
    # Pinned-SHA shallow fetch (`git clone --depth` would need PIN at a tip).
    git init -q "$ADDON"
    git -C "$ADDON" remote add origin "$ADDON_URL"
    git -C "$ADDON" fetch -q --depth 1 origin "$PIN"
    git -C "$ADDON" checkout -q FETCH_HEAD
  else
    git clone -q "$ADDON_URL" "$ADDON"
    git -C "$ADDON" checkout -q "$PIN"
  fi
  launch west init -l "$ADDON"
fi
if [ ! -f "$FETCHED" ]; then
  echo "    west update — fetching NCS + modules from GitHub (multi-GB on a first run)"
  # ALIRO_SHALLOW=1 fetches exactly the pinned revisions with no history —
  # same tree state, a fraction of the size. For CI; the bench default keeps
  # full clones (git archaeology in the workspace stays possible).
  if [ "${ALIRO_SHALLOW:-0}" = 1 ]; then
    ( cd "$WS" && launch west update --narrow -o=--depth=1 )
  else
    ( cd "$WS" && launch west update )
  fi
  touch "$FETCHED"
else
  echo "    already fetched — reusing (delete $WS for a clean re-fetch)"
fi

# 2. Apply our patches on top. Each target repo is reset to its pinned HEAD and
#    verified clean first, so a patch can never land on unexpected local state.
echo "==> applying integration patches"
# Apply patch files to a repository, resetting it to its pinned HEAD first.
#
# That reset is what makes bootstrap idempotent -- the previous run's patches have
# to come off before this run's go on -- but hand-editing $WS is the normal way
# upstream gets debugged here, and those edits look identical to it. So say what is
# about to go, and keep a copy: a run that silently eats an afternoon of debugging
# is the worst thing this script can do. ALIRO_KEEP_WS_EDITS=1 stops instead, for
# when the edits are the point and re-patching is not.
apply_to() {   # $1 = repo, remaining args = patch files
  local repo="$1"; shift
  local dirty saved
  dirty="$(git -C "$repo" status --porcelain --untracked-files=no)"
  if [ -n "$dirty" ]; then
    if [ "${ALIRO_KEEP_WS_EDITS:-0}" = 1 ]; then
      echo "ERROR: $repo has local changes and ALIRO_KEEP_WS_EDITS=1 — stopping" >&2
      printf '%s\n' "$dirty" | sed 's/^/      /' >&2
      exit 1
    fi
    mkdir -p "$WS/.aliro-discarded"
    saved="$WS/.aliro-discarded/$(basename "$repo")-$(date -u +%Y%m%dT%H%M%SZ).patch"
    # diff HEAD, not diff: staged work is just as easy to lose and `checkout -- .`
    # does not touch the index, so it would otherwise survive here and trip the
    # pristine assertion below with no record of what it was.
    git -C "$repo" diff HEAD >"$saved"
    echo "    discarding local changes in $repo:"
    printf '%s\n' "$dirty" | sed 's/^/      /'
    echo "    saved to $saved"
    echo "      restore with: git -C $repo apply '$saved'"
  fi
  git -C "$repo" checkout -q -- .
  # Reachable, despite the reset above: `checkout -- .` rewrites the working tree
  # from the index and leaves the index itself alone, so anything staged survives
  # to here. Staged work is deliberate work, so stop rather than clear it -- the
  # copy saved above is the way back if the stop is unwelcome.
  [ -z "$(git -C "$repo" status --porcelain --untracked-files=no)" ] || {
    echo "ERROR: $repo still not pristine — staged changes survive 'checkout -- .'" >&2
    echo "       Unstage them and re-run:  git -C $repo reset" >&2
    exit 1
  }
  git -C "$repo" apply --whitespace=nowarn "$@"
}
# HA=1 also applies the Home Assistant data-model patches: the DoorLock
# LockOperation event and the UWB-proximity occupancy endpoint. Off by default
# because they change the Matter data model of an already-commissioned lock, and
# that has not been validated on hardware. They are a pair, not independent: the
# occupancy patch is cut against a tree with the LockOperation one applied, so
# applying it alone will not apply cleanly. Pair with `make build HA=1`.
ha_patches=()
if [ "${HA:-0}" = 1 ]; then
  ha_patches=("$P/ha-lockoperation-event.patch" "$P/ha-occupancy-endpoint.patch")
fi

apply_to "$ADDON"                 "$P/custom_impl-uwb.patch" "$P/crypto-timesync-tap.patch" "$P/pretty-shell.patch" "$P/aliro-shell-factoryreset.patch" "$P/console-quiet-flood.patch" "$P/kpersistent-orphan-selfheal.patch" "$P/aliro-doc-time-ratchet.patch" "$P/aliro-time-persist.patch" "$P/extnvs-rollback-mirror-id.patch" "$P/approach-direction-cluster.patch" "$P/nfc-transport-seam.patch" ${ha_patches[@]+"${ha_patches[@]}"}
apply_to "$WS/nrf"                "$P/nrf-flashfit-dfu-guards.patch"
apply_to "$WS/modules/lib/matter" "$P/matter-ble-multi-identity.patch"

# A dirty repository proves that some patch exists, not that it is today's
# patch set. Record the exact patch contents and optional HA mode so build.sh
# rejects a workspace left behind by an older checkout.
"$TREE/scripts/integration-patch-id.py" "$P" "${HA:-0}" >"$PATCH_STATE"

# Calculate number of patches applied to $ADDON (always 11 base patches plus the HA patches if HA=1)
addon_patch_count=$((11 + ${#ha_patches[@]}))
total_patch_count=$((13 + ${#ha_patches[@]}))

echo "    ✓ pristine upstream + $total_patch_count patches (add-on ×$addon_patch_count, nrf, matter)"

echo "==> ready. Build with:  make build"
