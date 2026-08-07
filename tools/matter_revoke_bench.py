#!/usr/bin/env python3
# Copyright (c) 2026 asxeem
# SPDX-License-Identifier: ISC
"""Drive ClearCredential and ClearUser at a running lock, over a PASE session.

Revocation is the one part of the Matter surface no walk-up can exercise: the
commands only ever arrive from an admin, and the only admin this lock has is
Apple Home, which sends them when it feels like it. This sends them on demand.

  python3 tools/matter_revoke_bench.py --dry-run          encode only, no board
  python3 tools/matter_revoke_bench.py 1234-567-8901      both proofs
  python3 tools/matter_revoke_bench.py <code> --only A    ClearCredential only
  python3 tools/matter_revoke_bench.py <code> --only B    ClearUser only

The code is what Apple Home shows under the accessory's "Turn On Pairing Mode"
(11 digits; dashes optional). Options: --endpoint (default 1), --only, --index
and --user pick the slots to use, --storage points the controller's key store
somewhere other than beside this file, --expected-endpoint-keys turns the key
count the lock reports into a pass/fail check (it is per-target, so there is no
default worth having).

IT NEVER COMMISSIONS. A commissioning window is a PASE responder, so this opens
a PASE session and invokes over that -- no AddNOC, no second fabric consumed,
nothing to undo, and the window closes on its own timeout. That matters on a
board with two fabric slots and one already spent on Apple.

Needs the CHIP controller stack, which ships as wheels and needs no SDK build:

  python3 -m venv /tmp/mctl
  /tmp/mctl/bin/pip install home-assistant-chip-clusters home-assistant-chip-core
  /tmp/mctl/bin/python tools/matter_revoke_bench.py --dry-run

Run `make monitor` alongside: the lock logs "ALIRO CREDENTIAL ADDED" on the
install and "ALIRO CLEAR ... REVOKED" on the removal, and those lines are the
actual evidence. This script only proves the commands were accepted.
"""

import argparse
import asyncio
import os
import sys

DL = None  # bound in _load(), so --help works without the CHIP wheels installed
NODE_ID = 0xBEEF

# A throwaway uncompressed P-256 point: 0x04 || X || Y, the NIST generator. The
# lock keeps it as an opaque 65-byte verification key, so it never has to be on
# the curve -- but a real point costs nothing and keeps this honest if the
# firmware ever starts validating.
TEST_KEY = bytes.fromhex(
    "04"
    "6b17d1f2e12c4247f8bce6e563a440f277037d812deb33a0f4a13945d898c296"
    "4fe342e2fe1a7f9b8ee7eb4a7c0f9e162bce33576b315ececbb6406837bf51f5"
)

# The two proofs need DIFFERENT keys. A trust store that already holds a key
# refuses it a second time (`credential type 7 REFUSED (-1)`), so reusing one
# key means the second install silently never happens and the removal that
# follows has nothing to remove. These are 2G and 3G on the same curve.
TEST_KEY_2 = bytes.fromhex(
    "04"
    "7cf27b188d034f7e8a52380304b51ac3c08969e277f21b35a60b48fc47669978"
    "07775510db8ed040293d9ac69f7430dbba7dade63ce982299e04b79d227873d1"
)
TEST_KEY_3 = bytes.fromhex(
    "04"
    "5ecbe4d1a6330a44c8f7ef951d4bf165e6c6b721efada985fb41661bc6e7fd6c"
    "8734640c4998ff7e374b06ce1a64a2ecd82ab036384fb83d9a79b127a27d5032"
)


def _load():
    """Import the CHIP controller stack, or explain what to install."""
    global DL
    try:
        from chip import clusters as Clusters
    except ImportError:
        sys.exit("no CHIP controller stack: pip install home-assistant-chip-clusters "
                 "home-assistant-chip-core (see the header of this file)")
    DL = Clusters.DoorLock


def parse_code(code):
    """Split a manual pairing code into (passcode, discriminator)."""
    from chip.setup_payload import SetupPayload

    payload = SetupPayload().ParseManualPairingCode(code.replace("-", "").replace(" ", ""))
    attrs = payload.attributes
    # A manual code carries only the top 4 bits, so this is a short discriminator
    # shifted into place: enough to recognise the advert, not to open a session.
    # --long-discriminator overrides it with the 12 bits the advert carries.
    if "Discriminator" in attrs:
        return int(attrs["SetUpPINCode"]), int(attrs["Discriminator"])
    return int(attrs["SetUpPINCode"]), int(attrs["Short discriminator"]) << 8


class Bench:
    """The two proofs, against either a real controller or the dry-run stand-in."""

    def __init__(self, ctrl, endpoint):
        self.ctrl = ctrl
        self.ep = endpoint
        self.failures = []

    async def cmd(self, what, payload):
        """One timed invoke. Returns (sent, response).

        `sent` is False only when the invoke itself failed -- no session, timeout,
        a refusing IM status raised as an exception. It is NOT the same thing as a
        response the caller dislikes, and it has to be reported separately: a
        command that never landed answers None, and so does a command that landed
        and returned no payload. A caller that cannot tell those apart reads a dead
        link as a lock saying no.

        A refusal is recorded, never raised: the later steps still say something
        useful about a lock that rejected an earlier one.
        """
        try:
            resp = await self.ctrl.SendCommand(NODE_ID, self.ep, payload,
                                               timedRequestTimeoutMs=3000)
            print(f"  {what}: SUCCESS" if resp is None else f"  {what}: {resp}")
            return True, resp
        except Exception as exc:  # noqa: BLE001 -- any refusal is a failed step
            print(f"  {what}: REFUSED -- {type(exc).__name__}: {exc}")
            self.failures.append(what)
            return False, None

    def check(self, what, ok):
        print(f"  {'PASS' if ok else 'FAIL'}: {what}")
        if not ok:
            self.failures.append(what)

    def credential(self, index):
        return DL.Structs.CredentialStruct(
            credentialType=DL.Enums.CredentialTypeEnum.kAliroEvictableEndpointKey,
            credentialIndex=index)

    async def caps(self, expected=None):
        """Report the two counts; assert one only when the caller named a value.

        The number is board-specific, not a constant of the protocol: the nRF
        Matter node reports ALIRO_TRUST_MAX (6), the ESP32 delegate reports its own
        kAliroKeysSupported (10). Failing a bench run against a lock that is
        truthfully reporting 10 tells nobody anything, so --expected-endpoint-keys
        is what turns this into a check.
        """
        print("[caps] the Aliro key counts the lock reports")
        got = await self.ctrl.ReadAttribute(NODE_ID, [
            (self.ep, DL.Attributes.NumberOfAliroCredentialIssuerKeysSupported),
            (self.ep, DL.Attributes.NumberOfAliroEndpointKeysSupported),
        ])
        ep = got[self.ep][DL]
        print(f"  issuer keys supported   : "
              f"{ep[DL.Attributes.NumberOfAliroCredentialIssuerKeysSupported]}")
        endpoint_keys = ep[DL.Attributes.NumberOfAliroEndpointKeysSupported]
        print(f"  endpoint keys supported : {endpoint_keys}")
        if expected is not None:
            self.check(f"endpoint keys reported == {expected}", endpoint_keys == expected)

    async def install(self, user_index, cred_index, key=TEST_KEY):
        """SetUser then SetCredential, which is the order Apple uses: this node
        wants an explicit user index and never allocates one itself."""
        await self.cmd(f"SetUser({user_index})", DL.Commands.SetUser(
            operationType=DL.Enums.DataOperationTypeEnum.kAdd,
            userIndex=user_index, userName="bench", userUniqueID=0x42,
            userStatus=DL.Enums.UserStatusEnum.kOccupiedEnabled,
            userType=DL.Enums.UserTypeEnum.kUnrestrictedUser,
            credentialRule=DL.Enums.CredentialRuleEnum.kSingle))
        sent, resp = await self.cmd(
            f"SetCredential(type 7, index {cred_index}, user {user_index})",
            DL.Commands.SetCredential(
                operationType=DL.Enums.DataOperationTypeEnum.kAdd,
                credential=self.credential(cred_index), credentialData=key,
                userIndex=user_index,
                userStatus=DL.Enums.UserStatusEnum.kOccupiedEnabled,
                userType=DL.Enums.UserTypeEnum.kUnrestrictedUser))
        self.check(f"credential installed at index {cred_index}",
                   sent and getattr(resp, "status", None) is not None
                   and int(resp.status) == 0)

    async def proof_a(self, user_index, cred_index):
        print("\n[A] ClearCredential revokes the credential it names")
        await self.install(user_index, cred_index, TEST_KEY_2)
        before = len(self.failures)
        cred = self.credential(cred_index)
        await self.cmd(f"ClearCredential(7, {cred_index})",
                       DL.Commands.ClearCredential(credential=cred))
        await self.cmd(f"ClearCredential(7, {cred_index}) again",
                       DL.Commands.ClearCredential(credential=cred))
        self.check("both clears accepted -- the second is the idempotence case",
                   len(self.failures) == before)
        await self.cmd(f"ClearUser({user_index}) tidy-up",
                       DL.Commands.ClearUser(userIndex=user_index))

    async def proof_b(self, user_index, cred_index):
        print("\n[B] ClearUser revokes the credentials bound to that user")
        await self.install(user_index, cred_index, TEST_KEY_3)
        before = len(self.failures)
        await self.cmd(f"ClearUser({user_index})", DL.Commands.ClearUser(userIndex=user_index))
        self.check("clear accepted", len(self.failures) == before)


class DryCtrl:
    """Encodes every payload the bench would send, with no board and no radio.

    Worth having as more than a smoke test: the bytes it prints are the ones
    tests/host/test_matter_clusters.c replays through the firmware's decoder, so
    a field renumbered upstream shows up here before it shows up on a bench.
    """

    async def SendCommand(self, node, endpoint, payload, **kw):  # noqa: N802 -- SDK spelling
        raw = bytes(payload.ToTLV())
        print(f"    [dry] cluster 0x{payload.cluster_id:04x} cmd 0x{payload.command_id:04x} "
              f"{len(raw)} B: {raw.hex()}")
        if isinstance(payload, DL.Commands.SetCredential):
            return DL.Commands.SetCredentialResponse(status=0, userIndex=payload.userIndex,
                                                     nextCredentialIndex=None)
        return None

    async def ReadAttribute(self, node, paths, **kw):  # noqa: N802 -- SDK spelling
        return {paths[0][0]: {DL: {
            DL.Attributes.NumberOfAliroCredentialIssuerKeysSupported: 10,
            DL.Attributes.NumberOfAliroEndpointKeysSupported: 6,
        }}}


def verdict(bench, tail=""):
    print()
    if bench.failures:
        print("VERDICT: FAIL")
        for f in bench.failures:
            print(f"  - {f}")
        return 1
    print(f"VERDICT: PASS{tail}")
    return 0


async def run(args):
    bench = Bench(DryCtrl(), args.endpoint) if args.dry_run else None
    if bench is not None:
        await bench.caps(args.expected_endpoint_keys)
        await bench.proof_a(args.user, args.index)
        await bench.proof_b(args.user + 1, args.index + 1)
        return verdict(bench, " (encoding only -- nothing was sent to a board)")

    import chip.native
    from chip.CertificateAuthority import CertificateAuthorityManager
    from chip.ChipStack import ChipStack

    # Init first: SetupPayload reaches for the same library handle the stack
    # owns, so parsing the code before this raises "handle not initialized".
    chip.native.Init()

    passcode, discriminator = parse_code(args.code)
    if args.long_discriminator is not None:
        discriminator = args.long_discriminator
    print(f"pairing code parsed: discriminator 0x{discriminator:03x}")

    stack = ChipStack(persistentStoragePath=args.storage, enableServerInteractions=False)
    cam = CertificateAuthorityManager(chipStack=stack)
    cam.LoadAuthoritiesFromStorage()
    if not cam.activeCaList:
        cam.NewCertificateAuthority().NewFabricAdmin(vendorId=0xFFF1, fabricId=1)
    ctrl = cam.activeCaList[0].adminList[0].NewController(nodeId=112233)
    bench = Bench(ctrl, args.endpoint)

    try:
        print("opening PASE over BLE -- the commissioning window must be open ...")
        await ctrl.EstablishPASESessionBLE(passcode, discriminator, NODE_ID)
        print("PASE up")
        await bench.caps(args.expected_endpoint_keys)
        if args.only in (None, "A"):
            await bench.proof_a(args.user, args.index)
        if args.only in (None, "B"):
            await bench.proof_b(args.user + 1, args.index + 1)
    except Exception as exc:  # noqa: BLE001 -- report, never traceback at a bench
        bench.failures.append(f"session: {type(exc).__name__}: {exc}")
        print(f"FAILED: {type(exc).__name__}: {exc}")
    finally:
        try:
            await ctrl.UnpairDevice(NODE_ID)
        except Exception:  # noqa: BLE001 -- best-effort teardown
            pass
        ctrl.Shutdown()
        stack.Shutdown()

    return verdict(bench, " -- now power-cycle the board and check the boot line reports "
                          "the anchor count it had before this ran")


def main():
    here = os.path.dirname(os.path.abspath(__file__))
    ap = argparse.ArgumentParser(description=__doc__,
                                 formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("code", nargs="?", default="",
                    help="manual pairing code from Apple Home's pairing mode")
    ap.add_argument("--dry-run", action="store_true",
                    help="encode every command and stop; needs no board")
    ap.add_argument("--only", choices=("A", "B"), help="run one proof instead of both")
    ap.add_argument("--endpoint", type=int, default=1, help="lock endpoint (default 1)")
    # A manual pairing code carries only the top 4 bits of the discriminator,
    # and the BLE session wants all 12. The other 8 are in the advert, under
    # service 0xFFF6: bytes 1-2, little-endian, low 12 bits.
    ap.add_argument("--long-discriminator", type=int,
                    help="full 12-bit discriminator, read from the 0xFFF6 advert")
    ap.add_argument("--index", type=int, default=9, help="first credential index to use")
    ap.add_argument("--user", type=int, default=5, help="first user index to use")
    ap.add_argument("--storage", default=os.path.join(here, "matter-bench-store.json"),
                    help="controller key store (holds a throwaway CA)")
    # Not defaulted to 6: the count is per-target (nRF reports ALIRO_TRUST_MAX,
    # the ESP32 delegate reports 10), so a fixed expectation would fail a lock
    # that is answering correctly.
    ap.add_argument("--expected-endpoint-keys", type=int,
                    help="fail unless NumberOfAliroEndpointKeysSupported reports this "
                         "(nRF Matter node: 6; ESP32 delegate: 10)")
    args = ap.parse_args()
    if not args.dry_run and not args.code:
        ap.error("a pairing code is required unless --dry-run is given")
    _load()
    sys.exit(asyncio.run(run(args)))


if __name__ == "__main__":
    main()
