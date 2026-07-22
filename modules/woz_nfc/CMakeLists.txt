# Woz NFC transport seam. Compiled straight into the application target, the
# same mechanism woz_aliro_stack uses: the seam functions must resolve for the
# patched add-on call sites, and the backends need the add-on's global include
# paths (aliro headers, reader_storage, aliro_workqueue).
if(CONFIG_WOZ_NFC)
  target_include_directories(app PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/include)

  if(CONFIG_WOZ_NFC_TRANSPORT_RFAL)
    target_sources(app PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src/transport_rfal.cpp)
  elseif(CONFIG_WOZ_NFC_TRANSPORT_PN532)
    target_sources(app PRIVATE
      ${CMAKE_CURRENT_SOURCE_DIR}/src/transport_pn532.cpp
      ${CMAKE_CURRENT_SOURCE_DIR}/src/pn532.c
      ${CMAKE_CURRENT_SOURCE_DIR}/src/pn532_apdu.c
      ${CMAKE_CURRENT_SOURCE_DIR}/src/pn532_bus_spi.c)
  elseif(CONFIG_WOZ_NFC_TRANSPORT_NONE)
    target_sources(app PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src/transport_none.cpp)
  endif()
endif()
