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

# unit_srcs_from_role — append one role manifest (modules/<mod>/roles/<n>.list)
# to UNIT_SRCS. Same lists cmake/woz_roles.cmake reads, so the host suite and
# the apps/dwm3001cdk-lock/ESP builds cannot disagree about which files a role contains.
# Redirected (not piped) so the appends land in this shell under bash 3.2.
unit_srcs_from_role() {
	local _line
	while IFS= read -r _line || [ -n "$_line" ]; do
		_line="${_line%%#*}"
		_line="${_line#"${_line%%[![:space:]]*}"}"
		_line="${_line%"${_line##*[![:space:]]}"}"
		[ -n "$_line" ] || continue
		UNIT_SRCS+=("$ROOT/$_line")
	done < "$1"
}

UNIT_SRCS=(
	"$ROOT/modules/woz_aliro_stack/src/advertising_core.c"
	"$ROOT/modules/woz_aliro_stack/src/protocol/ble_message.c"
	"$ROOT/modules/woz_aliro_stack/src/protocol/ble_timeout.c"
	"$ROOT/modules/woz_aliro_stack/src/protocol/nfc_select.c"
	"$ROOT/modules/woz_aliro_stack/src/protocol/nfc_auth.c"
	"$ROOT/modules/woz_nfc/src/pn532.c"
	"$ROOT/modules/woz_nfc/src/pn532_apdu.c"
	"$ROOT/ports/zephyr/log/woz_logfmt.c"
	"$ROOT/ports/zephyr/log/woz_logquiet.c"
	"$ROOT/modules/ultrawidelock_matter/src/matter_tlv.c"
	"$ROOT/modules/ultrawidelock_matter/src/matter_msg.c"
	"$ROOT/modules/ultrawidelock_matter/src/matter_mrp.c"
	"$ROOT/modules/ultrawidelock_matter/src/matter_crypto.c"
	"$ROOT/modules/ultrawidelock_matter/src/matter_btp.c"
	"$ROOT/modules/ultrawidelock_matter/src/matter_pase.c"
	"$ROOT/modules/ultrawidelock_matter/src/matter_spake2p.c"
	"$ROOT/modules/ultrawidelock_matter/src/matter_pase_sm.c"
	"$ROOT/modules/ultrawidelock_matter/src/matter_exchange.c"
	"$ROOT/modules/ultrawidelock_matter/src/matter_im.c"
	"$ROOT/modules/ultrawidelock_matter/src/matter_clusters.c"
	"$ROOT/modules/ultrawidelock_matter/src/matter_attest.c"
	"$ROOT/modules/ultrawidelock_matter/src/matter_fabric.c"
	"$ROOT/modules/ultrawidelock_matter/src/matter_case.c"
	"$ROOT/modules/ultrawidelock_ml/src/ultrawidelock_ml_los.c"
	"$ROOT/modules/ultrawidelock_ml/src/ultrawidelock_ml_lin.c"
	"$ROOT/modules/ultrawidelock_ml/src/ultrawidelock_ml_feat.c"
	"$ROOT/modules/ultrawidelock_ml/src/ultrawidelock_ml_range.c"
	"$ROOT/modules/ultrawidelock_ml/src/ultrawidelock_ml_log2.c"
	"$ROOT/modules/ultrawidelock_anchor/src/ultrawidelock_door.c"
	"$ROOT/modules/ultrawidelock_anchor/src/ultrawidelock_fusion.c"
	"$ROOT/modules/ultrawidelock_anchor/src/ultrawidelock_report.c"
	"$ROOT/modules/ultrawidelock_anchor/src/ultrawidelock_satellite.c"
	"$ROOT/modules/ultrawidelock_anchor/src/ultrawidelock_side.c"
	"$ROOT/modules/ultrawidelock_anchor/src/ultrawidelock_side_log.c"
	"$ROOT/modules/ultrawidelock_anchor/src/ultrawidelock_slam.c"
)

# woz_aliro roles. wire_codecs = the shared step-up/assert codecs (one source;
# woz_aliro_stack compiles the same files on target) — crypto-free, so no
# aliro_crypto/prim backend is needed here. hash + reader_policy are the
# host-tested halves of the reader.
unit_srcs_from_role "$ALIRO/roles/wire_codecs.list"
unit_srcs_from_role "$ALIRO/roles/hash.list"
unit_srcs_from_role "$ALIRO/roles/reader_policy.list"

# woz_uwb roles. Everything radio-free: the CCC key schedule and STS engine, the
# M1-M4 codec on BOTH ends (aliro_device = initiator side, tested against the
# real reader codec), the ranging estimator and responder state machine, and the
# flight recorder's replay half. base_driver/responder_driver need a DW3000 and
# are absent by design.
unit_srcs_from_role "$ROOT/modules/woz_uwb/roles/ccc_keys.list"
unit_srcs_from_role "$ROOT/modules/woz_uwb/roles/ccc_engine.list"
unit_srcs_from_role "$ROOT/modules/woz_uwb/roles/aliro_adapter.list"
unit_srcs_from_role "$ROOT/modules/woz_uwb/roles/aliro_codec.list"
unit_srcs_from_role "$ROOT/modules/woz_uwb/roles/aliro_device.list"
unit_srcs_from_role "$ROOT/modules/woz_uwb/roles/base_engine.list"
unit_srcs_from_role "$ROOT/modules/woz_uwb/roles/responder_engine.list"
unit_srcs_from_role "$ROOT/modules/woz_uwb/roles/flight_recorder.list"

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
	"$HOST/test_rssi_gate.c"
	"$HOST/test_approach.c"
	"$HOST/test_ultrawidelock_door.c"
	"$HOST/test_ultrawidelock_fusion.c"
	"$HOST/test_ultrawidelock_report.c"
	"$HOST/test_ultrawidelock_satellite.c"
	"$HOST/test_ultrawidelock_side.c"
	"$HOST/test_ultrawidelock_side_replay.c"
	"$HOST/test_ultrawidelock_slam.c"
	"$HOST/test_woz_logfmt.c"
	"$HOST/test_trace.c"
	"$HOST/trace_stub.c"
	"$HOST/test_ccc_shim_wrap.c"
	"$HOST/test_flight_recorder.c"
	"$HOST/fr_replay.c"
	"$HOST/test_ultrawidelock_ml.c"
	"$HOST/test_woz_port.c"
)

SHIM_SRCS=(
	"$SHIM/shim.c"
	"$SHIM/dw_rx_stub.c"
	"$HOST/logfake/logfake.c"
	"$HOST/spakefake/spakefake.c"
	# The host OSAL/flash backends double as the test fakes (woz_osal.h).
	"$ROOT/tests/host/port/osal_host.c"
	"$ROOT/tests/host/port/flash_host.c"
)

# Include search path: shim first so <zephyr/...> resolves to the stubs;
# logfake supplies the Zephyr logging + CMSIS surface woz_logfmt.c needs.
INCS=(
	-I"$SHIM"
	-I"$HOST"
	-I"$HOST/logfake"
	-I"$ROOT/modules/woz_aliro_stack/src"
	-I"$ROOT/modules/woz_aliro_stack/src/protocol"
	-I"$ROOT/modules/woz_nfc/include"
	-I"$ROOT/modules/woz_nfc/src"
	-I"$ROOT/modules/woz_uwb/include"
	-I"$SRC/ccc"
	-I"$SRC/driver"
	-I"$SRC/aliro"
	-I"$SRC/aliro/include"
	-I"$SRC/fira"
	-I"$SRC/facade"
	-I"$ROOT/ports/zephyr/shell"
	-I"$ALIRO/include"
	-I"$ROOT/modules/woz_port/include"
	-I"$ROOT/modules/woz_aliro/include"
	-I"$ROOT/modules/woz_aliro/src"
	-I"$ROOT/modules/ultrawidelock_matter/include"
	-I"$ROOT/modules/ultrawidelock_ml/include"
	-I"$ROOT/modules/ultrawidelock_ml/src"
	-I"$ROOT/modules/ultrawidelock_anchor/include"
	-I"$ROOT/modules/ultrawidelock_dw3000/include"
)

# The Aliro path is Kconfig-gated in-tree; the normal build has it on.
# _DEFAULT_SOURCE: glibc hides clock_gettime/CLOCK_MONOTONIC under strict
# -std=c11 without it (feature_test_macros(7)); Darwin headers ignore it.
# WOZ_PORT_HOST selects the libc backend in woz_port.h / woz_log.h; without it
# those headers #error rather than guess a platform.
DEFS=(-DCONFIG_WOZ_ALIRO=1 -DCONFIG_ULTRAWIDELOCK_ML_LOS=1 -DCONFIG_WOZ_FLIGHT_RECORDER=1 -D_DEFAULT_SOURCE -DWOZ_PORT_HOST)

# PY — the interpreter the python-side suites run under: the repo-local .venv
# when one exists, else the system python3.
PY="$ROOT/.venv/bin/python3"
[ -x "$PY" ] || PY=python3
