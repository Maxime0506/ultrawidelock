# Compile the independent implementation directly into the application target.
# The upstream add-on has already created its imported `aliro` target by the
# time extra modules are processed. Zephyr links every registered library under
# --whole-archive, so leaving that target registered would force all duplicate
# definitions into the link. Remove exactly that target from the global list;
# the upstream module still supplies its public headers and integration code.
if(CONFIG_ULTRAWIDELOCK_CRED_SOURCE_STACK)
  get_property(_zephyr_libraries GLOBAL PROPERTY ZEPHYR_LIBS)
  list(REMOVE_ITEM _zephyr_libraries aliro)
  set_property(GLOBAL PROPERTY ZEPHYR_LIBS "${_zephyr_libraries}")

  # The step-up wire codecs, DeviceResponse parser and TLV codec are the shared
  # modules/ultrawidelock_cred sources (one source of protocol truth). They are
  # crypto-free: session.cpp encrypts through Nordic's Interface, so none of
  # ultrawidelock_cred's crypto units are pulled in.
  get_filename_component(ULTRAWIDELOCK_CRED "${CMAKE_CURRENT_SOURCE_DIR}/../ultrawidelock_cred" ABSOLUTE)

  target_sources(app PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/src/advertising_core.c
    ${CMAKE_CURRENT_SOURCE_DIR}/src/protocol/ble_message.c
    ${CMAKE_CURRENT_SOURCE_DIR}/src/protocol/ble_timeout.c
    ${CMAKE_CURRENT_SOURCE_DIR}/src/protocol/nfc_select.c
    ${CMAKE_CURRENT_SOURCE_DIR}/src/protocol/nfc_auth.c
    ${ULTRAWIDELOCK_CRED}/src/ultrawidelock_tlv.c
    ${ULTRAWIDELOCK_CRED}/src/ultrawidelock_stepup_wire.c
    ${ULTRAWIDELOCK_CRED}/src/ultrawidelock_stepup_parse.c
    ${CMAKE_CURRENT_SOURCE_DIR}/src/session.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/cred_stack.cpp)
  target_include_directories(app PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/src
    ${ULTRAWIDELOCK_CRED}/include
    ${ULTRAWIDELOCK_CRED}/src)
endif()
