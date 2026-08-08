# shellcheck shell=bash
# shellcheck disable=SC2034  # every list here is consumed by the sourcing scripts
# Shared source lists for the host test + coverage builds. Sourced by run.sh and
# coverage.sh; the caller must set $ROOT to the repo root.
#
# UNIT_SRCS  — our code under test (the coverage denominator).
# TEST_SRCS  — the harness + per-module suites + the host AES double.
# SHIM_SRCS  — non-inline shim definitions (STS register no-ops, RX stubs).
# See coverage.sh for what is deliberately excluded and why.

SRC="$ROOT/modules/woz_uwb/src"
ALIRO="$ROOT/modules/woz_aliro"
SHIM="$ROOT/tests/host/shim"
HOST="$ROOT/tests/host"

UNIT_SRCS=(
	"$ROOT/modules/woz_aliro_stack/src/advertising_core.c"
	"$ROOT/modules/woz_aliro_stack/src/protocol/ble_message.c"
	"$ROOT/modules/woz_aliro_stack/src/protocol/ble_timeout.c"
	"$ROOT/modules/woz_aliro_stack/src/protocol/tlv.c"
	"$ROOT/modules/woz_aliro_stack/src/protocol/nfc_select.c"
	"$ROOT/modules/woz_aliro_stack/src/protocol/nfc_auth.c"
	"$ROOT/modules/woz_aliro_stack/src/protocol/nfc_step_up.c"
	"$ROOT/modules/woz_aliro_stack/src/protocol/access_document.c"
	"$ROOT/modules/woz_nfc/src/pn532.c"
	"$ROOT/modules/woz_nfc/src/pn532_apdu.c"
	"$SRC/ccc/ccc_kdf.c"
	"$SRC/ccc/ccc_mac.c"
	"$SRC/ccc/ccc_session.c"
	"$SRC/ccc/ccc_shim.c"
	"$SRC/ccc/ccc_sts.c"
	"$SRC/aliro/aliro_uwb_msg_builder.c"
	"$SRC/aliro/aliro_uwb_msg_parser.c"
	"$SRC/aliro/aliro_uwb_adapter.c"
	"$SRC/aliro/aliro_uwb_msg.c"
	"$SRC/aliro/aliro_device_uwb.c"
	"$SRC/aliro/aliro_uwb_session.c"
	"$SRC/ccc/cherry_ccc_shim.c"
	"$SRC/ccc/ccc_shim_rx.c"
	"$SRC/ccc/ccc_shim_wrap.c"
	"$SRC/fira/ds_twr.c"
	"$SRC/fira/fira_session.c"
	"$SRC/facade/woz_uwb_facade.c"
	"$ROOT/modules/woz_aliro/src/aliro_rssi_gate.c"
	"$SRC/facade/woz_logfmt.c"
	"$SRC/facade/woz_logquiet.c"
	"$SRC/facade/flight_recorder.c"
	"$ALIRO/src/aliro_prov.c"
	"$ALIRO/src/aliro_hash.c"
	"$ROOT/modules/woz_matter/src/matter_tlv.c"
	"$ROOT/modules/woz_matter/src/matter_msg.c"
	"$ROOT/modules/woz_matter/src/matter_mrp.c"
	"$ROOT/modules/woz_matter/src/matter_crypto.c"
	"$ROOT/modules/woz_matter/src/matter_btp.c"
	"$ROOT/modules/woz_matter/src/matter_pase.c"
	"$ROOT/modules/woz_matter/src/matter_spake2p.c"
	"$ROOT/modules/woz_matter/src/matter_pase_sm.c"
	"$ROOT/modules/woz_matter/src/matter_exchange.c"
	"$ROOT/modules/woz_matter/src/matter_im.c"
	"$ROOT/modules/woz_matter/src/matter_clusters.c"
	"$ROOT/modules/woz_matter/src/matter_attest.c"
	"$ROOT/modules/woz_matter/src/matter_fabric.c"
	"$ROOT/modules/woz_matter/src/matter_case.c"
	"$ALIRO/src/aliro_assert.c"
	"$ALIRO/src/aliro_approach.c"
	"$ROOT/modules/woz_anchor/src/woz_door.c"
	"$ROOT/modules/woz_anchor/src/woz_fusion.c"
	"$ROOT/modules/woz_anchor/src/woz_report.c"
	"$ROOT/modules/woz_anchor/src/woz_satellite.c"
	"$ROOT/modules/woz_anchor/src/woz_slam.c"
)

TEST_SRCS=(
	"$HOST/aes_ref.c"
	"$HOST/test.c"
	"$HOST/test_main.c"
	"$HOST/test_matter_tlv.c"
	"$HOST/test_matter_msg.c"
	"$HOST/test_matter_mrp.c"
	"$HOST/test_matter_crypto.c"
	"$HOST/test_matter_btp.c"
	"$HOST/test_matter_pase.c"
	"$HOST/test_matter_spake2p.c"
	"$HOST/test_matter_pase_sm.c"
	"$HOST/test_matter_exchange.c"
	"$HOST/test_matter_im.c"
	"$HOST/test_matter_attest.c"
	"$HOST/test_matter_attest_stub.c"
	"$HOST/test_matter_fabric.c"
	"$HOST/test_matter_thread_stub.c"
	"$HOST/test_matter_case.c"
	"$HOST/test_matter_case_stub.c"
	"$HOST/test_matter_clusters.c"
	"$HOST/test_aliro_advertising.c"
	"$HOST/test_aliro_ble.c"
	"$HOST/test_aliro_nfc.c"
	"$HOST/test_pn532.c"
	"$HOST/test_ccc_kdf.c"
	"$HOST/test_ccc_mac.c"
	"$HOST/test_ccc_sts.c"
	"$HOST/test_ccc_shim.c"
	"$HOST/test_ccc_session.c"
	"$HOST/test_prepoll_schedule.c"
	"$HOST/test_aliro_builder.c"
	"$HOST/test_aliro_parser.c"
	"$HOST/test_aliro_adapter.c"
	"$HOST/test_aliro_msg.c"
	"$HOST/test_aliro_session.c"
	"$HOST/test_aliro_prov.c"
	"$HOST/test_aliro_hash.c"
	"$HOST/test_aliro_assert.c"
	"$HOST/test_aliro_device_uwb.c"
	"$HOST/test_cherry.c"
	"$HOST/test_fira.c"
	"$HOST/test_facade.c"
	"$HOST/test_prepoll_gate.c"
	"$HOST/test_prepoll_round.c"
	"$HOST/twin_frames.c"
	"$HOST/test_twin.c"
	"$HOST/test_rssi_gate.c"
	"$HOST/test_approach.c"
	"$HOST/test_woz_door.c"
	"$HOST/test_woz_fusion.c"
	"$HOST/test_woz_report.c"
	"$HOST/test_woz_satellite.c"
	"$HOST/test_woz_slam.c"
	"$HOST/test_woz_logfmt.c"
	"$HOST/test_trace.c"
	"$HOST/trace_stub.c"
	"$HOST/test_ccc_shim_wrap.c"
	"$HOST/test_flight_recorder.c"
	"$HOST/fr_replay.c"
)

SHIM_SRCS=(
	"$SHIM/shim.c"
	"$SHIM/dw_rx_stub.c"
	"$HOST/logfake/logfake.c"
	"$HOST/spakefake/spakefake.c"
)

# Include search path: shim first so <zephyr/...> resolves to the stubs;
# logfake supplies the Zephyr logging + CMSIS surface woz_logfmt.c needs.
INCS=(
	-I"$SHIM"
	-I"$HOST"
	-I"$HOST/logfake"
	-I"$ROOT/modules/woz_aliro_stack/src"
	-I"$ROOT/modules/woz_aliro_stack/src/protocol"
	-I"$ROOT/modules/woz_nfc/src"
	-I"$SRC/ccc"
	-I"$SRC/driver"
	-I"$SRC/aliro"
	-I"$SRC/aliro/include"
	-I"$SRC/fira"
	-I"$SRC/facade"
	-I"$SRC/shell"
	-I"$ALIRO/include"
	-I"$ROOT/modules/woz_port/include"
	-I"$ROOT/modules/woz_aliro/include"
	-I"$ROOT/modules/woz_aliro/src"
	-I"$ROOT/modules/woz_matter/include"
	-I"$ROOT/modules/woz_anchor/include"
)

# The Aliro path is Kconfig-gated in-tree; the normal build has it on.
# _DEFAULT_SOURCE: glibc hides clock_gettime/CLOCK_MONOTONIC under strict
# -std=c11 without it (feature_test_macros(7)); Darwin headers ignore it.
# WOZ_PORT_HOST selects the libc backend in woz_port.h / woz_log.h; without it
# those headers #error rather than guess a platform.
DEFS=(-DCONFIG_WOZ_ALIRO=1 -DCONFIG_WOZ_FLIGHT_RECORDER=1 -D_DEFAULT_SOURCE -DWOZ_PORT_HOST)

# PY — the interpreter the python-side suites run under.
#
# `markdown` and `coverage` are imported by the suites, so they have to live in
# the interpreter that runs them; a pipx venv is invisible to an import. Where
# the system python is externally-managed (PEP 668) pip will not install them
# there at all, so scripts/toolchain.sh puts them in a repo-local .venv and
# every runner finds it here. Nothing is added to PATH and no shell needs
# activating: the venv is either present in the checkout or it is not.
PY="$ROOT/.venv/bin/python3"
[ -x "$PY" ] || PY=python3
