/**
 * @file application_processor.c
 * @author Jacob Doll
 * @brief eCTF AP Example Design Implementation
 * @date 2024
 *
 * This source file is part of an example system for MITRE's 2024 Embedded System CTF (eCTF).
 * This code is being provided only for educational purposes for the 2024 MITRE eCTF competition,
 * and may not meet MITRE standards for quality. Use this code at your own risk!
 *
 * @copyright Copyright (c) 2024 The MITRE Corporation
 */

#include "board.h"
#include "i2c.h"
#include "icc.h"
#include "led.h"
#include "mxc_delay.h"
#include "mxc_device.h"
#include "nvic_table.h"

#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "board_link.h"
#include "simple_flash.h"
#include "host_messaging.h"
#ifdef CRYPTO_EXAMPLE
#include "simple_crypto.h"
#endif

#ifdef POST_BOOT
#include "mxc_delay.h"
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#endif

// Includes from containerized build
#include "ectf_params.h"
#include "global_secrets.h"

// MIT: Includes for our custom features
#include "ap_common.h"
#include "nice.h"

/********************************* UTILITIES **********************************/

// Initialize the device
// This must be called on startup to initialize the flash and i2c interfaces
void init() {

    // Enable global interrupts    
    __enable_irq();

    // Setup Flash
    flash_simple_init();

    // Test application has been booted before
    flash_first_boot();

    // MIT: Initialize our custom features
    common_init();
    session_init();
    
    // Initialize board link interface
    board_link_init();
}

/*********************************** MAIN *************************************/

int main() {
    // Initialize board
    init();

    // Handle commands forever
    char * buf = get_uart_buf();
    while (1) {
        recv_input("Enter Command: ", 10);

        // Execute requested command
        if (!strcmp(buf, "list")) {
            scan_components();
        } else if (!strcmp(buf, "boot")) {
            attempt_boot();
        } else if (!strcmp(buf, "replace")) {
            attempt_replace();
        } else if (!strcmp(buf, "attest")) {
            attempt_attest();
        } else if (!strcmp(buf, "custom")) {
            printf("sizeof(mit_opcode_t):  %d\n", sizeof(mit_opcode_t));
            printf("sizeof(mit_ad_t):      %d\n", sizeof(mit_ad_t));
            printf("sizeof(mit_authtag_t): %d\n", sizeof(mit_authtag_t));
            printf("sizeof(mit_message_t): %d\n", sizeof(mit_message_t));
            printf("sizeof(mit_packet_t):  %d\n", sizeof(mit_packet_t));
            printf("sizeof(mit_nonce_t):   %d\n", sizeof(mit_nonce_t));
            printf("sizeof(mit_comp_id_t): %d\n", sizeof(mit_comp_id_t));
            printf("--- sessions info ---\n");
            for (int i = 0; i < get_num_components(); i++) {
                mit_comp_id_t component_id = get_component_id(i);
                mit_session_t * session = get_session_of_component(component_id);
                printf("component_id: 0x%08x\n");
                printf("  session.component_id: 0x%08x\n", session->component_id);
                printf("  session.outgoing_nonce: 0x");
                print_hex(session->outgoing_nonce.rawBytes, sizeof(mit_nonce_t));
                printf("  session.incoming_nonce: 0x");
                print_hex(session->incoming_nonce.rawBytes, sizeof(mit_nonce_t));
            }
            uint8_t * hashedPinPtr = getHashedPinPtr();
            printf("Hashed Pin Ptr: 0x%x\n", hashedPinPtr);
            printf("Hashed Pin: 0x");
            print_hex(hashedPinPtr, 32);
        } else {
            print_error("Unrecognized command '%s'\n", buf);
        }
    }

    // Code never reaches here
    return 0;
}
