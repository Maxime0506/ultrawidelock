#!/usr/bin/env python3
"""Unit tests for the macOS PIV PIN helper's pure policy functions."""

import importlib.util
import pathlib
import unittest

ROOT = pathlib.Path(__file__).resolve().parents[2]
SPEC = importlib.util.spec_from_file_location(
    "piv_pin", ROOT / "tools" / "piv_pin.py"
)
piv_pin = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(piv_pin)


class PivPinTests(unittest.TestCase):
    def test_six_digit_pin_is_padded(self):
        self.assertEqual(piv_pin.encode_pin("123456"), b"123456\xff\xff")

    def test_eight_digit_pin_is_not_padded(self):
        self.assertEqual(piv_pin.encode_pin("12345678"), b"12345678")

    def test_pin_policy_rejects_bad_inputs(self):
        for value in ("12345", "123456789", "abcdef", "１２３４５６"):
            with self.subTest(value=value):
                with self.assertRaises(piv_pin.PivPinError):
                    piv_pin.encode_pin(value)

    def test_status_word(self):
        self.assertEqual(piv_pin.status_word(b"\x01\x02\x90\x00"), 0x9000)
        with self.assertRaises(piv_pin.PivPinError):
            piv_pin.status_word(b"\x90")

    def test_status_descriptions_do_not_include_pin_material(self):
        self.assertEqual(
            piv_pin.describe_status(0x63C2),
            "wrong current PIN; 2 retries remain",
        )
        self.assertEqual(
            piv_pin.describe_status(0x6983),
            "PIN is unprovisioned or blocked",
        )


if __name__ == "__main__":
    unittest.main()
