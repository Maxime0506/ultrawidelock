# Woz UWB engine as a Zephyr module for the Nordic door-lock add-on.
# The CCC STS substitution rides src/driver/uwb_seam.h: every caller reaches the
# radio through woz_uwb_arm_rx(), which programs the CCC key/IV on every RX-arm,
# and its three siblings. scripts/check-uwb-seam.sh keeps the seam intact.
# The DS-TWR responder runs on deps/dw3000.
# Contributes nothing unless CONFIG_WOZ_UWB=y (stock build byte-identical).

if(CONFIG_WOZ_UWB)

  set(SRC ${CMAKE_CURRENT_SOURCE_DIR}/src)

  zephyr_library_named(woz_uwb)
  # ds_twr.c sits at the BASE tier, not under the responder, because the
  # anchor-to-anchor app (anchor/) ranges another board with no CCC engine in
  # its image and needs the same estimator the Aliro responder uses.
  zephyr_library_sources(
    ${SRC}/driver/uwb_min.c
    ${SRC}/fira/ds_twr.c)

  if(CONFIG_WOZ_UWB_RESPONDER)
    # The responder runs entirely on deps/dw3000 in ccc_shim_rx.c
    # (ccc_prepoll_listen). The codec uses only qmalloc/qcalloc/qfree + qrtc_get_us,
    # provided as thin Zephyr wrappers by src/facade/woz_alloc.h.
    zephyr_library_sources(
      ${SRC}/driver/uwb_isr.c
      ${SRC}/fira/fira_session.c
    )

    if(CONFIG_WOZ_ALIRO)
      # The add-on hands us the plaintext URSK in _ConfigureRangingSession, so we
      # only need the CCC STS engine.
      zephyr_library_sources(
        ${SRC}/ccc/ccc_kdf.c ${SRC}/ccc/ccc_mac.c
        ${SRC}/ccc/ccc_sts.c ${SRC}/ccc/ccc_session.c
        ${SRC}/ccc/ccc_shim.c ${SRC}/ccc/ccc_shim_wrap.c
        ${SRC}/facade/woz_uwb_facade.c
        # Responder-RX CCC-STS substitution: program CCC key/IV per RX-arm.
        ${SRC}/ccc/ccc_shim_rx.c
        # Diagnostic (removable): tallies on-air RX events.
        ${SRC}/driver/uwb_rxdiag.c)

      # AES-ECB crypto seam (ccc_kdf.h): PSA on nRF (default), mbedTLS for SoCs
      # without a PSA provider (e.g. ESP32-S3). See docs/porting.md.
      if(CONFIG_WOZ_CRYPTO_MBEDTLS)
        zephyr_library_sources(${SRC}/ccc/ccc_crypto_mbedtls.c)
        # Put mbedTLS headers (<mbedtls/aes.h>) on this library's include path.
        # The PSA branch gets its headers from nrf_security; the mbedTLS branch
        # must pull them from the mbedTLS interface target explicitly.
        zephyr_library_link_libraries(mbedTLS)
      else()
        zephyr_library_sources(${SRC}/ccc/ccc_crypto_psa.c)
      endif()

      # PRETTY (reversible): DWT-cycle-counter timestamps + compact colored log
      # line. Owns the whole log line via LOG_CUSTOM_FORMAT_SUPPORT (set in
      # ports/nrf5340dk/overlays/woz-pretty.conf, active only under PRETTY=1).
      if(CONFIG_WOZ_PRETTY_SHELL)
        zephyr_library_sources(${SRC}/facade/woz_logfmt.c)
        # Runtime-mute benign upstream Matter/BLE error spam (reversible).
        zephyr_library_sources(${SRC}/facade/woz_logquiet.c)
      endif()

      # `aliro` UART console (status/rx/range/chip/selftest/log/version). Needs
      # the shell subsystem, which an NCS/Matter dependency force-selects.
      if(CONFIG_WOZ_UWB_SHELL)
        zephyr_library_sources(${SRC}/shell/aliro_shell.c)
        # Short commit SHA of THIS repo, baked in for `aliro version`. Captured
        # at CMake configure time (git -C works from a worktree too); refreshed
        # on a pristine/reconfigure build, so a stale-but-clean incremental keeps
        # the prior value. "unknown" if git is unavailable (e.g. a source export).
        execute_process(
          COMMAND git -C ${CMAKE_CURRENT_SOURCE_DIR} rev-parse --short HEAD
          OUTPUT_VARIABLE WOZ_GIT_SHA
          OUTPUT_STRIP_TRAILING_WHITESPACE
          ERROR_QUIET)
        if(NOT WOZ_GIT_SHA)
          set(WOZ_GIT_SHA "unknown")
        endif()
        set_source_files_properties(${SRC}/shell/aliro_shell.c PROPERTIES
          COMPILE_DEFINITIONS "WOZ_GIT_SHA=\"${WOZ_GIT_SHA}\"")
      endif()

      # Aliro UWB ranging-setup adapter: composes the M1-M4 handshake the add-on's
      # aliro_stack drives via the facade, over the cherry_ccc_* seam that
      # cherry_ccc_shim.c implements on our MAC (fira_session.c + ccc_shim).
      set(_aliro ${SRC}/aliro)
      zephyr_library_sources(
        ${_aliro}/aliro_uwb_adapter.c
        ${_aliro}/aliro_uwb_msg.c
        ${_aliro}/aliro_uwb_msg_builder.c
        ${_aliro}/aliro_uwb_msg_parser.c
        ${_aliro}/aliro_uwb_session.c
        ${SRC}/ccc/cherry_ccc_shim.c)
      zephyr_include_directories(
        ${_aliro}/include
        ${_aliro})
      # Diagnostic: one-shot boot self-test of the UWB init path (gated, default n).
      if(CONFIG_WOZ_UWB_SELFTEST)
        zephyr_library_sources(${SRC}/driver/uwb_selftest.c)
      endif()

      # CIA/CIR diagnostics: per-reception first-path summary + windowed CIR tap dump
      # (gated, default n; the RX-shim call sites no-op when unset).
      if(CONFIG_WOZ_UWB_CIRDIAG)
        zephyr_library_sources(${SRC}/driver/uwb_cirdiag.c)
      endif()
      # Flight recorder: record a live walk-up into a RAM ring for host replay
      # (gated, default n; the ccc_shim_rx.c call sites no-op when unset).
      if(CONFIG_WOZ_FLIGHT_RECORDER)
        zephyr_library_sources(${SRC}/facade/flight_recorder.c)
      endif()
    endif()

    # woz_alloc.h (in ${SRC}/facade, on the path below) covers the qmalloc/qrtc
    # surface the codec uses. deca_device_api.h comes from the dw3000 module.
  endif()

  # All layer subfolders on the include path so the flat #include "sibling.h"
  # style resolves across layers (headers moved into src/{driver,fira,ccc,facade,shell}).
  # The platform contract (woz_port.h / woz_log.h) lives in modules/woz_port.
  zephyr_include_directories(
    ${CMAKE_CURRENT_SOURCE_DIR}/../woz_port/include
    ${SRC}/driver ${SRC}/fira ${SRC}/ccc ${SRC}/facade ${SRC}/shell)

endif()
