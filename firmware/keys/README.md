# The MCUboot image-signing key

One file lives here, `mcuboot_ec_p256.pem`, and it is gitignored. It is an
ECDSA P-256 private key. MCUboot embeds its public half at build time and
refuses to boot any image that half did not sign.

```bash
make dfu-key      # generate it, once per clone. Never overwrites.
```

## Why this exists instead of the default

MCUboot ships `root-ec-p256.pem` in its own repository and uses it when nothing
else is configured. That key is public. Firmware signed with it is accepted by
every stock MCUboot in the world, so on a lock it is not a signing key at all,
it is a formality.

MCUboot does notice, at `boot/zephyr/CMakeLists.txt:449-452`, and emits a
`message(WARNING ...)`. A warning inside a ten-thousand-line build log is how
that survived here for as long as it did. `firmware/sysbuild.cmake` now makes it
a `FATAL_ERROR` instead, checked against MCUboot's full list of seven default
key files, so the build stops rather than shipping.

## Two traps worth knowing before you change the wiring

**The path must be absolute.** Sysbuild passes this symbol to the bootloader
image through `set_config_string()`
(`zephyr/share/sysbuild/images/bootloader/CMakeLists.txt:17`), never through a
`.conf` file. MCUboot's own resolver searches the text of its conf files for the
path string to work out a base directory
(`boot/zephyr/CMakeLists.txt:410-420`), finds nothing, and so a relative path
falls through to line 428: `${MCUBOOT_DIR}/<path>`. It resolves *inside the
MCUboot repository*, which is exactly how a wrong path turns back into the demo
key without saying so.

**The quotes are part of the value.** `zephyr/cmake/modules/kconfig.cmake:264`
writes a command-line cache variable through verbatim, so `mk/cdk.mk` passes
`-DSB_CONFIG_BOOT_SIGNATURE_KEY_FILE='"/abs/path.pem"'`. Drop the inner quotes
and you get invalid Kconfig string syntax.

## What losing it costs, and what rotating it costs

Losing the key costs you nothing on a board you can still reach with a debugger:
`make dfu-key` makes a new one and `make flash` writes a matching MCUboot and
application together. It costs you the board only if serial recovery is the
only way in, because then nothing you can sign will be accepted.

Rotating it has the same shape. MCUboot lives at `0x00000` and is rewritten by
`make flash`, so a full flash moves both halves at once. What you must not do is
sign an update with a new key and push it over serial recovery to a board still
running an MCUboot that holds the old public half: it will refuse the image, and
with `CONFIG_SINGLE_APPLICATION_SLOT=y` there is no second slot holding the
previous one.

`settings_storage` at `0x7e000` is untouched by any of this. The Matter fabrics,
the trust anchors and the reader identity survive a key rotation, because
`make flash` never passes `--erase`.

## CI

CI generates a throwaway key with `make dfu-key` before it builds, and that is
deliberate rather than a shortcut. CI artifacts are build-verification only. If
CI signed with the real key, the real key would have to live in the CI
provider's secret store, and an image anyone could download would be one a
deployed lock accepts.

There is no key escrow here and no key in any secret store. If you deploy these
boards for real, the private key belongs wherever your other production secrets
live, and this README is not that place.
