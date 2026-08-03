#!/usr/bin/env python3
"""Provision or change the OpenAliro PIV PIN through macOS PC/SC."""

import argparse
import ctypes
import getpass
import sys

PIV_AID = bytes.fromhex("a000000308000010000100")
DEFAULT_READER_MATCH = "OpenAliro Presence PIV"
PCSC_PATH = "/System/Library/Frameworks/PCSC.framework/PCSC"

SCARD_SCOPE_USER = 0x0000
SCARD_SHARE_SHARED = 0x0002
SCARD_PROTOCOL_T0 = 0x0001
SCARD_PROTOCOL_T1 = 0x0002
SCARD_LEAVE_CARD = 0x0000


class PivPinError(RuntimeError):
    """Expected provisioning or PC/SC failure."""


class ScardIoRequest(ctypes.Structure):
    """PC/SC IO_REQUEST structure with protocol and length fields."""
    _fields_ = [
        ("protocol", ctypes.c_uint32),
        ("length", ctypes.c_uint32),
    ]


def encode_pin(value):
    """Return an eight-byte PIV PIN padded with 0xff."""
    if not 6 <= len(value) <= 8 or not value.isascii() or not value.isdigit():
        raise PivPinError("PIN must contain 6 to 8 ASCII digits")
    encoded = value.encode("ascii")
    return encoded + b"\xff" * (8 - len(encoded))


def status_word(response):
    """Extract the two-byte status word from the end of a PIV APDU response; raise PivPinError if the response is truncated."""
    if len(response) < 2:
        raise PivPinError("token returned a truncated APDU response")
    return int.from_bytes(response[-2:], "big")


def describe_status(status):
    """Decode a PIV status word into a human-readable message; include retry count for PIN-guessing failures."""
    if status == 0x6983:
        return "PIN is unprovisioned or blocked"
    if status == 0x6982:
        return "token security policy denied the operation"
    if status & 0xFFF0 == 0x63C0:
        return f"wrong current PIN; {status & 0x000F} retries remain"
    return f"token returned status 0x{status:04x}"


class PcscCard:
    """Open and manage a PC/SC connection to an OpenAliro PIV card on macOS; establish context, connect to the reader matching reader_match, begin a transaction, and clean up on exit."""
    def __init__(self, reader_match):
        if sys.platform != "darwin":
            raise PivPinError("this helper requires macOS PC/SC")
        try:
            self.library = ctypes.CDLL(PCSC_PATH)
        except OSError as exc:
            raise PivPinError("could not load macOS PC/SC") from exc
        self.context = ctypes.c_int32()
        self.card = ctypes.c_int32()
        self.protocol = ctypes.c_uint32()
        self.connected = False
        self.transaction = False
        self._configure_functions()

        self._check(
            self.library.SCardEstablishContext(
                SCARD_SCOPE_USER, None, None, ctypes.byref(self.context)
            ),
            "establish PC/SC context",
        )
        reader = self._select_reader(reader_match)
        self._check(
            self.library.SCardConnect(
                self.context,
                reader,
                SCARD_SHARE_SHARED,
                SCARD_PROTOCOL_T0 | SCARD_PROTOCOL_T1,
                ctypes.byref(self.card),
                ctypes.byref(self.protocol),
            ),
            "connect to token",
        )
        self.connected = True
        self._check(
            self.library.SCardBeginTransaction(self.card),
            "begin token transaction",
        )
        self.transaction = True

    def _configure_functions(self):
        """Configure ctypes return types for all PC/SC library functions."""
        library = self.library
        library.SCardEstablishContext.restype = ctypes.c_int32
        library.SCardListReaders.restype = ctypes.c_int32
        library.SCardConnect.restype = ctypes.c_int32
        library.SCardBeginTransaction.restype = ctypes.c_int32
        library.SCardTransmit.restype = ctypes.c_int32
        library.SCardEndTransaction.restype = ctypes.c_int32
        library.SCardDisconnect.restype = ctypes.c_int32
        library.SCardReleaseContext.restype = ctypes.c_int32

    @staticmethod
    def _check(result, operation):
        """Raise PivPinError if a PC/SC operation returned nonzero."""
        if result != 0:
            unsigned = ctypes.c_uint32(result).value
            raise PivPinError(f"could not {operation} (PC/SC 0x{unsigned:08x})")

    def _select_reader(self, reader_match):
        length = ctypes.c_uint32()
        self._check(
            self.library.SCardListReaders(
                self.context, None, None, ctypes.byref(length)
            ),
            "list smart-card readers",
        )
        buffer = ctypes.create_string_buffer(length.value)
        self._check(
            self.library.SCardListReaders(
                self.context, None, buffer, ctypes.byref(length)
            ),
            "read smart-card reader list",
        )
        readers = [
            item
            for item in buffer.raw[: length.value].split(b"\0")
            if item
        ]
        needle = reader_match.encode("utf-8")
        matches = [reader for reader in readers if needle in reader]
        if len(matches) != 1:
            raise PivPinError(
                "expected exactly one matching OpenAliro PIV reader"
            )
        return matches[0]

    def transmit(self, command):
        send = (ctypes.c_ubyte * len(command)).from_buffer_copy(command)
        receive = (ctypes.c_ubyte * 2048)()
        receive_length = ctypes.c_uint32(len(receive))
        send_pci = ScardIoRequest(
            self.protocol.value, ctypes.sizeof(ScardIoRequest)
        )
        receive_pci = ScardIoRequest()
        self._check(
            self.library.SCardTransmit(
                self.card,
                ctypes.byref(send_pci),
                send,
                len(command),
                ctypes.byref(receive_pci),
                receive,
                ctypes.byref(receive_length),
            ),
            "exchange an APDU with the token",
        )
        return bytes(receive[: receive_length.value])

    def close(self):
        """Release the PC/SC transaction, disconnect from the card, and release the context."""
        if self.transaction:
            self.library.SCardEndTransaction(self.card, SCARD_LEAVE_CARD)
            self.transaction = False
        if self.connected:
            self.library.SCardDisconnect(self.card, SCARD_LEAVE_CARD)
            self.connected = False
        if self.context.value:
            self.library.SCardReleaseContext(self.context)
            self.context = ctypes.c_int32()

    def __enter__(self):
        """Enter the context manager and return self."""
        return self

    def __exit__(self, exc_type, exc_value, traceback):
        self.close()


def select_piv(card):
    response = card.transmit(
        bytes([0x00, 0xA4, 0x04, 0x00, len(PIV_AID)]) + PIV_AID
    )
    status = status_word(response)
    if status != 0x9000:
        raise PivPinError(f"could not select PIV application: {describe_status(status)}")


def prompt_new_pin():
    """Prompt for a new PIV PIN twice, verify they match, encode it, and return the result."""
    first = getpass.getpass("New PIV PIN (6-8 digits): ")
    second = getpass.getpass("Repeat new PIV PIN: ")
    if first != second:
        raise PivPinError("new PIN entries did not match")
    return encode_pin(first)


def provision_or_change(card, change):
    select_piv(card)
    if change:
        old_pin = encode_pin(getpass.getpass("Current PIV PIN: "))
    else:
        status = status_word(card.transmit(bytes.fromhex("00200080")))
        if status != 0x6983:
            raise PivPinError(
                "PIN is already provisioned; rerun with --change"
            )
        old_pin = b"\xff" * 8

    new_pin = prompt_new_pin()
    command = bytes.fromhex("0024008010") + old_pin + new_pin
    status = status_word(card.transmit(command))
    if status != 0x9000:
        raise PivPinError(f"PIN update failed: {describe_status(status)}")

    verify = bytes.fromhex("0020008008") + new_pin
    status = status_word(card.transmit(verify))
    if status != 0x9000:
        raise PivPinError(f"PIN verification failed: {describe_status(status)}")
    logout_status = status_word(card.transmit(bytes.fromhex("0020ff80")))
    if logout_status != 0x9000:
        raise PivPinError("PIN was set, but token logout failed")


def build_parser():
    """Return an argument parser for --change and --reader options."""
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument(
        "--change",
        action="store_true",
        help="change an existing PIN instead of first-time provisioning",
    )
    parser.add_argument(
        "--reader",
        default=DEFAULT_READER_MATCH,
        metavar="TEXT",
        help="unique reader-name substring (default: OpenAliro product name)",
    )
    return parser


def main(argv=None):
    args = build_parser().parse_args(argv)
    try:
        with PcscCard(args.reader) as card:
            provision_or_change(card, args.change)
    except (PivPinError, OSError) as exc:
        print(f"piv-pin: {exc}", file=sys.stderr)
        return 1
    print("PIV PIN provisioned and verified" if not args.change else
          "PIV PIN changed and verified")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
