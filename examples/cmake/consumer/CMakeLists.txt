cmake_minimum_required(VERSION 3.16)

project(openaliro_consumer LANGUAGES C)

set(OPENALIRO_REQUIRED_VERSION "" CACHE STRING
	"OpenAliro version required from an installed package")
set(OPENALIRO_SOURCE_DIR "" CACHE PATH
	"OpenAliro source tree to consume with add_subdirectory")

if(OPENALIRO_SOURCE_DIR)
	add_subdirectory("${OPENALIRO_SOURCE_DIR}" openaliro)
elseif(OPENALIRO_REQUIRED_VERSION)
	find_package(OpenAliro ${OPENALIRO_REQUIRED_VERSION} CONFIG REQUIRED)
else()
	find_package(OpenAliro CONFIG REQUIRED)
endif()

add_executable(openaliro_consumer main.c hal_contract.c)
target_link_libraries(openaliro_consumer PRIVATE OpenAliro::tlv)
