cmake_minimum_required(VERSION 3.16)

file(STRINGS "${CMAKE_CURRENT_SOURCE_DIR}/VERSION" ULTRAWIDELOCK_VERSION LIMIT_COUNT 1)
if(NOT ULTRAWIDELOCK_VERSION MATCHES "^[0-9]+[.][0-9]+[.][0-9]+$")
	message(FATAL_ERROR "VERSION must contain one semantic version: major.minor.patch")
endif()

project(UltraWideLock VERSION "${ULTRAWIDELOCK_VERSION}" LANGUAGES C)

include(CMakePackageConfigHelpers)
include(GNUInstallDirs)

add_library(ultrawidelock_headers INTERFACE)
add_library(UltraWideLock::headers ALIAS ultrawidelock_headers)
set_target_properties(ultrawidelock_headers PROPERTIES EXPORT_NAME headers)

target_include_directories(ultrawidelock_headers INTERFACE
	$<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
	$<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>)

set(ULTRAWIDELOCK_MODULE_INCLUDE_DIRS
	"${CMAKE_CURRENT_SOURCE_DIR}/modules/woz_aliro/include"
	"${CMAKE_CURRENT_SOURCE_DIR}/modules/woz_anchor/include"
	"${CMAKE_CURRENT_SOURCE_DIR}/modules/woz_dfu/include"
	"${CMAKE_CURRENT_SOURCE_DIR}/modules/woz_dw3000/include"
	"${CMAKE_CURRENT_SOURCE_DIR}/modules/woz_matter/include"
	"${CMAKE_CURRENT_SOURCE_DIR}/modules/woz_ml/include"
	"${CMAKE_CURRENT_SOURCE_DIR}/modules/woz_nfc/include"
	"${CMAKE_CURRENT_SOURCE_DIR}/modules/woz_port/include"
	"${CMAKE_CURRENT_SOURCE_DIR}/modules/woz_uwb/include")
foreach(include_dir IN LISTS ULTRAWIDELOCK_MODULE_INCLUDE_DIRS)
	target_include_directories(ultrawidelock_headers INTERFACE
		$<BUILD_INTERFACE:${include_dir}>)
endforeach()

set(ULTRAWIDELOCK_PACKAGE_HEADERS
	"include/ultrawidelock/ultrawidelock.h|ultrawidelock.h"
	"modules/woz_aliro/include/ultrawidelock/device.h|device.h"
	"modules/woz_aliro/include/ultrawidelock/reader.h|reader.h"
	"modules/woz_aliro/include/ultrawidelock/tlv.h|tlv.h"
	"modules/woz_uwb/include/ultrawidelock/uwb.h|uwb.h"
	"modules/woz_port/include/ultrawidelock/woz_hal.h|woz_hal.h"
	"modules/woz_aliro/include/aliro_advtag.h|aliro_advtag.h"
	"modules/woz_aliro/include/aliro_apdu.h|aliro_apdu.h"
	"modules/woz_aliro/include/aliro_ble.h|aliro_ble.h"
	"modules/woz_aliro/include/aliro_ble_central.h|aliro_ble_central.h"
	"modules/woz_aliro/include/aliro_crypto.h|aliro_crypto.h"
	"modules/woz_aliro/include/aliro_device_apdu.h|aliro_device_apdu.h"
	"modules/woz_aliro/include/aliro_prov.h|aliro_prov.h"
	"modules/woz_dw3000/include/dw3000_hw.h|dw3000_hw.h"
	"modules/woz_dw3000/include/dw3000_spi.h|dw3000_spi.h")
set(ULTRAWIDELOCK_INSTALLED_HEADER_PATHS)
foreach(header_mapping IN LISTS ULTRAWIDELOCK_PACKAGE_HEADERS)
	string(REPLACE "|" ";" header_fields "${header_mapping}")
	list(GET header_fields 0 header_source)
	list(GET header_fields 1 installed_header)
	list(FIND ULTRAWIDELOCK_INSTALLED_HEADER_PATHS "${installed_header}" header_index)
	if(NOT header_index EQUAL -1)
		message(FATAL_ERROR "public header install collision: ${installed_header}")
	endif()
	list(APPEND ULTRAWIDELOCK_INSTALLED_HEADER_PATHS "${installed_header}")
	get_filename_component(header_subdir "${installed_header}" DIRECTORY)
	install(FILES "${CMAKE_CURRENT_SOURCE_DIR}/${header_source}"
		DESTINATION "${CMAKE_INSTALL_INCLUDEDIR}/ultrawidelock/${header_subdir}")
endforeach()

add_library(ultrawidelock_tlv STATIC modules/woz_aliro/src/aliro_tlv.c)
add_library(UltraWideLock::tlv ALIAS ultrawidelock_tlv)
set_target_properties(ultrawidelock_tlv PROPERTIES
	C_STANDARD 11
	C_STANDARD_REQUIRED YES
	EXPORT_NAME tlv
	OUTPUT_NAME ultrawidelock_tlv)
target_link_libraries(ultrawidelock_tlv PUBLIC ultrawidelock_headers)

install(TARGETS ultrawidelock_headers ultrawidelock_tlv
	EXPORT UltraWideLockTargets
	ARCHIVE DESTINATION "${CMAKE_INSTALL_LIBDIR}")
install(EXPORT UltraWideLockTargets
	FILE UltraWideLockTargets.cmake
	NAMESPACE UltraWideLock::
	DESTINATION "${CMAKE_INSTALL_LIBDIR}/cmake/UltraWideLock")

configure_package_config_file(
	cmake/UltraWideLockConfig.cmake.in
	"${CMAKE_CURRENT_BINARY_DIR}/UltraWideLockConfig.cmake"
	INSTALL_DESTINATION "${CMAKE_INSTALL_LIBDIR}/cmake/UltraWideLock")
write_basic_package_version_file(
	"${CMAKE_CURRENT_BINARY_DIR}/UltraWideLockConfigVersion.cmake"
	VERSION "${PROJECT_VERSION}"
	COMPATIBILITY SameMinorVersion)
install(FILES
	"${CMAKE_CURRENT_BINARY_DIR}/UltraWideLockConfig.cmake"
	"${CMAKE_CURRENT_BINARY_DIR}/UltraWideLockConfigVersion.cmake"
	DESTINATION "${CMAKE_INSTALL_LIBDIR}/cmake/UltraWideLock")
