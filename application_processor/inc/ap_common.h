/**
 * @file "ap_common.h"
 * @brief Common include-all header for AP files
 */

#ifndef _AP_COMMON_H_
#define _AP_COMMON_H_

#include <stdint.h>

// MITRE-provided headers
#include "board_link.h"
#include "host_messaging.h"
#include "simple_flash.h"
#include "simple_i2c_controller.h"

// Deployment-related headers
#include "ectf_params.h"
#include "global_secrets.h"

// Common AP+Component headers
#include "common_crypto.h"
#include "common_init.h"
#include "common_msg.h"
#include "simple_trng.h"

// AP-related logic
#include "ap_attest.h"
#include "ap_boot.h"
#include "ap_list.h"
#include "ap_postboot.h"
#include "ap_replace.h"
#include "ap_utilities.h"

#include "return_codes.h"

// externs
#define AP_PLAINTEXT_LEN 256
extern uint8_t ap_plaintext[AP_PLAINTEXT_LEN];

// TODO use null_nonce similar to how we define shared_secret_key?
extern uint8_t null_nonce[MIT_NONCE_SIZE];

#endif
