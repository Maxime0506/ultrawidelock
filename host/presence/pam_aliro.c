// pam_aliro — PAM module: authenticate iff the provisioned iPhone is within the
// configured distance of the USB presence dongle. A thin shim over
// presence_check() (aliro_presence.c); all logic + tests live there. Use as a
// SECOND factor (see README), never as sole authentication.
/*
 * Copyright (c) 2026 asxeem
 * SPDX-License-Identifier: ISC
 */
#include <string.h>
#include <syslog.h>

#include <security/pam_modules.h>

#include "aliro_presence.h"

#ifndef PAM_EXTERN
#define PAM_EXTERN
#endif

#define DEFAULT_CONFIG "/etc/aliro-presence/config"

// PAM auth: run one presence check and map the verdict to a PAM result. Args:
//   config=<path>  override the config file (default /etc/aliro-presence/config)
PAM_EXTERN int pam_sm_authenticate(pam_handle_t *pamh, int flags, int argc, const char **argv)
{
	(void)pamh;
	(void)flags;
	const char *cfg_path = DEFAULT_CONFIG;

	for (int i = 0; i < argc; i++) {
		if (strncmp(argv[i], "config=", 7) == 0) {
			cfg_path = argv[i] + 7;
		}
	}

	struct presence_config c;
	presence_config_defaults(&c);
	/* A missing config file is fine (defaults are usable); a present-but-broken
	 * one is logged but we still try the check. */
	if (presence_config_load(&c, cfg_path) != 0) {
		syslog(LOG_AUTHPRIV | LOG_WARNING,
		       "pam_aliro: config %s missing or has a bad line; using defaults", cfg_path);
	}

	int r = presence_check(&c);

	switch (r) {
	case PRESENCE_PRESENT:
		return PAM_SUCCESS;
	case PRESENCE_DENIED:
		syslog(LOG_AUTHPRIV | LOG_NOTICE, "pam_aliro: denied (no provisioned device in range)");
		return PAM_AUTH_ERR;
	case PRESENCE_E_CRED:
		syslog(LOG_AUTHPRIV | LOG_NOTICE, "pam_aliro: in-range device is not the bound credential");
		return PAM_AUTH_ERR;
	default:
		syslog(LOG_AUTHPRIV | LOG_WARNING, "pam_aliro: dongle/config error (rc=%d)", r);
		return PAM_AUTHINFO_UNAVAIL; /* let a following factor decide */
	}
}

PAM_EXTERN int pam_sm_setcred(pam_handle_t *pamh, int flags, int argc, const char **argv)
{
	(void)pamh;
	(void)flags;
	(void)argc;
	(void)argv;
	return PAM_SUCCESS;
}
