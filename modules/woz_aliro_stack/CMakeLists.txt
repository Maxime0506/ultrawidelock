# Compile the clean-room implementation directly into the application target.
# The upstream add-on has already created its imported `aliro` target by the
# time extra modules are processed. Zephyr links every registered library under
# --whole-archive, so leaving that target registered would force all duplicate
# definitions into the link. Remove exactly that target from the global list;
# the upstream module still supplies its public headers and integration code.
if(CONFIG_WOZ_ALIRO_SOURCE_STACK)
  get_property(_zephyr_libraries GLOBAL PROPERTY ZEPHYR_LIBS)
  list(REMOVE_ITEM _zephyr_libraries aliro)
  set_property(GLOBAL PROPERTY ZEPHYR_LIBS "${_zephyr_libraries}")

  target_sources(app PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/src/advertising_core.c
    ${CMAKE_CURRENT_SOURCE_DIR}/src/protocol/ble_message.c
    ${CMAKE_CURRENT_SOURCE_DIR}/src/protocol/ble_timeout.c
    ${CMAKE_CURRENT_SOURCE_DIR}/src/protocol/tlv.c
    ${CMAKE_CURRENT_SOURCE_DIR}/src/protocol/nfc_select.c
    ${CMAKE_CURRENT_SOURCE_DIR}/src/protocol/nfc_auth.c
    ${CMAKE_CURRENT_SOURCE_DIR}/src/protocol/nfc_step_up.c
    ${CMAKE_CURRENT_SOURCE_DIR}/src/protocol/access_document.c
    ${CMAKE_CURRENT_SOURCE_DIR}/src/session.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/aliro_stack.cpp)
  target_include_directories(app PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src)
endif()
