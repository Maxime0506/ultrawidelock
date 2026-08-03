<!-- generated documentation — edit the source, not this file -->
# `firmware/src/dfu_ble_zephyr.c`

@file
@brief The over-the-air update channel: a second L2CAP CoC, and the button
that opens it.
WHY NOT mcumgr. SMP over Bluetooth was built and measured first. It costs
3,717 B of RAM on an image that had 7,448 B left, and its permission model
defaults to demanding a paired, authenticated link whenever BT_SMP is on --
which it is here, pulled in by L2CAP CoC. This reader must never ask a phone
to pair, because the walk-up unlock depends on it not asking. Setting the
permission to open instead hands an unauthenticated peer a write path into
flash, and mcumgr's OS group would hand it an unauthenticated reset command
as well. A lock anyone in radio range can reboot in a loop is a real attack.
So the patch rides the CoC transport this board already has, on its own PSM,
and authorization is a WINDOW rather than a handshake.
WHY A WINDOW IS ENOUGH. The gate is a denial-of-service control, not an
integrity one. The patch header is signed and the application checks it
(modules/woz_dfu/src/dfu_receiver.c), and underneath that MCUboot re-verifies
the P-256 signature of the patched RESULT before booting it. No peer can
install code no matter what reaches this channel. What a closed channel
prevents is a stranger spending the flash's erase cycles and the owner's
uptime.

<details><summary>Undocumented (9)</summary>

- `dfu_alloc_buf`
- `dfu_recv`
- `dfu_connected`
- `dfu_disconnected`
- `dfu_accept`
- `dfu_gatt_write`
- `button_work_fn`
- `button_pressed`
- `dfu_ble_start`

</details>
