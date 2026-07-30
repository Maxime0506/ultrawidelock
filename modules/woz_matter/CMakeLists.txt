# Hand-written minimal Matter node as a Zephyr module.
# Contributes nothing unless CONFIG_WOZ_MATTER=y (every existing build stays
# byte-identical). See internal/cdk-matter-plan.md for why this exists at all:
# CHIP does not fit the nRF52833 and rs-matter cannot supply a Door Lock
# cluster, so the DWM3001CDK can only self-provision from Apple Home if the
# node is written by hand.
#
# Platform-agnostic C11 with no Zephyr dependency, so tests/host compiles the
# same sources directly (tests/host/sources.sh).

if(CONFIG_WOZ_MATTER)

  set(SRC ${CMAKE_CURRENT_SOURCE_DIR}/src)

  zephyr_library_named(woz_matter)
  zephyr_library_sources(
    ${SRC}/matter_tlv.c
    ${SRC}/matter_msg.c
    ${SRC}/matter_mrp.c
    ${SRC}/matter_crypto.c
    ${SRC}/matter_btp.c
    ${SRC}/matter_pase.c
    ${SRC}/matter_spake2p.c)

  zephyr_include_directories(${CMAKE_CURRENT_SOURCE_DIR}/include)

  # matter_crypto.c uses aliro_hash.h for SHA-256/HKDF, and the
  # crypto_aes_ecb_encrypt seam it declares itself. Both already ship in any
  # image that has the reader -- aliro_hash.c is compiled by the port, and the
  # AES backend by the CONFIG_WOZ_CRYPTO_* choice -- so this is an include path,
  # not new code. It is still a real dependency, which CONFIG_WOZ_MATTER records
  # with `depends on WOZ_ALIRO`.
  #
  # The host suite never caught this: tests/host/sources.sh puts
  # modules/woz_aliro/src on the include path for its own reasons, so
  # matter_crypto.c compiled there and only failed on the first Zephyr build.
  zephyr_library_include_directories(${CMAKE_CURRENT_SOURCE_DIR}/../woz_aliro/src)

endif(CONFIG_WOZ_MATTER)
