# woz_roles.cmake — read one role manifest (modules/<mod>/roles/<role>.list)
# into a CMake list of absolute source paths. The manifests are the ONE place a
# shared source is assigned to a role; every build system (Zephyr modules and
# apps, ESP-IDF components, tests/host/sources.sh) reads them instead of
# carrying its own copy. tests/tooling/port_purity_check.sh enforces that every
# manifest path exists and every shared woz_aliro/woz_uwb source is manifested.
#
# Include by absolute path (plain CMake, works before idf_component_register):
#   include("${REPO_ROOT}/cmake/woz_roles.cmake")
#   woz_role_sources("${REPO_ROOT}/modules/woz_aliro/roles/hash.list" HASH_SRCS)

function(woz_role_sources listfile out_var)
	# Manifests live at <repo>/modules/<mod>/roles/<name>.list, so the repo
	# root is always four components above the manifest itself. Deriving it
	# here keeps the function state-free: safe to include() from any scope.
	get_filename_component(_root "${listfile}/../../../.." ABSOLUTE)
	file(STRINGS "${listfile}" _lines)
	set(_srcs)
	foreach(_line IN LISTS _lines)
		string(REGEX REPLACE "#.*$" "" _line "${_line}")
		string(STRIP "${_line}" _line)
		if(NOT _line STREQUAL "")
			list(APPEND _srcs "${_root}/${_line}")
		endif()
	endforeach()
	# Editing a manifest must re-run configure, or the build keeps the old set.
	set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS "${listfile}")
	set(${out_var} "${_srcs}" PARENT_SCOPE)
endfunction()
