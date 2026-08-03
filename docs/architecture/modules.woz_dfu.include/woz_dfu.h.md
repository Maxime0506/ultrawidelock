<!-- generated documentation — edit the source, not this file -->
# `modules/woz_dfu/include/woz_dfu.h`

@file
@brief The on-flash contract between the application and the bootloader for
a delta firmware update.
The application receives a patch over Bluetooth and writes it into the
`patch_staging` partition. MCUboot reads it on the next boot and applies it
onto the primary slot. Nothing else connects the two, so this header IS the
interface: a change here that is not made on both sides produces a board
that stages an update and then silently declines to install it.
Plain C11 with no Zephyr dependency, so the host tests and the patch builder
can include it and agree on the layout by construction rather than by
transcription.

**used by** [`modules/woz_dfu/src/dfu_applier.c`](../modules.woz_dfu.src/dfu_applier.c.md), [`modules/woz_dfu/src/dfu_receiver.c`](../modules.woz_dfu.src/dfu_receiver.c.md), [`modules/woz_dfu/src/dfu_smp_img.c`](../modules.woz_dfu.src/dfu_smp_img.c.md)

## API

### `struct woz_dfu_hdr`
`modules/woz_dfu/include/woz_dfu.h:80`

The staged-update header.
Integrity here is CRC-32, not a hash, and that is deliberate. AUTHENTICITY is
checked by the APPLICATION, which has PSA ECDSA-P256 already linked for
Aliro, before it ever writes this header. The bootloader is the flash-starved
image and only has to answer a narrower question: did the bytes I am about to
apply arrive intact, and do they belong to the image I am running? A CRC
answers both.
The floor underneath both is MCUboot's own image validation:
`CONFIG_BOOT_VALIDATE_SLOT0=y` re-verifies the P-256 signature of the
RESULT before booting it. So a forged header cannot install code -- it can
only destroy the current image, and `CONFIG_BOOT_SERIAL_NO_APPLICATION=y`
catches that in recovery rather than in a boot loop.
Every field is little-endian. Total 32 bytes, word-aligned throughout,
because the nRF flash driver writes words.
