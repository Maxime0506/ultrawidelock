# Compile the independent implementation directly into the application target.
# The upstream add-on has already created its imported `aliro` target by the
# time extra modules are processed. Zephyr links every registered library under
# --whole-archive, so leaving that target registered would force all duplicate
# definitions into the link. Remove exactly that target from the global list;
# the upstream module still supplies its public headers and integration code.
if(CONFIG_WOZ_ALIRO_SOURCE_STACK)
  get_property(_zephyr_libraries GLOBAL PROPERTY ZEPHYR_LIBS)
  list(REMOVE_ITEM _zephyr_libraries aliro)
  set_property(GLOBAL PROPERTY ZEPHYR_LIBS "${_zephyr_libraries}")

  # The step-up wire codecs, DeviceResponse parser and TLV codec are the shared
  # modules/woz_aliro sources (one source of protocol truth). They are
  # crypto-free: session.cpp encrypts through Nordic's Interface, so none of
  # woz_aliro's crypto units are pulled in.
  get_filename_component(WOZ_ALIRO "${CMAKE_CURRENT_SOURCE_DIR}/../woz_aliro" ABSOLUTE)

  target_sources(app PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/src/advertising_core.c
    ${CMAKE_CURRENT_SOURCE_DIR}/src/protocol/ble_message.c
    ${CMAKE_CURRENT_SOURCE_DIR}/src/protocol/ble_timeout.c
    ${CMAKE_CURRENT_SOURCE_DIR}/src/protocol/nfc_select.c
    ${CMAKE_CURRENT_SOURCE_DIR}/src/protocol/nfc_auth.c
    ${WOZ_ALIRO}/src/aliro_tlv.c
    ${WOZ_ALIRO}/src/aliro_stepup_wire.c
    ${WOZ_ALIRO}/src/aliro_stepup_parse.c
    ${CMAKE_CURRENT_SOURCE_DIR}/src/session.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/aliro_stack.cpp)
  target_include_directories(app PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/src
    ${WOZ_ALIRO}/include
    ${WOZ_ALIRO}/src)
endif()
