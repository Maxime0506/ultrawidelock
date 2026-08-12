# Hand-written minimal Matter node as a Zephyr module.
# Contributes nothing unless CONFIG_ULTRAWIDELOCK_MATTER=y (every existing build stays
# byte-identical). Why this exists at all:
# CHIP does not fit the nRF52833 and rs-matter cannot supply a Door Lock
# cluster, so the DWM3001CDK can only self-provision from Apple Home if the
# node is written by hand.
#
# Platform-agnostic C11 with no Zephyr dependency, so tests/host compiles the
# same sources directly (tests/host/sources.sh).

if(CONFIG_ULTRAWIDELOCK_MATTER)

  set(SRC ${CMAKE_CURRENT_SOURCE_DIR}/src)

  zephyr_library_named(ultrawidelock_matter)
  zephyr_library_sources(
    ${SRC}/matter_tlv.c
    ${SRC}/matter_msg.c
    ${SRC}/matter_mrp.c
    ${SRC}/matter_crypto.c
    ${SRC}/matter_btp.c
    ${SRC}/matter_pase.c
    ${SRC}/matter_spake2p.c
    ${SRC}/matter_pase_sm.c
    ${SRC}/matter_exchange.c
    ${SRC}/matter_im.c
    ${SRC}/matter_clusters.c
    ${SRC}/matter_attest.c
    ${SRC}/matter_fabric.c
    ${SRC}/matter_case.c)

  zephyr_include_directories(${CMAKE_CURRENT_SOURCE_DIR}/include)

  # matter_crypto.c uses aliro_hash.h for SHA-256/HKDF, and the
  # crypto_aes_ecb_encrypt seam it declares itself. Both already ship in any
  # image that has the reader -- aliro_hash.c is compiled by the port, and the
  # AES backend by the CONFIG_WOZ_CRYPTO_* choice -- so this is an include path,
  # not new code. It is still a real dependency, which CONFIG_ULTRAWIDELOCK_MATTER records
  # with `depends on WOZ_ALIRO`.
  #
  zephyr_library_include_directories(${CMAKE_CURRENT_SOURCE_DIR}/../woz_aliro/include)

endif(CONFIG_ULTRAWIDELOCK_MATTER)
