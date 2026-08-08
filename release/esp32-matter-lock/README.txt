openaliro @VERSION@   ·   ESP32-S3 / C5 / C6

The complete Aliro Matter lock on a single ESP32. It commissions into Apple
Home over Wi-Fi, your iPhone carries the key in Wallet, and the lock opens as
you walk up, phone in your pocket. No NFC tap on these boards.


START HERE
──────────────────────────────────────────────────────────────────────────────

  Open FLASH.md in any text editor or on GitHub. That is the full guide, much
  easier to follow than this file.

  Read it before you flash: the UWB radio is hand-wired over SPI, eleven
  connections, and the DWM3000EVB's power-select jumper has to be right before
  anything works.


THE EASY WAY: FLASH FROM YOUR BROWSER
──────────────────────────────────────────────────────────────────────────────

  You do not need this zip, or any tools at all, to flash the board. Open

    https://openaliro.github.io/openaliro/flash/

  in Chrome or Edge, plug the board in, and pick it. That page serves the same
  images published with this release, for all three chips.


THE SHORT VERSION, WITH TOOLS
──────────────────────────────────────────────────────────────────────────────

  1.  Install Python 3, then:  pip install esptool
  2.  Wire the DWM3000EVB per the pin table in FLASH.md. Power it from 3V3,
      never 5 V.
  3.  Plug in a USB data cable and run:  bash flash.sh
      It asks which chip you have, or take it as an argument:
        bash flash.sh --chip esp32c6
  4.  In Apple Home: +, Add Accessory, More options, Enter Code. Then give it
      your 2.4 GHz Wi-Fi.

  SETUP CODE @SETUP_CODE@

  The same code, and a QR to scan instead, are printed on the serial port at
  115200 baud when the board boots.

  Then lock it in Home, walk five metres away, and walk back with the phone in
  your pocket. It opens without you touching anything.


WHAT IS IN HERE
──────────────────────────────────────────────────────────────────────────────

  openaliro-matter-lock-esp32s3.bin   merged S3 image, flashed at 0x0
  openaliro-matter-lock-esp32c5.bin   merged C5 image
  openaliro-matter-lock-esp32c6.bin   merged C6 image
  manifest.json                       chip list for the browser flasher
  flash.sh                            flashes the image for your chip
  FLASH.md                            the full guide            <-- start here
  README.txt                          this file
  VERSION.txt                         what this build is
  SHA256SUMS.txt                      checksums for every file above

  Each .bin is a complete image: bootloader, partition table and application in
  one file, written at offset 0. There is nothing else to flash.

  How far each chip is proven differs, and FLASH.md says exactly what each
  claim covers. In short: the S3 is the hardware-validated target, the C6 has
  its UWB radio validated but not the full Matter walk-up, and the C5 is
  release-built with no hardware validation recorded.


CHECK IT REALLY CAME FROM US
──────────────────────────────────────────────────────────────────────────────

  SHA256SUMS.txt only proves your download is intact. It cannot prove who
  built it: anyone who could alter the firmware could alter the checksums in
  the same motion.

  This release is signed. To prove these bytes came out of openaliro's CI,
  run this against the zip you downloaded:

    gh attestation verify openaliro-esp32-matter-lock.zip --repo openaliro/openaliro

  It checks a Sigstore signature created while the release was being built,
  and prints the commit and workflow that produced it. It needs the GitHub CLI
  (cli.github.com) and a few seconds. flash.sh runs it for you when it can.


BEFORE YOU TRUST IT WITH ANYTHING
──────────────────────────────────────────────────────────────────────────────

  This is a hobby project, not a product. Two things in particular:

    ·  The setup code above is public, and identical for everyone who flashes
       this release. Anyone in Bluetooth range while the lock is waiting to be
       added can add it to their home instead of yours. Commission it promptly.
    ·  Secure boot and flash encryption are both off. Anyone who can reach the
       USB port can read the firmware back, including the lock's private key,
       and can write their own.
    ·  These are evaluation builds carrying Matter test certificates.

  Do not secure anything valuable with this.


                        https://github.com/openaliro/openaliro
