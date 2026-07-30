#!/usr/bin/env python3
"""Derive a SPAKE2+ verifier (w0 and L) for a Matter setup passcode.

A Matter device never stores its setup passcode. It stores the verifier, which
is what PBKDF2 over the passcode yields plus one scalar multiplication:

    w0s || w1s = PBKDF2-HMAC-SHA256(passcode as LE uint32, salt, iterations, 80)
    w0 = w0s mod n     w1 = w1s mod n     L = w1 * G

Someone who reads the flash gets w0 and L, which are enough to VERIFY a
commissioner that knows the passcode and not enough to impersonate one. That is
the whole point of the augmented form, and the reason this runs here rather than
on the device: L needs a base-point multiply, which is not one of the four
operations nrf_oberon exposes to the reader.

Usage:

    scripts/spake2p_verifier.py                     # CHIP's test pairing
    scripts/spake2p_verifier.py --passcode 12345678 --salt-b64 <...>

The output goes into CONFIG_ALIRO_MATTER_SPAKE2P_VERIFIER and friends
(ports/dwm3001cdk/app/Kconfig). Print nothing anywhere it will be logged: the
verifier is not a secret in the way the passcode is, but it identifies the
device and there is no reason to scatter it.
"""
import argparse
import base64
import hashlib

# P-256 (SEC 2, secp256r1).
P = 0xFFFFFFFF00000001000000000000000000000000FFFFFFFFFFFFFFFFFFFFFFFF
A = P - 3
B = 0x5AC635D8AA3A93E7B3EBBD55769886BC651D06B0CC53B0F63BCE3C3E27D2604B
N = 0xFFFFFFFF00000000FFFFFFFFFFFFFFFFBCE6FAADA7179E84F3B9CAC2FC632551
G = (
    0x6B17D1F2E12C4247F8BCE6E563A440F277037D812DEB33A0F4A13945D898C296,
    0x4FE342E2FE1A7F9B8EE7EB4A7C0F9E162BCE33576B315ECECBB6406837BF51F5,
)


def on_curve(pt):
    if pt is None:
        return True
    x, y = pt
    return (y * y - (x * x * x + A * x + B)) % P == 0


def add(p1, p2):
    if p1 is None:
        return p2
    if p2 is None:
        return p1
    x1, y1 = p1
    x2, y2 = p2
    if x1 == x2 and (y1 + y2) % P == 0:
        return None
    if p1 == p2:
        lam = (3 * x1 * x1 + A) * pow(2 * y1, P - 2, P) % P
    else:
        lam = (y2 - y1) * pow(x2 - x1, P - 2, P) % P
    x3 = (lam * lam - x1 - x2) % P
    return (x3, (lam * (x1 - x3) - y1) % P)


def mul(k, pt):
    r, acc = None, pt
    k %= N
    while k:
        if k & 1:
            r = add(r, acc)
        acc = add(acc, acc)
        k >>= 1
    return r


def main():
    ap = argparse.ArgumentParser(description=__doc__)
    # CHIP's own test pairing, so the defaults commission with a stock
    # chip-tool and with Apple Home using the standard setup code.
    ap.add_argument("--passcode", type=int, default=20202021)
    ap.add_argument("--salt-b64", default="U1BBS0UyUCBLZXkgU2FsdA==")
    ap.add_argument("--salt-hex", default=None, help="overrides --salt-b64")
    ap.add_argument("--iterations", type=int, default=1000)
    args = ap.parse_args()

    salt = bytes.fromhex(args.salt_hex) if args.salt_hex else base64.b64decode(args.salt_b64)

    # Bounds the device also enforces (matter_pase.h, from CHIPCryptoPAL.h:86-89).
    if not 16 <= len(salt) <= 32:
        raise SystemExit(f"salt must be 16..32 bytes, got {len(salt)}")
    if not 1000 <= args.iterations <= 100000:
        raise SystemExit("iterations must be 1000..100000")
    if not 0 < args.passcode < (1 << 27):
        raise SystemExit("passcode must fit 27 bits")

    # LITTLE-endian uint32. Getting this backwards produces a verifier that is
    # wrong in a way nothing detects until a commissioner's cA fails.
    ws = hashlib.pbkdf2_hmac(
        "sha256", args.passcode.to_bytes(4, "little"), salt, args.iterations, 80
    )
    w0 = int.from_bytes(ws[:40], "big") % N
    w1 = int.from_bytes(ws[40:], "big") % N
    L = mul(w1, G)

    assert on_curve(L) and mul(N, L) is None, "L is not a valid P-256 point"
    assert 0 < w0 < N and 0 < w1 < N

    l_bytes = b"\x04" + L[0].to_bytes(32, "big") + L[1].to_bytes(32, "big")
    blob = w0.to_bytes(32, "big") + l_bytes

    print(f"passcode    {args.passcode}")
    print(f"salt        {salt.hex()}  ({len(salt)} bytes)")
    print(f"iterations  {args.iterations}")
    print()
    print("CONFIG_ALIRO_MATTER_SPAKE2P_SALT=\"%s\"" % salt.hex())
    print("CONFIG_ALIRO_MATTER_SPAKE2P_ITERATIONS=%d" % args.iterations)
    print("CONFIG_ALIRO_MATTER_SPAKE2P_VERIFIER=\"%s\"" % blob.hex())
    print()
    print(f"# {len(blob)} bytes: w0 (32) then L (65, uncompressed)")


main()
