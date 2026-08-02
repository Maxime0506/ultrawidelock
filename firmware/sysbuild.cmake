# Sysbuild-level build guards: the APPROTECT lock, and the image-signing key.
# Both are settings that are cheap to get wrong and expensive to discover late,
# and both are invisible to firmware/CMakeLists.txt for the same reason.
#
# CMakeLists.txt catches CONFIG_NRF_*APPROTECT_LOCK on the application image.
# It cannot see SB_CONFIG_*, because those are evaluated one level up, before
# any image is configured -- and sysbuild is exactly where an APPROTECT setting
# would now be written, since MCUboot arrived as a second image with a config
# file of its own. Loaded automatically: sysbuild includes <app>/sysbuild.cmake
# if it exists (zephyr/share/sysbuild/cmake/modules/sysbuild_extensions.cmake:929).
#
# Why this matters more than it looks: the lock re-asserts in SystemInit() on
# every boot, and the only exit is `nrfjprog --recover`, which mass-erases flash
# AND UICR. On this board that destroys settings_storage at 0x7e000 -- the Matter
# fabrics, the trust anchors, and the reader private key that every iPhone key
# was provisioned against. Not a brick; a permanent loss of the board's identity.
if(SB_CONFIG_APPROTECT_LOCK OR SB_CONFIG_SECURE_APPROTECT_LOCK)
	message(FATAL_ERROR
		"APPROTECT lock is enabled in sysbuild. Refusing to build.\n"
		"  SB_CONFIG_APPROTECT_LOCK        = ${SB_CONFIG_APPROTECT_LOCK}\n"
		"  SB_CONFIG_SECURE_APPROTECT_LOCK = ${SB_CONFIG_SECURE_APPROTECT_LOCK}\n"
		"See firmware/sysbuild.conf for why this board never locks.")
endif()

# ---- the image-signing key ---------------------------------------------------
#
# MCUboot boots slot 0 only if it verifies against a public key compiled into the
# bootloader, so the private half is the entire answer to "what firmware will
# this lock run". Configure nothing and MCUboot signs with root-ec-p256.pem out
# of its OWN repository, which is published there: every stock MCUboot in the
# world accepts images signed with it. On a lock that is not a signing key, it
# is a formality.
#
# MCUboot does notice, at bootloader/mcuboot/boot/zephyr/CMakeLists.txt:449-452,
# and calls message(WARNING). That is precisely why it survived here: a warning
# in a ten-thousand-line build log is indistinguishable from no warning. These
# are fatal instead.
#
# mcuboot_demo_keys is MCUboot's own list, copied from lines 439-447 of that
# file. Compared by BASENAME, so a demo key copied somewhere else is still
# caught -- the check is about which key, not which path.
#
# Ordering is deliberate: the demo-name check runs before the absolute-path
# check so that a RELATIVE demo path reports the useful failure rather than the
# pedantic one.
#
# SB_CONFIG_* is populated here: Kconfig must run before images are added (it is
# SB_CONFIG_BOOTLOADER_MCUBOOT that causes the MCUboot image to exist at all),
# and sysbuild.cmake is included only after they have been
# (sysbuild_extensions.cmake:906-931).
set(mcuboot_demo_keys
	root-ec-p256.pem
	root-ec-p256-pkcs8.pem
	root-ec-p384.pem
	root-ec-p384-pkcs8.pem
	root-ed25519.pem
	root-rsa-2048.pem
	root-rsa-3072.pem)

get_filename_component(sign_key_name "${SB_CONFIG_BOOT_SIGNATURE_KEY_FILE}" NAME)

if(NOT SB_CONFIG_BOOT_SIGNATURE_KEY_FILE)
	message(FATAL_ERROR
		"No MCUboot signing key is configured, so this build would fall back to\n"
		"MCUboot's PUBLIC demo key. Refusing to build.\n"
		"  Fix: make dfu-key\n"
		"See firmware/keys/README.md.")
elseif(sign_key_name IN_LIST mcuboot_demo_keys)
	message(FATAL_ERROR
		"MCUboot's published demo key is not a signing key. Refusing to build.\n"
		"  SB_CONFIG_BOOT_SIGNATURE_KEY_FILE = ${SB_CONFIG_BOOT_SIGNATURE_KEY_FILE}\n"
		"  Fix: make dfu-key\n"
		"See firmware/keys/README.md.")
elseif(NOT IS_ABSOLUTE "${SB_CONFIG_BOOT_SIGNATURE_KEY_FILE}")
	message(FATAL_ERROR
		"The signing key path must be ABSOLUTE. A relative one is resolved against\n"
		"the MCUboot repository (boot/zephyr/CMakeLists.txt:428) and lands on the\n"
		"demo key without saying so. Refusing to build.\n"
		"  SB_CONFIG_BOOT_SIGNATURE_KEY_FILE = ${SB_CONFIG_BOOT_SIGNATURE_KEY_FILE}\n"
		"See firmware/keys/README.md.")
elseif(NOT EXISTS "${SB_CONFIG_BOOT_SIGNATURE_KEY_FILE}")
	message(FATAL_ERROR
		"The configured MCUboot signing key does not exist. Refusing to build.\n"
		"  SB_CONFIG_BOOT_SIGNATURE_KEY_FILE = ${SB_CONFIG_BOOT_SIGNATURE_KEY_FILE}\n"
		"  Fix: make dfu-key\n"
		"See firmware/keys/README.md.")
endif()
