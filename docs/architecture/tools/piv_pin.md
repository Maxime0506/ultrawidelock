<!-- generated documentation — edit the source, not this file -->
# `tools/piv_pin.py`

Provision or change the OpenAliro PIV PIN through macOS PC/SC.

**discussed in** [`host/presence/README.md`](../../../host/presence/README.md)

## API

### `class PivPinError(RuntimeError)`
`tools/piv_pin.py:20`

Expected provisioning or PC/SC failure.

**called by** `PcscCard.__init__`, `PcscCard._check`, `PcscCard._select_reader`, `encode_pin`, `prompt_new_pin`, `provision_or_change`, `select_piv`, `status_word`

### `class ScardIoRequest(ctypes.Structure)`
`tools/piv_pin.py:24`

PC/SC IO_REQUEST structure with protocol and length fields.

**called by** `PcscCard.transmit`

### `encode_pin(value)`
`tools/piv_pin.py:32`

Return an eight-byte PIV PIN padded with 0xff.

**called by** `prompt_new_pin`, `provision_or_change`  ·  **calls** `PivPinError`

### `status_word(response)`
`tools/piv_pin.py:40`

Extract the two-byte status word from the end of a PIV APDU response; raise PivPinError if the response is truncated.

**called by** `provision_or_change`, `select_piv`  ·  **calls** `PivPinError`

### `describe_status(status)`
`tools/piv_pin.py:47`

Decode a PIV status word into a human-readable message; include retry count for PIN-guessing failures.

**called by** `provision_or_change`, `select_piv`

### `class PcscCard`
`tools/piv_pin.py:58`

Open and manage a PC/SC connection to an OpenAliro PIV card on macOS; establish context, connect to the reader matching reader_match, begin a transaction, and clean up on exit.

**called by** `main`

#### `PcscCard._configure_functions(self)`
`tools/piv_pin.py:99`

Configure ctypes return types for all PC/SC library functions.

**called by** `PcscCard.__init__`

#### `PcscCard._check(result, operation)`
`tools/piv_pin.py:112`

Raise PivPinError if a PC/SC operation returned nonzero.

**called by** `PcscCard.__init__`, `PcscCard._select_reader`, `PcscCard.transmit`  ·  **calls** `PivPinError`

#### `PcscCard.close(self)`
`tools/piv_pin.py:168`

Release the PC/SC transaction, disconnect from the card, and release the context.

**called by** `PcscCard.__exit__`

#### `PcscCard.__enter__(self)`
`tools/piv_pin.py:180`

Enter the context manager and return self.

### `prompt_new_pin()`
`tools/piv_pin.py:197`

Prompt for a new PIV PIN twice, verify they match, encode it, and return the result.

**called by** `provision_or_change`  ·  **calls** `PivPinError`, `encode_pin`

### `build_parser()`
`tools/piv_pin.py:233`

Return an argument parser for --change and --reader options.

**called by** `main`

<details><summary>Undocumented (7)</summary>

- `PcscCard.__init__`
- `PcscCard._select_reader`
- `PcscCard.transmit`
- `PcscCard.__exit__`
- `select_piv`
- `provision_or_change`
- `main`

</details>
