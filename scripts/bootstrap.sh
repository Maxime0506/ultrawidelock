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
# Prereq (once per machine): nRF Connect SDK v3.3.0 toolchain
#   nrfutil sdk-manager toolchain install --ncs-version v3.3.0
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
P="$TREE/ports/nrf5340dk/patches"

# Launch the nRF Util SDK manager toolchain with the configured NCS version, passing through all remaining arguments.
# ALIRO_TOOLCHAIN=env skips that wrapper and runs the command directly — for
# environments with the toolchain already on PATH (the NCS toolchain container
# in CI, where nrfutil's toolchain index is not reachable).
if [ "${ALIRO_TOOLCHAIN:-}" = env ]; then
  launch() { "$@"; }
else
  launch() { nrfutil sdk-manager toolchain launch --ncs-version "$NCS_VER" -- "$@"; }
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
# Apply patch files to a repository, ensuring it is pristine (no uncommitted changes) before patching.
apply_to() {   # $1 = repo, remaining args = patch files
  local repo="$1"; shift
  git -C "$repo" checkout -q -- .
  [ -z "$(git -C "$repo" status --porcelain --untracked-files=no)" ] \
    || { echo "ERROR: $repo not pristine — refusing to patch"; exit 1; }
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

apply_to "$ADDON"                 "$P/custom_impl-uwb.patch" "$P/crypto-timesync-tap.patch" "$P/pretty-shell.patch" "$P/console-quiet-flood.patch" "$P/kpersistent-orphan-selfheal.patch" "$P/aliro-doc-time-ratchet.patch" "$P/aliro-time-persist.patch" "$P/extnvs-rollback-mirror-id.patch" "$P/approach-direction-cluster.patch" "$P/nfc-transport-seam.patch" ${ha_patches[@]+"${ha_patches[@]}"}
apply_to "$WS/nrf"                "$P/nrf-flashfit-dfu-guards.patch"
apply_to "$WS/modules/lib/matter" "$P/matter-ble-multi-identity.patch"

# Calculate number of patches applied to $ADDON (always 10 base patches plus the HA patches if HA=1)
addon_patch_count=$((10 + ${#ha_patches[@]}))
total_patch_count=$((12 + ${#ha_patches[@]}))

echo "    ✓ pristine upstream + $total_patch_count patches (add-on ×$addon_patch_count, nrf, matter)"

echo "==> ready. Build with:  make build"
