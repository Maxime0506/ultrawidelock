<!-- generated documentation — edit the source, not this file -->
# `modules/woz_dfu/src/dfu_smp_img.c`

@file
@brief SMP image-management group, so a stock mcumgr client can push a delta.
WHY THIS EXISTS INSTEAD OF ZEPHYR'S img_mgmt. Zephyr's implementation cannot
be built here, and not for a reason a partition rename fixes:
CONFIG_MCUMGR_GRP_IMG ... unsatisfied dependencies:
IMG_MANAGER (=n), (!MCUBOOT_BOOTLOADER_MODE_SINGLE_APP) (=n)
img_mgmt is gated OFF by single-slot mode itself. That mode is not incidental
on this board -- it is the only reason MCUboot fits, because two slots want
844 KB of a 512 KB part (firmware/pm_static.yml does the arithmetic). So the
choice was to leave single-slot mode, which the flash forbids, or to serve
group 1 ourselves. This is the second.
It is a thin adapter, not a reimplementation: every byte still goes through
woz_dfu_rx_upload(), so the signature check, the size limits, the CRC and the
window gate are the same ones the native transport uses, in the same order.
What is new here is only CBOR in and CBOR out.
WHAT A CLIENT SEES. One image, one slot, active and confirmed, versioned and
hashed from the running MCUboot header. Uploads are accepted and staged. It
does NOT pretend to have a second slot, because there is no honest hash to
report for one -- the staged bytes are a patch, and what they produce is not
known until the bootloader has applied it.
SO THE GUIDED "FIRMWARE UPGRADE" WIZARD IS NOT THE TARGET. That flow wants
upload -> test -> reset -> reconnect -> confirm, and two things break it: the
device never reports a pending second image to confirm, and the reboot after
reset spends 17-31 s applying the patch, which outlasts the client's
reconnect window. The supported path is the plain one, and it is three taps:
1. Images -> Upload, choose the .woz patch      (this file, group 1 cmd 1)
2. Device -> Reset                              (os_mgmt, group 0 cmd 5)
3. wait ~30 s while MCUboot applies it          (src/dfu_applier.c)

**depends on** [`modules/woz_dfu/include/woz_dfu.h`](../modules.woz_dfu.include/woz_dfu.h.md), [`modules/woz_dfu/include/woz_dfu_rx.h`](../modules.woz_dfu.include/woz_dfu_rx.h.md)

```mermaid
flowchart TD
  encode_slot --> running_hash
  encode_slot --> running_header
```

## API

### `static const struct image_header *running_header(void)`
`modules/woz_dfu/src/dfu_smp_img.c:123`

Read straight through a pointer rather than flash_area_read(): this is the
image that is currently executing, so it is already mapped at a known address
and a copy would only cost stack.

**called by** `encode_slot`

### `static const uint8_t *running_hash(const struct image_header *h)`
`modules/woz_dfu/src/dfu_smp_img.c:138`

Find the SHA-256 that MCUboot recorded for the running image.
This is the same hash the bootloader verifies against, so a client that
records it before an update can tell afterwards whether the update landed.
Returns NULL if the TLV block is not where the header says it is, which is
reported to the client as an all-zero hash rather than a refusal.

**called by** `encode_slot`

### `static bool encode_slot(zcbor_state_t *zse)`
`modules/woz_dfu/src/dfu_smp_img.c:181`

Encode the one slot this board has.
Deliberately shaped exactly like Zephyr's img_mgmt_state_encode_slot(), keys
and order included, so a client cannot distinguish the two.

**called by** `state_read`  ·  **calls** `running_hash`, `running_header`

### `static int state_write(struct smp_streamer *ctxt)`
`modules/woz_dfu/src/dfu_smp_img.c:236`

Accept "set state" and change nothing.
A client sends this to mark an uploaded image test-or-confirm. There is
nothing to mark: an update is either fully staged, in which case the next
boot applies it, or it is not. Answering with the current list is both
truthful and what the client expects to parse.

**calls** `state_read`

### `static int erase_write(struct smp_streamer *ctxt)`
`modules/woz_dfu/src/dfu_smp_img.c:321`

Throw away whatever is staged.
Gated on the window like upload is, and for the same reason: this erases
flash, and an unauthenticated peer that can erase in a loop is an
availability attack on a door lock.

### `static enum mgmt_cb_return reset_gate(uint32_t event, enum mgmt_cb_return prev_status, int32_t *rc, uint16_t *group, bool *abort_more, void *data, size_t data_size)`
`modules/woz_dfu/src/dfu_smp_img.c:359`

Refuse os_mgmt reset unless an update is actually in flight.
THIS IS NOT OPTIONAL ON THIS BOARD. MCUMGR_TRANSPORT_BT_PERM_RW leaves the
SMP endpoint writable by any unpaired peer in radio range, and group 0
command 5 reboots the device. Without this hook, anyone within a few metres
of the door could hold the lock in a reboot loop -- an availability attack
that needs no credential, no pairing and no knowledge of the firmware, and
the single strongest argument against putting mcumgr on a lock at all.
The gate is the same one the upload uses: the owner has opened an update
window, or a verified patch is already staged and waiting to be applied.
Outside those two states the reader has no reason to accept a remote reboot.

<details><summary>Undocumented (7)</summary>

- `image_version`
- `image_header`
- `image_tlv_info`
- `image_tlv`
- `state_read`
- `upload_write`
- `woz_smp_img_init`

</details>
