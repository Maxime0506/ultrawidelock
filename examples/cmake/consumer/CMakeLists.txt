cmake_minimum_required(VERSION 3.16)

project(ultrawidelock_consumer LANGUAGES C)

set(ULTRAWIDELOCK_REQUIRED_VERSION "" CACHE STRING
	"UltraWideLock version required from an installed package")
set(ULTRAWIDELOCK_SOURCE_DIR "" CACHE PATH
	"UltraWideLock source tree to consume with add_subdirectory")

if(ULTRAWIDELOCK_SOURCE_DIR)
	add_subdirectory("${ULTRAWIDELOCK_SOURCE_DIR}" ultrawidelock)
elseif(ULTRAWIDELOCK_REQUIRED_VERSION)
	find_package(UltraWideLock ${ULTRAWIDELOCK_REQUIRED_VERSION} CONFIG REQUIRED)
else()
	find_package(UltraWideLock CONFIG REQUIRED)
endif()

add_executable(ultrawidelock_consumer main.c hal_contract.c)
target_link_libraries(ultrawidelock_consumer PRIVATE UltraWideLock::tlv)
