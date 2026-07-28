# Disposable VM installation

Use only in the snapshotted Parallels macOS 26 guest. Keep password fallback
and the separate rescue administrator. Do not enable `enforceSmartCard`.

## 1. Flash the UWB-only profile from the host

Connect USB-UART to the host, close its serial monitor, and disconnect USB-OTG
from the VM while flashing:

```sh
cd ports/esp32/apps/matter-lock
make piv-uwb-app-flash PORT=<host-uart-port>
```

This is an app-only flash. It preserves the Matter fabric, Aliro credential,
PIV keys, certificates, PIN, and account pairing.

## 2. Install and register the extension in the VM

Extract the archive and run:

```sh
APP="/Applications/OpenAliro Presence Token.app"
EXT="$APP/Contents/PlugIns/PresenceTokenExtension.appex"

sudo ditto "OpenAliro Presence Token.app" \
  "$APP"

open "$APP"
pluginkit -a "$EXT"
pluginkit -e use \
  -p com.apple.ctk-tokens \
  -i org.openaliro.presence-token.extension

pluginkit -m -A -D -v \
  -p com.apple.ctk-tokens \
  -i org.openaliro.presence-token.extension

sudo sc_auth enable_for_login -c org.openaliro.presence-token.extension
```

The detailed `pluginkit` query must report the extension path under the app and
must not show an ignored (`-`) election. If it reports no match, stop before
changing the built-in driver.

Disconnect and reconnect USB-OTG to the VM. Confirm the custom class still
appears before changing the built-in driver:

```sh
pluginkit -m -p com.apple.ctk-tokens |
  grep -F org.openaliro.presence-token.extension
```

## 3. Switch the VM from Apple's PIV driver

Only after the custom extension is listed:

```sh
sudo security smartcards token -d com.apple.CryptoTokenKit.pivtoken
```

Reconnect USB-OTG again. The token identifier should now start with the custom
class, and the existing authentication identity should retain the same public
key hash:

```sh
security list-smartcards
sc_auth identities
sc_auth list -u "$USER"
```

Do not share the complete token identifier or public-key hash in public logs.

## 4. Test without removing password fallback

Wake the phone, keep it near the reader, lock the VM screen, and select the
smart-card identity if LoginWindow offers a choice. The expected behavior is:

1. LoginWindow still presents its PIN field on macOS 26.4.1;
2. submit the provisioned test PIN; do not test an incorrect PIN because PIV
   retry counters may be consumed;
3. Wallet/Aliro/UWB runs after submission;
4. LoginWindow unlocks only after the fresh range succeeds;
5. a timeout or distant phone leaves the VM locked.

This verifies the custom provider and firmware UWB gate. It does not verify
PINless native screen unlock; LoginWindow imposes the nonempty PIN submission
before requesting the token signature.

## Roll back

From the VM:

```sh
sudo security smartcards token -e com.apple.CryptoTokenKit.pivtoken
sudo mv "/Applications/OpenAliro Presence Token.app" \
  "/Users/Shared/OpenAliro Presence Token.disabled.app"
```

Then reconnect USB-OTG. From the host, restore the PIN-enforced firmware without
erasing NVS:

```sh
cd ports/esp32/apps/matter-lock
make piv-pin-app-flash PORT=<host-uart-port>
```
