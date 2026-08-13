ultrawidelock @VERSION@   ·   nRF5340 DK

The ultrawidelock target that taps. A credential lock with NFC tap and UWB approach
unlock: it joins Apple Home over Thread, your iPhone carries the key in Wallet,
and the lock opens as you walk up or on a tap.


START HERE
──────────────────────────────────────────────────────────────────────────────

  Open FLASH.md in any text editor or on GitHub. That is the full guide, much
  easier to follow than this file.

  Read it before you flash: this target needs three boards wired together, and
  the DWM3000EVB's power-select jumper has to be right before anything works.


THE SHORT VERSION
──────────────────────────────────────────────────────────────────────────────

  1.  Install the SEGGER J-Link software, then nRF Util, then run:
        nrfutil install device
  2.  Seat the DWM3000EVB on the DK's Arduino header, and wire the
      X-NUCLEO-NFC12A1 per the table in FLASH.md.
  3.  Plug in the USB cable and run:  bash flash.sh
  4.  Scan SETUP-QR.png with the Home app, straight from this folder. The
      same QR code and setup code are also printed on the DK's serial console
      at 115200 baud, and the code is in VERSION.txt.

  Then lock it in Home, walk five metres away, and walk back with the phone in
  your pocket. It opens without you touching anything.


WHAT IS IN HERE
──────────────────────────────────────────────────────────────────────────────

  merged.hex           application core: Matter, the credential stack, the UWB engine
  merged_CPUNET.hex    network core: the radio controller
  flash.sh             flashes both cores over the DK's on-board J-Link
  SETUP-QR.png         the commissioning QR code, scannable from here
  FLASH.md             the full guide                           <-- start here
  README.txt           this file
  VERSION.txt          what this build is
  SHA256SUMS.txt       checksums for every file above

  Both hex files are needed. The nRF5340 is a two-core part and flashing only
  the application core leaves the radio dead.


CHECK IT REALLY CAME FROM US
──────────────────────────────────────────────────────────────────────────────

  SHA256SUMS.txt only proves your download is intact. It cannot prove who
  built it: anyone who could alter the firmware could alter the checksums in
  the same motion.

  This release is signed. To prove these bytes came out of ultrawidelock's CI,
  run this against the zip you downloaded:

    gh attestation verify ultrawidelock-nrf5340dk.zip --repo ultrawidelock/ultrawidelock

  It checks a Sigstore signature created while the release was being built,
  and prints the commit and workflow that produced it. It needs the GitHub CLI
  (cli.github.com) and a few seconds. flash.sh runs it for you when it can.


BEFORE YOU TRUST IT WITH ANYTHING
──────────────────────────────────────────────────────────────────────────────

  This is a hobby project, not a product. Two things in particular:

    ·  This image carries no bootloader, so there is no firmware update over
       the air. Updating means another USB flash, which clears the pairing.
    ·  Debug access is left open. Anyone holding the board can read its
       memory, including the lock's private key.

  Do not secure anything valuable with this.


                        https://github.com/ultrawidelock/ultrawidelock
