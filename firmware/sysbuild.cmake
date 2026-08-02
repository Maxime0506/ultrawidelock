# Sysbuild-level half of the APPROTECT guard.
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
