# credential device/initiator session layer as an ESP-IDF component — the mirror of
# the ultrawidelock_reader component. Sources come from the role manifests in
# modules/ultrawidelock_cred/roles/ (the same lists the Zephyr initiator and on-target
# test read); nothing here is ESP32-specific.
#
# The apdu role is ALSO compiled by the ultrawidelock_reader component, so an app must
# not REQUIRE both this and ultrawidelock_reader — it would link the symbol twice.
# Nothing needs to today: a board is either the reader or the initiator, not both.
#
# The initiator app uses the public ultrawidelock_apdu.h for the 4-byte L2CAP envelope,
# so it reuses the reader's framing rather than hand-rolling a second layout.

get_filename_component(REPO_ROOT "${CMAKE_CURRENT_LIST_DIR}/../../../.." ABSOLUTE)
set(CRED "${REPO_ROOT}/modules/ultrawidelock_cred")

include("${REPO_ROOT}/cmake/ultrawidelock_roles.cmake")
ultrawidelock_role_sources("${CRED}/roles/device.list" ULTRAWIDELOCK_DEVICE_SRCS)
ultrawidelock_role_sources("${CRED}/roles/apdu.list" ULTRAWIDELOCK_APDU_SRCS)

idf_component_register(
	SRCS
		${ULTRAWIDELOCK_DEVICE_SRCS}
		${ULTRAWIDELOCK_APDU_SRCS}
	INCLUDE_DIRS "${CRED}/include"
	PRIV_INCLUDE_DIRS "${CRED}/src"
	REQUIRES ultrawidelock_crypto
)
