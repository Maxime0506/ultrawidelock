# Wireshark dissector

`tools/aliro.lua` turns the reverse-engineering in [protocol-research.md](protocol-research.md)
into something you can run against a live capture. It decodes the parts of an Aliro
transaction that a passive BLE sniffer sees **in the clear**, with no keys and no firmware
changes. This is the "BLE sniffer alone yields everything through section 6" path the
research doc describes, made executable.

It decodes two things:

- **`aliro_adv`** — the `0xFFF2` advertising service data (research doc section 3). Fires
  automatically on every advertisement that carries 16-bit service UUID `0xFFF2`.
- **`aliro_timesync`** — the Procedure-0 time-sync message (research doc section 5), the one
  time sync sent before any session key exists and therefore readable in the clear. Applied
  manually with **Decode As** (see below).

## What it does not decode

Everything after *Access Protocol Completed* is encrypted under BleSK: the in-session time
sync, ranging setup **M1-M4**, and suspend/resume. A passive capture sees only ciphertext,
so this dissector does not attempt them. The UWB ranging frames (section 7) are not BLE and
never reach Wireshark from a BLE sniffer. Decoding either would need a key or the firmware's
own decrypted trace, which is a separate effort.

## Install

Copy the plugin into your Wireshark personal Lua plugins directory (find it under
**Help > About Wireshark > Folders > Personal Lua Plugins**), then restart Wireshark or press
**Ctrl/Cmd+Shift+L** to reload Lua:

```
cp tools/aliro.lua "$(tshark -G folders | awk -F'\t' '/Personal Lua Plugins/{print $2}')"/
```

Or load it for a single run without installing:

```
tshark -X lua_script:tools/aliro.lua -r your_capture.pcapng
```

## Capture recipe (nRF52 sniffer)

Any BLE sniffer works, because the dissector hooks the EIR/AD service-data layer that
Wireshark builds regardless of the capture's link type (Nordic, TI, Ubertooth, or an Android
`btsnoop_hci.log`). The Nordic sniffer is the easiest to get running:

1. Flash an nRF52840 dongle or DK with **nRF Sniffer for Bluetooth LE** (Nordic's package),
   and install its Wireshark extcap plugin per Nordic's instructions.
2. In Wireshark, pick the **nRF Sniffer for Bluetooth LE** capture interface and start it.
3. Power the reader. Its `ADV_IND` on the primary advertising channels carries the `0xFFF2`
   service data; the dissector labels it immediately, no target selection needed.
4. To catch the Procedure-0 time sync, select the reader in the sniffer toolbar so it follows
   the connection, then walk a provisioned phone up to it.

Fastest sanity check, straight from the research doc: filter on `aliro_adv.flow_uwb == 1`.
A hit confirms a UWB-capable reader is advertising within seconds.

## Using `aliro_adv`

Auto-fires. Useful fields and filters:

| field | filter | meaning |
|---|---|---|
| BLE + UWB flow | `aliro_adv.flow_uwb` | the one bit that decides whether the phone will range at all |
| BLE-only flow | `aliro_adv.flow_ble` | control-only flow supported |
| adv version | `aliro_adv.adv_version` | folded into the secure-channel KDF |
| TX power | `aliro_adv.tx_power` | int8 dBm |
| group id | `aliro_adv.group_id` | truncated reader group identifier plus sub-identifier |
| tag expiry | `aliro_adv.tag_expiry` | dynamic tag expiry, or `0xFFFFFFFF` when the reader has no clock |
| dynamic tag | `aliro_adv.dynamic_tag` | first 7 octets of `AES-128(GroupResolvingKey, pad ‖ AdvA ‖ expiry)` |

Expert notes flag the two states the field guide (section 10) calls out on sight: the UWB
flow bit clear (control-only build, phone will not range) and a `0xFFFFFFFF` expiry (reader
has no clock).

The expiry is decoded big-endian. That byte order is confirmed against the firmware that
builds the advert (`modules/woz_aliro_stack`, `SetDynamicTagExpiryTimestamp` writes the
most-significant byte first) and the Aliro Specification 1.0 Appendix 20 known-answer vectors
(`tests/host/test_aliro_advertising.c`). The raw four bytes stay visible, and an expiry that
is not a plausible date is flagged as a likely malformed or misaligned advert.

## Using `aliro_timesync`

The Aliro control plane rides an L2CAP connection-oriented channel (observed PSM `0x0080`, a
dynamic CID such as `0x0040`), not ATT, so the time sync is not auto-hooked. To try it,
right-click a frame on that CID, choose **Decode As...**, and set the *L2CAP CID* row to
**aliro_timesync**. On the command line:

```
tshark -r capture.pklg -d btl2cap.cid==0x0040,aliro_timesync -Y "frame.number==N" -O aliro_timesync
```

Its field set and sizes come from research doc section 5, with the wire order and clock-skew
flag width inferred; treat the offsets as provisional. A payload shorter than 23 bytes is
flagged rather than mis-parsed.

**Bench note.** In a real iPhone-to-ESP32 capture, no clear-text Procedure-0 appears on the
wire. The control plane is proto-2 framed PDUs `[type | subtype | 16-bit length | body]`: type
`00` is the Auth/ECDH exchange (clear, `04`-prefixed EC public keys), type `01`/`03` are auth
cryptograms, and type `02` is BleSK-sealed control. The only 23-byte frames are sealed type-`02`
PDUs whose bodies are ciphertext, so Decode As on them yields nonsense (a giant device event
count, an impossible max-PPM). Any time sync in a modern transaction is therefore sealed (Tier
B). This decoder is retained for manual use and for older or spec-defined flows that do send a
clear Procedure-0.

## Verified against

The dissector was checked with tshark 4.6.7 against synthetic `0xFFF2` advertisements,
including the Aliro Specification 1.0 Appendix 20 vector (expiry `0x7a4b8500`, dynamic tag
`7b7f4a82557990`), a no-clock reader, a truncated advert, an implausible-expiry advert, and a
synthetic 23-byte Procedure-0 payload, with zero Lua errors.

It has also been run against a real capture: an iPhone PacketLogger trace of a walk-up to an
ESP32 reader (39 `0xFFF2` adverts). `aliro_adv` auto-fired on the live iOS HCI capture and
decoded every advert, with the big-endian expiry resolving to real wall-clock dates matching the
capture time (confirming the byte order on silicon) and a fresh 7-octet dynamic tag on each
refresh. That same capture is what established that no clear-text Procedure-0 rides the wire (see
the bench note above).
