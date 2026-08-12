# UltraWideLock NFC transport seam. Compiled straight into the application target, the
# same mechanism ultrawidelock_cred_stack uses: the seam functions must resolve for the
# patched add-on call sites, and the backends need the add-on's global include
# paths (ultrawidelock headers, reader_storage, aliro_workqueue).
if(CONFIG_ULTRAWIDELOCK_NFC)
  target_include_directories(app PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/include)

  if(CONFIG_ULTRAWIDELOCK_NFC_TRANSPORT_RFAL)
    target_sources(app PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src/transport_rfal.cpp)
  elseif(CONFIG_ULTRAWIDELOCK_NFC_TRANSPORT_PN532)
    # The Zephyr SPI backend of pn532_bus.h (pn532_bus_spi.c) compiles from
    # ports/zephyr (ultrawidelock_zephyr_port) under this same gate.
    target_sources(app PRIVATE
      ${CMAKE_CURRENT_SOURCE_DIR}/src/transport_pn532.cpp
      ${CMAKE_CURRENT_SOURCE_DIR}/src/pn532.c
      ${CMAKE_CURRENT_SOURCE_DIR}/src/pn532_apdu.c)
  elseif(CONFIG_ULTRAWIDELOCK_NFC_TRANSPORT_NONE)
    target_sources(app PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src/transport_none.cpp)
  endif()
endif()

# RFAL-path ECP (Apple Enhanced Contactless Polling) emitter. Implements the
# RFAL proprietary-poll hook (NfcPropInit / NfcPropGetCallbacks) that the
# add-on's subsys/nfc_prop declares, so a matching iPhone auto-presents its
# Credential pass (Express / no Face ID). Gated on the add-on's
# CONFIG_DOOR_LOCK_NFC_PROP, independent of CONFIG_ULTRAWIDELOCK_NFC: the RFAL build
# uses this hook without the transport seam. Compiles against the add-on's
# RFAL, nfc_prop, reader_storage and ultrawidelock headers (global Zephyr include
# dirs). transport_pn532.cpp emits the same frame on the PN532 path.
if(CONFIG_DOOR_LOCK_NFC_PROP)
  zephyr_library_named(ultrawidelock_nfc_ecp)
  zephyr_library_sources(${CMAKE_CURRENT_SOURCE_DIR}/src/nfc_prop_ecp.cpp)
endif()
