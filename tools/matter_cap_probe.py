#!/usr/bin/env python3
# Copyright (c) 2026 asxeem
# SPDX-License-Identifier: ISC
"""Add Aliro endpoint keys to a running lock until one is refused.

  python3 tools/matter_cap_probe.py --dry-run            check the key generator
  python3 tools/matter_cap_probe.py 1234-567-8901        walk into the ceiling
  python3 tools/matter_cap_probe.py <code> --long-discriminator 1459
  python3 tools/matter_cap_probe.py <code> --pre-clear 20 21

The lock advertises `ALIRO_TRUST_MAX` endpoint keys and cannot necessarily
persist that many. On the DWM3001CDK the settings partition is one 4096-byte
NVS sector shared with the fabric and the Thread dataset, and garbage collection
carries the OLD blob forward before the NEW one lands, so a write needs room for
both at once. The ceiling that follows is not a constant: it moves as ordinary
Matter traffic changes how much else is live in that sector. This measures where
it is today, and puts the store back afterwards.

Options: `--user` and `--index` pick the first slot pair to use, `--count` how
many to try, `--base` the anchor count already in the store so the running total
printed is the real one, `--pre-clear` removes credential indices a previous
interrupted run left behind, `--storage` moves the controller's key store.

Run `make monitor` alongside. The refusal the lock prints is the evidence:

    E:   credential type 7 REFUSED (-28)

-28 is -ENOSPC, and it is legible only because the provisioning paths propagate
the store's errno instead of collapsing it to -1. Anything else -- -1 above all
-- means the write was rejected for some other reason and says nothing about
capacity.

Like `matter_revoke_bench.py`, whose session and command helpers it borrows,
this NEVER commissions: it opens PASE against an open commissioning window and
invokes over that, consuming no fabric slot.
"""

import argparse
import asyncio
import os
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))

# NIST P-256. Every key installed is a different small multiple of the
# generator, computed rather than hard-coded: the trust store refuses a point it
# already holds, and that refusal is indistinguishable at the wire from a full
# partition. Reusing one key would fake the very result being measured.
P = 0xFFFFFFFF00000001000000000000000000000000FFFFFFFFFFFFFFFFFFFFFFFF
A = P - 3
GX = 0x6B17D1F2E12C4247F8BCE6E563A440F277037D812DEB33A0F4A13945D898C296
GY = 0x4FE342E2FE1A7F9B8EE7EB4A7C0F9E162BCE33576B315ECECBB6406837BF51F5


def _add(p, q):
    """Affine point addition, doubling included. Never called on a curve secret."""
    if p is None:
        return q
    if q is None:
        return p
    (x1, y1), (x2, y2) = p, q
    if x1 == x2 and (y1 + y2) % P == 0:
        return None
    if p == q:
        lam = (3 * x1 * x1 + A) * pow(2 * y1, P - 2, P) % P
    else:
        lam = (y2 - y1) * pow(x2 - x1, P - 2, P) % P
    x3 = (lam * lam - x1 - x2) % P
    return (x3, (lam * (x1 - x3) - y1) % P)


def point(k):
    """k*G as an uncompressed 65-byte SEC1 point, which is what the lock stores."""
    r, base = None, (GX, GY)
    while k:
        if k & 1:
            r = _add(r, base)
        base = _add(base, base)
        k >>= 1
    return b"\x04" + r[0].to_bytes(32, "big") + r[1].to_bytes(32, "big")


def self_test():
    """1G, 2G and 3G against the constants matter_revoke_bench already ships.

    A generator that is subtly wrong produces points the lock rejects, which
    would read as a capacity ceiling three anchors early. This is the check that
    separates those two, and it needs no board.
    """
    import matter_revoke_bench as bench

    ok = True
    for k, want in ((1, bench.TEST_KEY), (2, bench.TEST_KEY_2), (3, bench.TEST_KEY_3)):
        got = point(k)
        good = got == want
        ok &= good
        print(f"  {'PASS' if good else 'FAIL'}: {k}G == TEST_KEY"
              f"{'' if k == 1 else f'_{k}'} ({got[:5].hex()}...)")
    print(f"VERDICT: {'PASS' if ok else 'FAIL'}")
    return 0 if ok else 1


async def _install(bench, mod, user, cred, key):
    """SetUser then SetCredential, reporting only what the credential write said.

    SetUser fails on its own for reasons that have nothing to do with capacity --
    an index past the lock's user table answers InvalidCommand -- and the
    credential still lands. Treating that as the ceiling ends the probe early.
    """
    await bench.cmd(f"SetUser({user})", mod.DL.Commands.SetUser(
        operationType=mod.DL.Enums.DataOperationTypeEnum.kAdd,
        userIndex=user, userName="cap", userUniqueID=0x43,
        userStatus=mod.DL.Enums.UserStatusEnum.kOccupiedEnabled,
        userType=mod.DL.Enums.UserTypeEnum.kUnrestrictedUser,
        credentialRule=mod.DL.Enums.CredentialRuleEnum.kSingle))
    resp = await bench.cmd(
        f"SetCredential(type 7, index {cred}, user {user})",
        mod.DL.Commands.SetCredential(
            operationType=mod.DL.Enums.DataOperationTypeEnum.kAdd,
            credential=bench.credential(cred), credentialData=key,
            userIndex=user,
            userStatus=mod.DL.Enums.UserStatusEnum.kOccupiedEnabled,
            userType=mod.DL.Enums.UserTypeEnum.kUnrestrictedUser))
    status = getattr(resp, "status", None)
    return status if status is None else int(status)


async def run(args):
    import chip.native
    from chip.CertificateAuthority import CertificateAuthorityManager
    from chip.ChipStack import ChipStack

    import matter_revoke_bench as mod

    # Init first, for the same reason matter_revoke_bench does it first: parsing
    # the code reaches for the library handle the stack owns.
    chip.native.Init()
    mod._load()  # noqa: SLF001 -- binds mod.DL, and there is no public spelling

    passcode, disc = mod.parse_code(args.code)
    if args.long_discriminator is not None:
        disc = args.long_discriminator
    print(f"pairing code parsed: discriminator 0x{disc:03x}")

    stack = ChipStack(persistentStoragePath=args.storage,
                      enableServerInteractions=False)
    cam = CertificateAuthorityManager(chipStack=stack)
    cam.LoadAuthoritiesFromStorage()
    if not cam.activeCaList:
        cam.NewCertificateAuthority().NewFabricAdmin(vendorId=0xFFF1, fabricId=1)
    ctrl = cam.activeCaList[0].adminList[0].NewController(nodeId=112233)
    bench = mod.Bench(ctrl, args.endpoint)

    added = []
    ceiling = None
    try:
        print("opening PASE over BLE -- the commissioning window must be open ...")
        await ctrl.EstablishPASESessionBLE(passcode, disc, mod.NODE_ID)
        print("PASE up\n")
        for cred in args.pre_clear:
            await bench.cmd(f"pre-clear ClearCredential(7, {cred})",
                            mod.DL.Commands.ClearCredential(
                                credential=bench.credential(cred)))
        for n in range(args.count):
            user, cred = args.user + n, args.index + n
            total = args.base + len(added) + 1
            print(f"--- anchor {total}: user {user}, credential {cred} ---")
            status = await _install(bench, mod, user, cred, point(4 + n))
            if status == 0:
                added.append((user, cred))
                print(f"  stored: {total} anchor(s) now in the store")
            else:
                ceiling = total
                print(f"\nREFUSED at anchor {total} (status {status}). "
                      f"That is the ceiling: {total - 1} fit, {total} does not.")
                print("  check the board log for the errno -- -28 is -ENOSPC")
                break
    except Exception as exc:  # noqa: BLE001 -- report, never traceback at a bench
        print(f"FAILED: {type(exc).__name__}: {exc}")
    finally:
        # Leaving anchors behind would move the ceiling for the next run, so the
        # cleanup matters more here than the measurement.
        print(f"\ncleanup: removing the {len(added)} anchor(s) this probe added")
        for user, cred in reversed(added):
            try:
                await bench.cmd(f"ClearCredential(7, {cred})",
                                mod.DL.Commands.ClearCredential(
                                    credential=bench.credential(cred)))
                await bench.cmd(f"ClearUser({user})",
                                mod.DL.Commands.ClearUser(userIndex=user))
            except Exception as exc:  # noqa: BLE001 -- best-effort teardown
                print(f"  credential {cred} NOT removed: {exc} -- "
                      f"re-run with --pre-clear {cred}")
        try:
            await ctrl.UnpairDevice(mod.NODE_ID)
        except Exception:  # noqa: BLE001 -- best-effort teardown
            pass
        ctrl.Shutdown()
        stack.Shutdown()

    if ceiling is None:
        print(f"\nNo refusal in {args.count} attempt(s): the ceiling is above "
              f"{args.base + len(added)}. Raise --count.")
        return 1
    return 0


def main():
    ap = argparse.ArgumentParser(
        description="Add Aliro endpoint keys until the lock refuses one.")
    ap.add_argument("code", nargs="?", help="manual pairing code, dashes optional")
    ap.add_argument("--dry-run", action="store_true",
                    help="check the key generator against the bench constants, no board")
    ap.add_argument("--long-discriminator", type=int,
                    help="the 12 bits the advert carries; a manual code has only 4")
    ap.add_argument("--endpoint", type=int, default=1)
    ap.add_argument("--user", type=int, default=5, help="first user index to use")
    ap.add_argument("--index", type=int, default=30, help="first credential index")
    ap.add_argument("--count", type=int, default=4, help="how many to try")
    ap.add_argument("--base", type=int, default=0,
                    help="anchors already stored, so the running total printed is real")
    ap.add_argument("--pre-clear", type=int, nargs="*", default=[],
                    help="credential indices an interrupted run left behind")
    ap.add_argument("--storage", default="/tmp/cap-probe.json")
    args = ap.parse_args()
    if args.dry_run:
        sys.exit(self_test())
    if not args.code:
        ap.error("a pairing code is required unless --dry-run")
    sys.exit(asyncio.run(run(args)))


if __name__ == "__main__":
    main()
