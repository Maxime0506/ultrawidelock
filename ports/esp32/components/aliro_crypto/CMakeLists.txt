# Aliro key schedule + crypto primitives as an ESP-IDF component.
# The sources come from the role manifests in modules/woz_aliro/roles/ (the
# same lists the nRF builds and the host suite read); nothing here is
# ESP32-specific except the mbedTLS-PSA provider that backs aliro_prim_psa.c.

get_filename_component(REPO_ROOT "${CMAKE_CURRENT_LIST_DIR}/../../../.." ABSOLUTE)
set(ALIRO "${REPO_ROOT}/modules/woz_aliro")

include("${REPO_ROOT}/cmake/woz_roles.cmake")
woz_role_sources("${ALIRO}/roles/hash.list" ALIRO_HASH_SRCS)
woz_role_sources("${ALIRO}/roles/crypto.list" ALIRO_CRYPTO_SRCS)
woz_role_sources("${ALIRO}/roles/dyntag.list" ALIRO_DYNTAG_SRCS)

idf_component_register(
	SRCS
		${ALIRO_HASH_SRCS}
		${ALIRO_CRYPTO_SRCS}
		${ALIRO_DYNTAG_SRCS}
	INCLUDE_DIRS "${ALIRO}/include"
	PRIV_INCLUDE_DIRS "${ALIRO}/src"
	REQUIRES mbedtls
)
