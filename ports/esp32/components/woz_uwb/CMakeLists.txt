# Woz UWB (Aliro DS-TWR responder) engine as an ESP-IDF component.
# Additive: reuses modules/woz_uwb/src + modules/woz_dw3000 from the repo unchanged.
# The engine is platform-neutral: it takes its OS surface from woz_port.h and
# its logging from woz_log.h (modules/woz_port/include), which select the ESP-IDF
# backend on ESP_PLATFORM. No Zephyr compat layer is involved. The DW3000
# platform SPI/GPIO layer is the ESP-IDF backend in port/.
# Source set = the role manifests in modules/{woz_uwb,woz_dw3000}/roles/ — the
# SAME lists modules/woz_uwb/CMakeLists.txt reads for CONFIG_WOZ_UWB +
# RESPONDER + ALIRO + CRYPTO_MBEDTLS (no PRETTY/SHELL/SELFTEST, no uwb_rxdiag:
# those stay Zephyr-module literals) — plus the port/*.c locals below.

get_filename_component(REPO_ROOT "${CMAKE_CURRENT_LIST_DIR}/../../../.." ABSOLUTE)
set(WOZ "${REPO_ROOT}/modules/woz_uwb/src")
set(WOZ_INC "${REPO_ROOT}/modules/woz_uwb/include")
set(WOZ_PORT "${REPO_ROOT}/modules/woz_port/include")
set(DW  "${REPO_ROOT}/modules/woz_dw3000")
set(UWBROLES "${REPO_ROOT}/modules/woz_uwb/roles")
set(DWROLES "${REPO_ROOT}/modules/woz_dw3000/roles")

include("${REPO_ROOT}/cmake/woz_roles.cmake")
# engine: driver + fira (ds_twr = shared ToF estimator; ccc_shim_rx.c calls it)
woz_role_sources("${UWBROLES}/base_driver.list" UWB_BASE_DRIVER_SRCS)
woz_role_sources("${UWBROLES}/base_engine.list" UWB_BASE_ENGINE_SRCS)
woz_role_sources("${UWBROLES}/responder_driver.list" UWB_RESP_DRIVER_SRCS)
woz_role_sources("${UWBROLES}/responder_engine.list" UWB_RESP_ENGINE_SRCS)
# engine: CCC STS (+ mbedTLS AES-ECB seam: no PSA provider on ESP32)
woz_role_sources("${UWBROLES}/ccc_keys.list" UWB_CCC_KEYS_SRCS)
woz_role_sources("${UWBROLES}/ccc_engine.list" UWB_CCC_ENGINE_SRCS)
woz_role_sources("${UWBROLES}/crypto_mbedtls.list" UWB_CRYPTO_SRCS)
# engine: Aliro ranging-setup adapter + M1-M4 codec
woz_role_sources("${UWBROLES}/aliro_adapter.list" UWB_ALIRO_ADAPTER_SRCS)
woz_role_sources("${UWBROLES}/aliro_codec.list" UWB_ALIRO_CODEC_SRCS)
# vendor decadriver (modules/woz_dw3000/roles/ canonical set)
woz_role_sources("${DWROLES}/core.list" DW_CORE_SRCS)
woz_role_sources("${DWROLES}/chip_dw3000.list" DW_CHIP_SRCS)

set(WOZ_SRCS
    ${UWB_BASE_DRIVER_SRCS}
    ${UWB_BASE_ENGINE_SRCS}
    ${UWB_RESP_DRIVER_SRCS}
    ${UWB_RESP_ENGINE_SRCS}
    ${UWB_CCC_KEYS_SRCS}
    ${UWB_CCC_ENGINE_SRCS}
    ${UWB_CRYPTO_SRCS}
    ${UWB_ALIRO_ADAPTER_SRCS}
    ${UWB_ALIRO_CODEC_SRCS}
    ${DW_CORE_SRCS}
    ${DW_CHIP_SRCS}
    # ESP-IDF platform backend (replaces Zephyr dw3000_spi.c / dw3000_hw.c)
    "port/dw3000_spi.c"
    "port/dw3000_hw.c"
    "port/woz_seam_stubs.c"
)

# CIA/CIR diagnostics: per-reception first-path summary + windowed CIR tap dump
# (gated, default n; the RX-shim call sites no-op when unset).
if(CONFIG_WOZ_UWB_CIRDIAG)
    woz_role_sources("${UWBROLES}/diag_cir.list" UWB_DIAG_CIR_SRCS)
    list(APPEND WOZ_SRCS ${UWB_DIAG_CIR_SRCS})
endif()

# Flight recorder: record a live walk-up into a RAM ring for host replay
# (gated, default n; the ccc_shim_rx.c call sites no-op when unset).
if(CONFIG_WOZ_FLIGHT_RECORDER)
    woz_role_sources("${UWBROLES}/flight_recorder.list" UWB_FLIGHT_SRCS)
    list(APPEND WOZ_SRCS ${UWB_FLIGHT_SRCS})
endif()

idf_component_register(
    SRCS ${WOZ_SRCS}
    INCLUDE_DIRS
        "${WOZ_PORT}"          # woz_port.h / woz_log.h — the platform contract
        "${WOZ_INC}"           # facade, seams, diagnostics and ranging-setup API
    PRIV_INCLUDE_DIRS
        "port"                 # board_pins.h
        "${WOZ}/driver" "${WOZ}/fira" "${WOZ}/ccc" "${WOZ}/facade" "${WOZ}/shell"
		"${WOZ}/aliro"
		"${DW}/include"
        "${DW}/dwt_uwb_driver"
        "${DW}/dwt_uwb_driver/lib/qmath/include"
    REQUIRES
        # woz_port carries the ESP-IDF backend of the contract this engine is
        # written against. Named here rather than left to the WOZ_PORT include
        # path above: ESP-IDF only builds a component something requires, so
        # without this the backend is discovered and then silently skipped.
        woz_port
        driver esp_timer mbedtls
)

# The engine + vendor decadriver were written for the Nordic/Zephyr warning
# regime (e.g. int32_t == int, so %d matched). ESP-IDF defaults to -Werror=all,
# which flags benign format/maybe-uninitialized diffs in this reused code. Keep
# warnings visible but non-fatal for this component only (don't edit shared or
# vendor source). Our own port/ files still get reviewed against these warnings.
target_compile_options(${COMPONENT_LIB} PRIVATE
    -Wno-error -Wno-format -Wno-maybe-uninitialized)

# Kconfig symbols the in-tree build gates on, supplied as compile defs.
target_compile_definitions(${COMPONENT_LIB} PRIVATE
    CONFIG_WOZ_UWB=1
    CONFIG_WOZ_UWB_RESPONDER=1
    CONFIG_WOZ_ALIRO=1
    CONFIG_WOZ_CRYPTO_MBEDTLS=1
    CONFIG_DW3000=1
    CONFIG_DW3000_CHIP_DW3000=1
)

# The CCC STS seam is modules/woz_uwb/include/uwb_seam.h: every caller reaches
# the radio through woz_uwb_arm_rx / _set_sts_iv / _set_callbacks / _configure_phy,
# so there is no link-time interposition to declare here. ccc_shim_rx.c and
# ccc_shim_wrap.c supply the first two; port/woz_seam_stubs.c supplies the last
# two, standing in for the uwb_rxdiag.c this port omits (a diagnostic, not a
# dependency: it is portable since T2a, and nothing on this port reads it).
