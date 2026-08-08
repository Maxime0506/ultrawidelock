# Woz UWB (Aliro DS-TWR responder) engine as an ESP-IDF component.
# Additive: reuses modules/woz_uwb/src + modules/woz_dw3000 from the repo unchanged.
# The engine is platform-neutral: it takes its OS surface from woz_port.h and
# its logging from woz_log.h (modules/woz_port/include), which select the ESP-IDF
# backend on ESP_PLATFORM. No Zephyr compat layer is involved. The DW3000
# platform SPI/GPIO layer is the ESP-IDF backend in port/.
# Source set mirrors modules/woz_uwb/CMakeLists.txt + modules/woz_dw3000/CMakeLists.txt
# for CONFIG_WOZ_UWB + RESPONDER + ALIRO + CRYPTO_MBEDTLS (no PRETTY/SHELL/SELFTEST).

get_filename_component(REPO_ROOT "${CMAKE_CURRENT_LIST_DIR}/../../../.." ABSOLUTE)
set(WOZ "${REPO_ROOT}/modules/woz_uwb/src")
set(WOZ_PORT "${REPO_ROOT}/modules/woz_port/include")
set(DW  "${REPO_ROOT}/modules/woz_dw3000")

set(WOZ_SRCS
    # engine: driver + fira
    "${WOZ}/driver/uwb_min.c"
    "${WOZ}/driver/uwb_isr.c"
    "${WOZ}/fira/fira_session.c"
    "${WOZ}/fira/ds_twr.c" # shared ToF estimator; ccc_shim_rx.c calls it (a3ab4ed4)
    # engine: CCC STS
    "${WOZ}/ccc/ccc_kdf.c"
    "${WOZ}/ccc/ccc_mac.c"
    "${WOZ}/ccc/ccc_sts.c"
    "${WOZ}/ccc/ccc_session.c"
    "${WOZ}/ccc/ccc_shim.c"
    "${WOZ}/ccc/ccc_shim_wrap.c"
    "${WOZ}/ccc/ccc_shim_rx.c"
    "${WOZ}/ccc/ccc_crypto_mbedtls.c"
    "${WOZ}/facade/woz_uwb_facade.c"
    # engine: Aliro ranging-setup adapter
    "${WOZ}/aliro/aliro_uwb_adapter.c"
    "${WOZ}/aliro/aliro_uwb_msg.c"
    "${WOZ}/aliro/aliro_uwb_msg_builder.c"
    "${WOZ}/aliro/aliro_uwb_msg_parser.c"
    "${WOZ}/aliro/aliro_uwb_session.c"
    "${WOZ}/ccc/cherry_ccc_shim.c"
    # vendor decadriver (modules/woz_dw3000/CMakeLists.txt canonical set)
    "${DW}/platform/deca_port.c"
    "${DW}/platform/deca_compat.c"
    "${DW}/dwt_uwb_driver/deca_interface.c"
    "${DW}/dwt_uwb_driver/deca_rsl.c"
    "${DW}/dwt_uwb_driver/lib/qmath/src/qmath.c"
    "${DW}/dwt_uwb_driver/dw3000/dw3000_device.c"
    # ESP-IDF platform backend (replaces Zephyr dw3000_spi.c / dw3000_hw.c)
    "port/dw3000_spi.c"
    "port/dw3000_hw.c"
    "port/woz_seam_stubs.c"
)

# CIA/CIR diagnostics: per-reception first-path summary + windowed CIR tap dump
# (gated, default n; the RX-shim call sites no-op when unset).
if(CONFIG_WOZ_UWB_CIRDIAG)
    list(APPEND WOZ_SRCS "${WOZ}/driver/uwb_cirdiag.c")
endif()

# Flight recorder: record a live walk-up into a RAM ring for host replay
# (gated, default n; the ccc_shim_rx.c call sites no-op when unset).
if(CONFIG_WOZ_FLIGHT_RECORDER)
    list(APPEND WOZ_SRCS "${WOZ}/facade/flight_recorder.c")
endif()

idf_component_register(
    SRCS ${WOZ_SRCS}
    INCLUDE_DIRS
        "${WOZ_PORT}"          # woz_port.h / woz_log.h — the platform contract
        "${WOZ}/facade"        # woz_uwb_facade.h — public API for the app
        "${WOZ}/aliro/include" # aliro_uwb_adapter/ + cherry/ — reader ranging-setup API
    PRIV_INCLUDE_DIRS
        "port"                 # board_pins.h
        "${WOZ}/driver" "${WOZ}/fira" "${WOZ}/ccc" "${WOZ}/facade" "${WOZ}/shell"
        "${WOZ}/aliro" "${WOZ}/aliro/include"
        "${DW}/platform"
        "${DW}/dwt_uwb_driver"
        "${DW}/dwt_uwb_driver/lib/qmath/include"
    REQUIRES
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

# The CCC STS seam is modules/woz_uwb/src/driver/uwb_seam.h: every caller reaches
# the radio through woz_uwb_arm_rx / _set_sts_iv / _set_callbacks / _configure_phy,
# so there is no link-time interposition to declare here. ccc_shim_rx.c and
# ccc_shim_wrap.c supply the first two; port/woz_seam_stubs.c supplies the last
# two, standing in for the uwb_rxdiag.c this port omits (it needs Zephyr k_work).
