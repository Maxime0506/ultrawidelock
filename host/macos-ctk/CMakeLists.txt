cmake_minimum_required(VERSION 3.29)

project(PresenceToken LANGUAGES Swift)

if(NOT CMAKE_GENERATOR STREQUAL "Xcode")
  message(FATAL_ERROR "PresenceToken must be built with the Xcode generator")
endif()

set(CMAKE_OSX_DEPLOYMENT_TARGET "26.0")

add_executable(PresenceTokenExtension MACOSX_BUNDLE
  Extension/PIVCodec.swift
  Extension/PIVTransport.swift
  Extension/Token.swift
  Extension/TokenDriver.swift
  Extension/TokenSession.swift
)
set_target_properties(PresenceTokenExtension PROPERTIES
  BUNDLE_EXTENSION appex
  MACOSX_BUNDLE_INFO_PLIST "${CMAKE_CURRENT_SOURCE_DIR}/Extension/Info.plist"
  XCODE_PRODUCT_TYPE "com.apple.product-type.app-extension"
  XCODE_ATTRIBUTE_APPLICATION_EXTENSION_API_ONLY YES
  XCODE_ATTRIBUTE_CODE_SIGN_ENTITLEMENTS
    "${CMAKE_CURRENT_SOURCE_DIR}/Extension/PresenceTokenExtension.entitlements"
  XCODE_ATTRIBUTE_CODE_SIGNING_ALLOWED NO
  XCODE_ATTRIBUTE_CODE_SIGNING_REQUIRED NO
  XCODE_ATTRIBUTE_PRODUCT_BUNDLE_IDENTIFIER
    "org.openaliro.presence-token.extension"
  XCODE_ATTRIBUTE_SWIFT_VERSION "5.0"
)
target_link_libraries(PresenceTokenExtension PRIVATE
  "-framework CryptoTokenKit"
  "-framework Security"
)

add_executable(PresenceTokenHost MACOSX_BUNDLE Host/main.swift)
set_target_properties(PresenceTokenHost PROPERTIES
  OUTPUT_NAME "OpenAliro Presence Token"
  MACOSX_BUNDLE_INFO_PLIST "${CMAKE_CURRENT_SOURCE_DIR}/Host/Info.plist"
  XCODE_PRODUCT_TYPE "com.apple.product-type.application"
  XCODE_ATTRIBUTE_CODE_SIGNING_ALLOWED NO
  XCODE_ATTRIBUTE_CODE_SIGNING_REQUIRED NO
  XCODE_ATTRIBUTE_PRODUCT_BUNDLE_IDENTIFIER
    "org.openaliro.presence-token"
  XCODE_ATTRIBUTE_SWIFT_VERSION "5.0"
)
add_dependencies(PresenceTokenHost PresenceTokenExtension)
add_custom_command(TARGET PresenceTokenHost POST_BUILD
  COMMAND "${CMAKE_COMMAND}" -E make_directory
    "$<TARGET_BUNDLE_DIR:PresenceTokenHost>/Contents/PlugIns"
  COMMAND "${CMAKE_COMMAND}" -E copy_directory
    "$<TARGET_BUNDLE_DIR:PresenceTokenExtension>"
    "$<TARGET_BUNDLE_DIR:PresenceTokenHost>/Contents/PlugIns/PresenceTokenExtension.appex"
  VERBATIM
)
