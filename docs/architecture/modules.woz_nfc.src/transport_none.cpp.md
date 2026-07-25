<!-- generated documentation — edit the source, not this file -->
# `modules/woz_nfc/src/transport_none.cpp`

WozNfc backend for boards with no NFC frontend: polling never starts and no
NFC session is ever created, so Send()/Terminate() are unreachable in a
correct run; Send() reports invalid state defensively.

**depends on** [`modules/woz_nfc/include/woz_nfc/transport.h`](../modules.woz_nfc.include.woz_nfc/transport.h.md)

<details><summary>Undocumented (5)</summary>

- `Init`
- `Start`
- `Stop`
- `Send`
- `Terminate`

</details>
