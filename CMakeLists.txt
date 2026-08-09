cmake_minimum_required(VERSION 3.16)

file(STRINGS "${CMAKE_CURRENT_SOURCE_DIR}/VERSION" OPENALIRO_VERSION LIMIT_COUNT 1)
if(NOT OPENALIRO_VERSION MATCHES "^[0-9]+[.][0-9]+[.][0-9]+$")
	message(FATAL_ERROR "VERSION must contain one semantic version: major.minor.patch")
endif()

project(OpenAliro VERSION "${OPENALIRO_VERSION}" LANGUAGES C)

include(CMakePackageConfigHelpers)
include(GNUInstallDirs)

add_library(openaliro_headers INTERFACE)
add_library(OpenAliro::headers ALIAS openaliro_headers)
set_target_properties(openaliro_headers PROPERTIES EXPORT_NAME headers)

target_include_directories(openaliro_headers INTERFACE
	$<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
	$<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>)

set(OPENALIRO_MODULE_INCLUDE_DIRS
	"${CMAKE_CURRENT_SOURCE_DIR}/modules/woz_aliro/include"
	"${CMAKE_CURRENT_SOURCE_DIR}/modules/woz_anchor/include"
	"${CMAKE_CURRENT_SOURCE_DIR}/modules/woz_dfu/include"
	"${CMAKE_CURRENT_SOURCE_DIR}/modules/woz_dw3000/include"
	"${CMAKE_CURRENT_SOURCE_DIR}/modules/woz_matter/include"
	"${CMAKE_CURRENT_SOURCE_DIR}/modules/woz_ml/include"
	"${CMAKE_CURRENT_SOURCE_DIR}/modules/woz_nfc/include"
	"${CMAKE_CURRENT_SOURCE_DIR}/modules/woz_port/include"
	"${CMAKE_CURRENT_SOURCE_DIR}/modules/woz_uwb/include")
foreach(include_dir IN LISTS OPENALIRO_MODULE_INCLUDE_DIRS)
	target_include_directories(openaliro_headers INTERFACE
		$<BUILD_INTERFACE:${include_dir}>)
endforeach()

set(OPENALIRO_PACKAGE_HEADERS
	"include/openaliro/openaliro.h|openaliro.h"
	"modules/woz_aliro/include/openaliro/device.h|device.h"
	"modules/woz_aliro/include/openaliro/reader.h|reader.h"
	"modules/woz_aliro/include/openaliro/tlv.h|tlv.h"
	"modules/woz_uwb/include/openaliro/uwb.h|uwb.h"
	"modules/woz_port/include/openaliro/woz_hal.h|woz_hal.h"
	"modules/woz_aliro/include/aliro_advtag.h|aliro_advtag.h"
	"modules/woz_aliro/include/aliro_apdu.h|aliro_apdu.h"
	"modules/woz_aliro/include/aliro_ble.h|aliro_ble.h"
	"modules/woz_aliro/include/aliro_ble_central.h|aliro_ble_central.h"
	"modules/woz_aliro/include/aliro_crypto.h|aliro_crypto.h"
	"modules/woz_aliro/include/aliro_device_apdu.h|aliro_device_apdu.h"
	"modules/woz_aliro/include/aliro_prov.h|aliro_prov.h"
	"modules/woz_dw3000/include/dw3000_hw.h|dw3000_hw.h"
	"modules/woz_dw3000/include/dw3000_spi.h|dw3000_spi.h")
set(OPENALIRO_INSTALLED_HEADER_PATHS)
foreach(header_mapping IN LISTS OPENALIRO_PACKAGE_HEADERS)
	string(REPLACE "|" ";" header_fields "${header_mapping}")
	list(GET header_fields 0 header_source)
	list(GET header_fields 1 installed_header)
	list(FIND OPENALIRO_INSTALLED_HEADER_PATHS "${installed_header}" header_index)
	if(NOT header_index EQUAL -1)
		message(FATAL_ERROR "public header install collision: ${installed_header}")
	endif()
	list(APPEND OPENALIRO_INSTALLED_HEADER_PATHS "${installed_header}")
	get_filename_component(header_subdir "${installed_header}" DIRECTORY)
	install(FILES "${CMAKE_CURRENT_SOURCE_DIR}/${header_source}"
		DESTINATION "${CMAKE_INSTALL_INCLUDEDIR}/openaliro/${header_subdir}")
endforeach()

add_library(openaliro_tlv STATIC modules/woz_aliro/src/aliro_tlv.c)
add_library(OpenAliro::tlv ALIAS openaliro_tlv)
set_target_properties(openaliro_tlv PROPERTIES
	C_STANDARD 11
	C_STANDARD_REQUIRED YES
	EXPORT_NAME tlv
	OUTPUT_NAME openaliro_tlv)
target_link_libraries(openaliro_tlv PUBLIC openaliro_headers)

install(TARGETS openaliro_headers openaliro_tlv
	EXPORT OpenAliroTargets
	ARCHIVE DESTINATION "${CMAKE_INSTALL_LIBDIR}")
install(EXPORT OpenAliroTargets
	FILE OpenAliroTargets.cmake
	NAMESPACE OpenAliro::
	DESTINATION "${CMAKE_INSTALL_LIBDIR}/cmake/OpenAliro")

configure_package_config_file(
	cmake/OpenAliroConfig.cmake.in
	"${CMAKE_CURRENT_BINARY_DIR}/OpenAliroConfig.cmake"
	INSTALL_DESTINATION "${CMAKE_INSTALL_LIBDIR}/cmake/OpenAliro")
write_basic_package_version_file(
	"${CMAKE_CURRENT_BINARY_DIR}/OpenAliroConfigVersion.cmake"
	VERSION "${PROJECT_VERSION}"
	COMPATIBILITY SameMinorVersion)
install(FILES
	"${CMAKE_CURRENT_BINARY_DIR}/OpenAliroConfig.cmake"
	"${CMAKE_CURRENT_BINARY_DIR}/OpenAliroConfigVersion.cmake"
	DESTINATION "${CMAKE_INSTALL_LIBDIR}/cmake/OpenAliro")
