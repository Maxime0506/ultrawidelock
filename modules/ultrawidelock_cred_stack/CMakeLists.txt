# Compile the independent implementation directly into the application target.
# The upstream add-on has already created its imported `aliro` target by the
# time extra modules are processed. Zephyr links every registered library under
# --whole-archive, so leaving that target registered would force all duplicate
# definitions into the link. Remove exactly that target from the global list;
# the upstream module still supplies its public headers and integration code.
#
# `aliro` is upstream's target name (ncs-door-lock-and-access-control's
# lib/aliro/CMakeLists.txt gates the blob import on `if(NOT TARGET aliro)`), so
# it is not ours to rename. Asserted rather than assumed because REMOVE_ITEM is
# silent when the item is absent: renaming this string does not fail here, it
# fails hundreds of objects later as duplicate definitions of Aliro::AliroStack.
if(CONFIG_ULTRAWIDELOCK_CRED_SOURCE_STACK)
  get_property(_zephyr_libraries GLOBAL PROPERTY ZEPHYR_LIBS)
  if(NOT "aliro" IN_LIST _zephyr_libraries)
    message(FATAL_ERROR
      "Expected upstream's `aliro` library target to be registered, so the "
      "source stack could displace it. It is not in ZEPHYR_LIBS. Either the "
      "add-on changed the name, or this string did.")
  endif()
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
