<!-- generated documentation — edit the source, not this file -->
# `modules/woz_dfu/include/woz_dfu_rx.h`

@file
@brief Receives a delta patch into the staging partition, application side.
Transport-independent on purpose. The DWM3001CDK feeds this from a second
L2CAP CoC beside the Aliro one, but nothing here knows that -- it takes
frames and returns replies, so the host tests can drive it without a radio.
The bootloader half is @ref woz_dfu.h. This side never applies anything: it
writes bytes, checks a signature, and reboots.

**used by** [`modules/woz_dfu/src/dfu_receiver.c`](../modules.woz_dfu.src/dfu_receiver.c.md), [`modules/woz_dfu/src/dfu_smp_img.c`](../modules.woz_dfu.src/dfu_smp_img.c.md)
