# DWM3001CDK standalone Aliro reader — stage 0 skeleton.
#
# Purpose of this stage is a NUMBER, not a feature: link the real reader engine,
# the real CCC/FiRa responder, the real DW3110 driver and a real BLE stack into
# one nRF52833 image and see what it weighs against the 480 KB the single-image
# partition layout gives us.
#
# woz_uwb and deps/dw3000 come in as Zephyr modules because they carry Kconfig
# gating and -Wl,--wrap link flags. modules/woz_aliro's reader core is listed
# directly here, the way ports/nrf5340dk/on_target_ec does, so the nRF5340 build
# that consumes that module through CONFIG_WOZ_ALIRO_STACK stays untouched.
cmake_minimum_required(VERSION 3.20.0)

get_filename_component(REPO_ROOT "${CMAKE_CURRENT_LIST_DIR}/../../.." ABSOLUTE)
# modules/woz_aliro is listed for its Kconfig alone: the reader sources use the
# LOG_MODULE the `module = WOZ_ALIRO` template declares, so CONFIG_WOZ_ALIRO_LOG_LEVEL
# has to exist. Its CMakeLists is gated on CONFIG_WOZ_ALIRO_STACK, which stays n
# here, so it contributes no sources and the nRF5340 build is unaffected.
list(APPEND ZEPHYR_EXTRA_MODULES
	"${REPO_ROOT}/modules/woz_aliro"
	"${REPO_ROOT}/modules/woz_matter"
	"${REPO_ROOT}/modules/woz_uwb"
	"${REPO_ROOT}/deps/dw3000")

find_package(Zephyr REQUIRED HINTS $ENV{ZEPHYR_BASE})
project(aliro_reader_dwm3001cdk)

set(ALIRO "${REPO_ROOT}/modules/woz_aliro")

target_sources(app PRIVATE
	src/main.c
	# Platform seams the reader engine links against. Stage 0 fills them with
	# the smallest thing that keeps the call graph honest; the real Zephyr BLE
	# transport is stage 4 and the settings-backed store is stage 1.
	src/aliro_ble_zephyr.c
	src/aliro_prov_settings.c
	# Reader engine.
	${ALIRO}/src/aliro_reader.c
	${ALIRO}/src/aliro_ranging.c
	${ALIRO}/src/aliro_apdu.c
	${ALIRO}/src/aliro_crypto.c
	${ALIRO}/src/aliro_hash.c
	${ALIRO}/src/aliro_prim_psa.c
	${ALIRO}/src/aliro_advtag.c
	${ALIRO}/src/aliro_prov.c
	${ALIRO}/src/aliro_lat.c
	${ALIRO}/src/aliro_rssi_gate.c
	${ALIRO}/src/aliro_approach.c
)
# Provisioning console (USB CDC-ACM, provisioning mode only).
target_sources_ifdef(CONFIG_ALIRO_PROV_CONSOLE app PRIVATE src/prov_shell.c)
# Measurement only; CONFIG_ALIRO_THREAD_GATE defaults n so this is absent from
# every shipping image.
target_sources_ifdef(CONFIG_ALIRO_THREAD_GATE app PRIVATE src/thread_gate.c)
target_sources_ifdef(CONFIG_ALIRO_CASE_BENCH app PRIVATE src/case_bench.c)
target_sources_ifdef(CONFIG_ALIRO_MATTER_BLE app PRIVATE src/matter_ble_zephyr.c)

# CONFIG_MBEDTLS_MEMORY_DEBUG=y is a NO-OP in this build and sets no define.
# nrf_security only wires that symbol through configs/legacy_crypto_config.h.template
# (`#cmakedefine MBEDTLS_MEMORY_DEBUG`, line 1268); a PSA-only build
# (CONFIG_OBERON_ONLY_PSA_ENABLED) generates nrf-config.h from a different
# template that has no slot for it, so the Kconfig is accepted and silently
# dropped. The app then compiles against a declaration the library never
# defined and the link fails on mbedtls_memory_buffer_alloc_max_get.
#
# Define it for every target instead. Safe because the only struct it changes,
# buffer_alloc_ctx, is file-local to mbedtls/library/memory_buffer_alloc.c, so
# there is no layout to get out of step across compilation units.
if(CONFIG_ALIRO_HEAP_PROBE)
	zephyr_compile_definitions(MBEDTLS_MEMORY_DEBUG)
endif()
target_include_directories(app PRIVATE ${ALIRO}/include ${ALIRO}/src)
