<!-- generated documentation — edit the source, not this file -->
# `tools/piv_pin.py`

Provision or change the OpenAliro PIV PIN through macOS PC/SC.

**discussed in** [`host/presence/README.md`](../../../host/presence/README.md)

## API

### `class PivPinError(RuntimeError)`
`tools/piv_pin.py:20`

Expected provisioning or PC/SC failure.

**called by** `PcscCard.__init__`, `PcscCard._check`, `PcscCard._select_reader`, `encode_pin`, `prompt_new_pin`, `provision_or_change`, `select_piv`, `status_word`

### `encode_pin(value)`
`tools/piv_pin.py:31`

Return an eight-byte PIV PIN padded with 0xff.

**called by** `prompt_new_pin`, `provision_or_change`  ·  **calls** `PivPinError`

<details><summary>Undocumented (17)</summary>

- `ScardIoRequest`
- `status_word` — tested: status word
- `describe_status` — tested: status descriptions do not include pin material
- `PcscCard`
- `PcscCard.__init__`
- `PcscCard._configure_functions`
- `PcscCard._check`
- `PcscCard._select_reader`
- `PcscCard.transmit`
- `PcscCard.close`
- `PcscCard.__enter__`
- `PcscCard.__exit__`
- `select_piv`
- `prompt_new_pin`
- `provision_or_change`
- `build_parser`
- `main`

</details>
