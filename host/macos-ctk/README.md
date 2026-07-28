# OpenAliro macOS CryptoTokenKit extension

This disposable-VM token extension marks the two token operations required by
LoginWindow as allowed without CryptoTokenKit-managed authentication:

- slot 9A signs only after the firmware completes a fresh Wallet/Aliro/UWB
  transaction inside the configured distance;
- slot 9D performs the key agreement macOS uses to wrap or unwrap the login
  keychain secret.

The separate `sdkconfig.piv-uwb` firmware profile permits those two APDUs
without PIV `VERIFY`. The ordinary `sdkconfig.piv` profile continues to require
the provisioned PIN and is the recovery image.

This does not remove the PIN field from LoginWindow on macOS 26.4.1.
LoginWindow still requires PIN submission before it requests the slot 9A
signature, even though the token operation itself has a boolean `true`
constraint. The custom profile therefore demonstrates native smart-card login
with fresh UWB enforcement, but not PINless native screen unlock.

## Build

On macOS 26 with Xcode and CMake:

```sh
make presence-token
```

The result is `build/openaliro-presence-token.zip`. It contains an ad-hoc-signed
host app with an embedded smart-card token extension and the VM procedure.

The extension claims the full PIV AID. Do not install it on the main host. Do
not disable Apple's built-in PIV token outside the disposable VM.

## Security boundary

The absence of firmware-enforced PIV `VERIFY` is intentional only in
`CONFIG_WOZ_PIV_UWB_ONLY` builds. The private keys remain in ESP32 encrypted NVS
and are never exported. Slot 9A fails closed if a fresh Wallet/UWB proof fails.
Slot 9D is available whenever the physical token is attached, because macOS
needs it before LoginWindow can unwrap the account secret.

This does not make an iPhone Wallet Home credential ambient. The phone still
has to satisfy Wallet's wake and authorization policy. Password fallback,
a rescue administrator, and a clean VM snapshot remain required.
