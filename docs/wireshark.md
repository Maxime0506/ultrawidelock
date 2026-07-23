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

The research doc does not pin the byte order of the expiry field. The dissector reads it both
ways and shows the interpretation that lands in a plausible date window (2020-2100), labelling
which byte order that was and printing both raw values, so the choice stays checkable against
a real reader. If a live capture proves one order, this is where to hard-set it.

## Using `aliro_timesync`

The Procedure-0 time sync rides GATT/L2CAP on a handle a passive tool cannot identify on its
own, so it is not auto-hooked. To decode it: select the packet carrying the time-sync payload,
right-click the L2CAP/ATT value bytes, choose **Decode As...**, and set the protocol to
**aliro_timesync**.

Its field set and sizes come straight from research doc section 5. The wire order follows the
doc's listing order and the clock-skew flag width is inferred; neither is yet confirmed against
a live capture, so treat the field offsets as provisional until a real Procedure-0 sync is
matched. A payload shorter than 23 bytes is flagged rather than mis-parsed.

## Verified against

The dissector was checked with tshark 4.6.7 against synthetic `0xFFF2` advertisements
(big-endian and little-endian expiries, a no-clock reader, and a truncated advert) and a
synthetic 23-byte Procedure-0 payload, with zero Lua errors. It has not yet been run against a
capture from a real iPhone-to-reader transaction; that is the natural next validation.
