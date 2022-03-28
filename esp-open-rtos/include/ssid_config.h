//
// Why this file?
//
// We all need to add our personal SSID/password to each ESP project but we
// do not want that information pushed to Github. This file solves that
// problem. Create an include/private_ssid_config.h file with the following two
// definitions uncommented:
//
 #define WIFI_SSID "quangoc"
 #define WIFI_PASS "conmeo123"
//

#ifndef __SSID_CONFIG_H__
#define __SSID_CONFIG_H__

#include "private_ssid_config.h"

#endif // __SSID_CONFIG_H__
