#!/usr/bin/env python3
"""Enrol the nRF5340DK initiator into a reader's home, the way Apple Home enrols an iPhone.

Usage:
  # headless, no pairing window, no phone: act as an admin that already
  # commissioned this reader (the HITL path -- see --fabric below: the
  # 2026-08-07 identity in ~/.aliro-chip-tool is chip-tool's DEFAULT
  # fabric, which is alpha, not this script's beta default)
  scripts/ultrawidelock-enroll.py --node-id 0x1234 --storage ~/.aliro-chip-tool --fabric alpha

  # first run, BLE route -- the one that works against this reader today
  scripts/ultrawidelock-enroll.py --node-id 0x1234 --pairing-code <11 digits> \
      --dataset <hex from `make monitor`>

  # later runs, already joined, default chip-tool storage
  scripts/ultrawidelock-enroll.py --node-id 0x1234

Options:
  --node-id        node id to address the reader by. Required.
  --pairing-code   Apple Home "Turn On Pairing Mode" code. Omit once joined.
  --dataset        active Thread dataset hex. Its presence selects the BLE route
                   instead of IP. Needed because PASE does not run over IP on
                   this reader -- see the comment at step 1. `make monitor`
                   prints it when a window opens.
  --discriminator  LONG 12-bit discriminator. Optional. With it, chip-tool goes
                   straight to BLE and never browses; without it, it browses,
                   finds the IP service first, and burns ~10 s on a PASE that
                   cannot succeed here. Prefer passing it.
  --endpoint       door lock endpoint (default 1)
  --fabric         chip-tool fabric name (default beta, so alpha stays free)
  --out            header to write (default examples/zephyr/nrf5340dk-initiator/src/bench_identity.h)
  --cred-type      7 evictable / 8 non-evictable endpoint key (default 8)
  --chip-tool      path to the chip-tool binary
  --fresh-storage  run against a private chip-tool KVS, so nothing an earlier
                   attempt persisted can affect this one
  --dry-run        print the chip-tool commands without running them

What it does, in the order it does it:
  1. joins the reader's fabric as a SECOND admin, leaving Apple Home's admin intact
  2. reads the four Aliro attributes that make up the reader's public identity
  3. generates a P-256 credential for the initiator and posts the public half
     with SetCredential, which is what puts it in the reader's trust store
  4. writes a C header the initiator compiles in

The reader is not modified: no firmware change, no shell command, no re-flash. Its
Apple Home fabric, Home tile and walk-up unlock all keep working. Multi-fabric is
ordinary Matter behaviour and the reader treats this admin like any other.

Why a controller is needed at all: a credential enters the trust store only
through Matter SetCredential (apps/dwm3001cdk-lock/src/matter_commission.c:1966). A phone
never provisions itself -- the home's admin does it on the phone's behalf -- so
standing in for a phone means standing in for the admin too.

The generated header carries the reader's public identity and the initiator's
PRIVATE credential key. It is written outside version control on purpose. Do not
commit it, paste it, or put it in a doc.
"""

import argparse
import os
import re
import subprocess
import tempfile
import sys

from cryptography.hazmat.primitives.asymmetric import ec

# reader_id is group_id || group_sub_id, 16 bytes each, assembled exactly this way
# by the reader itself (apps/dwm3001cdk-lock/src/matter_commission.c:1991).
ATTR_VERIFICATION_KEY = "aliro-reader-verification-key"
ATTR_GROUP_ID = "aliro-reader-group-identifier"
ATTR_GROUP_SUB_ID = "aliro-reader-group-sub-identifier"
ATTR_GRK = "aliro-group-resolving-key"

DEFAULT_CHIP_TOOL = os.path.expanduser(
    "~/esp/esp-matter/connectedhomeip/connectedhomeip/out/host/chip-tool"
)
DEFAULT_OUT = "examples/zephyr/nrf5340dk-initiator/src/bench_identity.h"

# SetCredential argument slots, per `chip-tool doorlock set-credential` usage:
#   OperationType Credential CredentialData UserIndex UserStatus UserType dest endpoint
OP_TYPE_ADD = 0
USER_STATUS_OCCUPIED_ENABLED = 1
USER_TYPE_UNRESTRICTED = 0


def decode_manual_code(code):
    """Return (short_discriminator, passcode) from an 11-digit manual pairing code.

    Field layout per the Matter spec section 5.1.3, constants read off
    connectedhomeip's src/setup_payload/SetupPayload.h rather than remembered:
    chunk1 is 1 digit, chunk2 is 5, chunk3 is 4, then a Verhoeff check digit that
    is not needed to decode (chip-tool rejects a bad code on its own).

    The passcode comes out exact. The DISCRIMINATOR DOES NOT -- a manual code
    carries only its top 4 bits, which is why `pairing ble-thread` needs the long
    one supplied separately and why chip-tool browses "_S<4 bits>". Take it from
    the board's own SRP log line, which prints D=<full 12 bits>.
    """
    digits = "".join(ch for ch in code if ch.isdigit())
    if len(digits) != 11:
        sys.exit(f"expected an 11-digit manual pairing code, got {len(digits)} digits")

    chunk1 = int(digits[0:1])
    chunk2 = int(digits[1:6])
    chunk3 = int(digits[6:10])

    short_disc = ((chunk2 >> 14) & 0x03) | ((chunk1 & 0x03) << 2)
    passcode = (chunk2 & 0x3FFF) | ((chunk3 & 0x1FFF) << 14)
    return short_disc, passcode


# Appended to every chip-tool invocation. Set once by --fresh-storage; it has
# to reach ALL of them, not just the pairing call, because the fabric the pairing
# writes and the fabric the reads authenticate with are the same storage.
EXTRA_ARGS = []


def run(cmd, dry_run):
    """Run a chip-tool invocation and return its combined output, or die loudly."""
    cmd = cmd + EXTRA_ARGS
    printable = " ".join(cmd)
    if dry_run:
        print(f"  [dry-run] {printable}")
        return ""
    print(f"  $ {os.path.basename(cmd[0])} {' '.join(cmd[1:])}")
    proc = subprocess.run(cmd, capture_output=True, text=True)
    out = proc.stdout + proc.stderr
    if proc.returncode != 0:
        sys.exit(f"chip-tool failed ({proc.returncode}):\n{out}")
    return out


def read_octets(chip_tool, fabric, attr, node_id, endpoint, dry_run, expect_len):
    """Read one octet-string attribute and return it as bytes.

    chip-tool renders an octet string as a hex blob on the report line. The value
    is NULL until Apple Home has provisioned the reader, which the reader reports
    deliberately (modules/ultrawidelock_matter/src/matter_clusters.c:482) -- an unprovisioned
    reader has no identity to hand out, and that is a legible failure rather than
    a confusing one.
    """
    out = run(
        [chip_tool, "doorlock", "read", attr, node_id, str(endpoint),
         "--commissioner-name", fabric],
        dry_run,
    )
    if dry_run:
        return b"\x00" * expect_len

    # chip-tool takes the attribute DASHED on the command line and prints it
    # CamelCase on the report line, so neither match below can use `attr`:
    #   $ chip-tool doorlock read aliro-reader-verification-key ...
    #     [TOO]   AliroReaderVerificationKey: null
    # Matching the dashed name meant the null check never fired and the failure
    # surfaced as "could not find a hex value" -- which reads as a parsing bug
    # and was in fact the device answering the question truthfully.
    label = "".join(part.capitalize() for part in attr.split("-"))

    if re.search(rf"\b{label}\s*:\s*null\b", out, re.IGNORECASE):
        sys.exit(
            f"{label} is NULL: this reader is not reporting an Aliro configuration.\n"
            "Either no home has provisioned it, or it has rebooted since one did and\n"
            "the firmware predates the boot-time restore of these attributes."
        )

    # Anchored on the label, not on any line ending in hex. The old pattern took
    # the LAST such line in the whole log, which on a busy run is not this one.
    m = re.search(rf"\b{label}\s*:\s*([0-9A-Fa-f]{{2,}})\b", out)
    if not m:
        sys.exit(f"could not find a hex value for {label} in chip-tool output:\n{out}")
    raw = bytes.fromhex(m.group(1))
    if len(raw) != expect_len:
        sys.exit(f"{attr}: expected {expect_len} bytes, got {len(raw)}")
    return raw


def c_array(name, data):
    """Render bytes as a C initialiser, 11 per line to match the repo's key tables."""
    body = ""
    for i in range(0, len(data), 11):
        chunk = ", ".join(f"0x{b:02x}" for b in data[i:i + 11])
        body += f"\t{chunk},\n"
    return f"static const uint8_t {name}[{len(data)}] = {{\n{body}}};\n"


def main():
    ap = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    ap.add_argument("--node-id", required=True)
    ap.add_argument("--pairing-code")
    ap.add_argument("--dataset", help="active Thread dataset hex; switches step 1 to BLE")
    ap.add_argument("--discriminator", type=int,
                    help="LONG 12-bit discriminator. Optional, but it skips discovery "
                         "entirely and goes straight to BLE, which is the route that works.")
    ap.add_argument("--endpoint", type=int, default=1)
    ap.add_argument("--fabric", default="beta")
    ap.add_argument("--out", default=DEFAULT_OUT)
    ap.add_argument("--cred-type", type=int, default=8, choices=(7, 8))
    ap.add_argument("--chip-tool", default=DEFAULT_CHIP_TOOL)
    ap.add_argument("--fresh-storage", action="store_true",
                    help="give chip-tool a private KVS directory, so no state "
                         "from an earlier attempt can leak into this one")
    ap.add_argument("--storage",
                    help="chip-tool storage directory holding an ALREADY-commissioned "
                         "controller identity (e.g. ~/.aliro-chip-tool). With it, no "
                         "--pairing-code is needed: the fabric in that directory is the "
                         "admin this run acts as. Mutually exclusive with --fresh-storage.")
    ap.add_argument("--dry-run", action="store_true")
    args = ap.parse_args()

    if args.storage and args.fresh_storage:
        sys.exit("--storage names a persistent identity and --fresh-storage discards one; "
                 "pick one.")

    if not args.dry_run and not os.path.isfile(args.chip_tool):
        sys.exit(f"chip-tool not found at {args.chip_tool} (pass --chip-tool)")

    # A relative --out is anchored to the REPO, not to wherever this was run
    # from. Run with scripts/ as the working directory, the old behaviour wrote
    # a perfectly valid header to scripts/ports/.../bench_identity.h -- the
    # credential went ON the reader, the header the build compiled stayed the
    # OLD one, and the device presented a key one SetCredential behind the
    # trust store. "device signature OK" followed by "NOT trusted", measured
    # 2026-08-08 00:23, and nothing anywhere pointed at a pathname.
    if not os.path.isabs(args.out):
        args.out = os.path.join(os.path.dirname(os.path.dirname(os.path.abspath(__file__))),
                                args.out)

    if args.fresh_storage:
        # chip-tool persists fabrics and node state in a KVS under $TMPDIR and
        # reuses it across processes. A run that reuses a node id therefore
        # inherits whatever the previous run left, and DeviceCommissioner
        # releasing a stale commissionee calls CloseAllBleConnections()
        # (CHIPDeviceController.cpp:635) -- which kills the BLE link that the
        # PASE about to start depends on. A private directory makes each attempt
        # independent, at the cost of a new controller identity each time.
        EXTRA_ARGS.extend(["--storage-directory", tempfile.mkdtemp(prefix="ultrawidelock-enroll-")])
        print(f"   (fresh chip-tool storage: {EXTRA_ARGS[1]})")
    elif args.storage:
        # The other direction: an identity that is meant to OUTLIVE this run.
        # $TMPDIR is purged by macOS after a few days, and a purged controller
        # identity is an orphan fabric on the reader -- removable only through a
        # pairing window, which is the human step this option exists to avoid.
        storage = os.path.expanduser(args.storage)
        if not os.path.isdir(storage):
            sys.exit(f"--storage {storage}: not a directory. It must hold a chip-tool "
                     "identity that already commissioned this reader.")
        EXTRA_ARGS.extend(["--storage-directory", storage])
        print(f"   (persistent chip-tool storage: {storage})")

    # 1. Join the fabric. Skipped on later runs: chip-tool keeps its fabric
    # credentials in its storage directory, so the node stays addressable.
    if args.pairing_code and args.dataset:
        # The BLE route. Needed because PASE does not run over IP on this reader:
        # its Thread receive path answers CASE Sigma1/Sigma3 and drops everything
        # else, so PBKDFParamRequest never reaches the PASE responder and an
        # otherwise perfect DNS-SD discovery ends in a timeout.
        #
        short_disc, passcode = decode_manual_code(args.pairing_code)
        dataset = args.dataset if args.dataset.startswith("hex:") else "hex:" + args.dataset

        if args.discriminator is not None:
            # ble-thread goes STRAIGHT to BLE: PairingCommand.cpp:132 maps
            # PairingMode::Ble to Pair(remoteId, PeerAddress::BLE()) with no
            # discovery step at all. That is the whole reason to prefer it.
            #
            # code-thread below takes PairingMode::Code -> PairWithCode
            # (PairingCommand.cpp:122), which browses every transport and tries
            # them in the order it finds them. This reader publishes
            # _matterc._udp, so IP wins the race, PASE over IP cannot succeed
            # here, and chip-tool spends ~10 s failing it while the BLE link it
            # opened in parallel sits idle -- observed dying in exactly that gap,
            # with the retry landing on a link CoreBluetooth had already given up
            # on ("Failed to write characteristic" 5 ms after connect, far too
            # fast to be an ATT error at a 15-30 ms connection interval).
            #
            # The cost is the LONG 12-bit discriminator, which a manual pairing
            # code cannot supply: it carries only the top 4 bits. Read it off the
            # board's "commissioning window open" line, or from the D= in
            # `chip-tool discover commissionables`.
            if (args.discriminator >> 8) != short_disc:
                sys.exit(f"discriminator {args.discriminator} has top nibble "
                         f"{args.discriminator >> 8}, but the pairing code decodes to "
                         f"{short_disc}. They come from different commissioning windows; "
                         f"the older one has closed. Reopen pairing mode and take both again.")
            print(f"1. joining over BLE, direct (discriminator {args.discriminator})")
            run(
                [args.chip_tool, "pairing", "ble-thread", args.node_id, dataset,
                 str(passcode), str(args.discriminator),
                 "--commissioner-name", args.fabric],
                args.dry_run,
            )
        else:
            print(f"1. joining via discovery (code decodes to short discriminator "
                  f"{short_disc}; pass --discriminator to skip the IP detour)")
            run(
                [args.chip_tool, "pairing", "code-thread", args.node_id, dataset,
                 args.pairing_code, "--commissioner-name", args.fabric],
                args.dry_run,
            )
    elif args.pairing_code:
        print("1. joining the reader's fabric as a second admin (over IP)")
        run(
            [args.chip_tool, "pairing", "code", args.node_id, args.pairing_code,
             "--commissioner-name", args.fabric],
            args.dry_run,
        )
    else:
        print("1. skipping commissioning (no --pairing-code); assuming already joined")

    # 2. Read the reader's public identity.
    print("2. reading the reader's Aliro identity")
    verif = read_octets(args.chip_tool, args.fabric, ATTR_VERIFICATION_KEY,
                        args.node_id, args.endpoint, args.dry_run, 65)
    gid = read_octets(args.chip_tool, args.fabric, ATTR_GROUP_ID,
                      args.node_id, args.endpoint, args.dry_run, 16)
    gsid = read_octets(args.chip_tool, args.fabric, ATTR_GROUP_SUB_ID,
                       args.node_id, args.endpoint, args.dry_run, 16)
    grk = read_octets(args.chip_tool, args.fabric, ATTR_GRK,
                      args.node_id, args.endpoint, args.dry_run, 16)
    reader_id = gid + gsid

    # 3. Mint the initiator's credential and hand the public half to the reader.
    #
    # Type 6 (issuer key) is the trap: the reader ACCEPTS it, reports a trust
    # anchor, and then rejects every device one step after "device signature OK",
    # because an issuer key identifies the home and no device ever presents it
    # (apps/dwm3001cdk-lock/src/matter_commission.c:1940-1957, measured across three pairings).
    # Only the two endpoint types, 7 and 8, become anchors.
    print("3. minting the initiator credential and posting it with SetCredential")
    key = ec.generate_private_key(ec.SECP256R1())
    priv = key.private_numbers().private_value.to_bytes(32, "big")
    pub = key.public_key().public_numbers()
    cred_pub = b"\x04" + pub.x.to_bytes(32, "big") + pub.y.to_bytes(32, "big")

    credential = f'{{"credentialType": {args.cred_type}, "credentialIndex": 1}}'
    run(
        [args.chip_tool, "doorlock", "set-credential",
         str(OP_TYPE_ADD), credential, "hex:" + cred_pub.hex(),
         "null", str(USER_STATUS_OCCUPIED_ENABLED), str(USER_TYPE_UNRESTRICTED),
         args.node_id, str(args.endpoint),
         "--commissioner-name", args.fabric, "--timedInteractionTimeoutMs", "5000"],
        args.dry_run,
    )

    # 4. Emit the header. Written, not printed: the private key should not end up
    # in a terminal scrollback that outlives the bench session.
    print(f"4. writing {args.out}")
    header = (
        "/* GENERATED by scripts/ultrawidelock-enroll.py -- do not edit, do not commit.\n"
        " *\n"
        " * The reader's public identity, read back over Matter, plus the private\n"
        " * credential key this initiator authenticates with. Regenerate whenever the\n"
        " * reader is re-provisioned: every value here dies with its home.\n"
        " */\n"
        "#ifndef ALIRO_BENCH_IDENTITY_H\n"
        "#define ALIRO_BENCH_IDENTITY_H\n\n"
        "#include <stdint.h>\n\n"
        "/* group_id || group_sub_id, assembled as the reader assembles it. */\n"
        + c_array("k_reader_id", reader_id)
        + "\n" + c_array("k_reader_verif_pub", verif)
        + "\n/* Resolves the reader's advert tag on approach; unused until Stage 1b. */\n"
        + c_array("k_group_resolving_key", grk)
        + "\n" + c_array("k_cred_priv", priv)
        + "\n#endif /* ALIRO_BENCH_IDENTITY_H */\n"
    )
    if args.dry_run:
        print("  [dry-run] header not written")
    else:
        os.makedirs(os.path.dirname(args.out), exist_ok=True)
        with open(args.out, "w") as f:
            f.write(header)
        os.chmod(args.out, 0o600)

    print("\ndone. The reader now trusts this initiator and its identity is in the header.")
    print("That header holds a private key: it is gitignored, keep it that way.")
    # The header is detected at CMake configure time, not by the preprocessor, so
    # a plain incremental build would not notice it appearing. See the app's
    # CMakeLists.txt for why it is not __has_include.
    print("\nRebuild with -p always (or delete the build directory) so CMake sees it,")
    print("then flash. The boot log should say 'identity: ENROLLED'.")


if __name__ == "__main__":
    main()
