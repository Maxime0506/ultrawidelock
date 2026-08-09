/* sdkfake host/util/util.h */
#ifndef SDKFAKE_HOST_UTIL_UTIL_H
#define SDKFAKE_HOST_UTIL_UTIL_H

#include "../ble_hs.h"

int ble_hs_util_ensure_addr(int prefer_random);
extern int fake_hs_util_ensure_addr_rc;

#endif
