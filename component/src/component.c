/**
 * @file component.c
 * @author Jacob Doll 
 * @brief eCTF Component Example Design Implementation
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
#include "led.h"
#include "mxc_delay.h"
#include "mxc_errors.h"
#include "nvic_table.h"
#include <stdbool.h>
#include <stdio.h>
#include <string.h>

#include "simple_i2c_peripheral.h"
#include "board_link.h"

// Includes from containerized build
#include "ectf_params.h"
#include "global_secrets.h"

// MIT: Includes for our custom features
#include "common_crypto.h"
#include "common_init.h"
#include "common_msg.h"
#include "simple_trng.h"

#ifdef POST_BOOT
#include "led.h"
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#endif

/********************************* CONSTANTS **********************************/

// Passed in through ectf-params.h
// Example of format of ectf-params.h shown here
/*
#define COMPONENT_ID 0x11111124
#define COMPONENT_BOOT_MSG "Component boot"
#define ATTESTATION_LOC "McLean"
#define ATTESTATION_DATE "08/08/08"
#define ATTESTATION_CUSTOMER "Fritz"
*/

/******************************** TYPE DEFINITIONS ********************************/


/********************************* FUNCTION DECLARATIONS **********************************/
// Core function definitions
int component_process_cmd(void);
int process_boot(void);
int process_scan(void);
int process_validate(void);
int process_attest(void);

/********************************* GLOBAL VARIABLES **********************************/
// Global varaibles
uint8_t receive_buffer[MAX_I2C_MESSAGE_LEN];
uint8_t transmit_buffer[MAX_I2C_MESSAGE_LEN];

/******************************* MIT UTILITIES ********************************/

#define COMP_PLAINTEXT_LEN 256

 // TODO gross data allocation ?
uint8_t working_buffer[MIT_MAX_MSG_LEN];
uint8_t comp_plaintext[COMP_PLAINTEXT_LEN];

mit_session_t session;

// TODO replace with CHACHA20_POLY1305_AEAD_IV_SIZE
uint8_t null_nonce[MIT_NONCE_SIZE] = {0};

void session_init(void) {
    // Initialize nonce's to {0}.
    session.component_id = 0;
    memset(session.outgoing_nonce.rawBytes, 0, MIT_NONCE_SIZE);
    memset(session.incoming_nonce.rawBytes, 0, MIT_NONCE_SIZE);

    // Initialize outgoing nonce to some random value
    while (memcmp(session.outgoing_nonce.rawBytes, null_nonce, MIT_NONCE_SIZE) == 0) {
        get_rand_bytes(session.outgoing_nonce.rawBytes, MIT_NONCE_SIZE);
    }
}


void set_ad(mit_packet_t * packet, mit_comp_id_t comp_id, mit_opcode_t opcode, uint8_t len) {
    // TODO limits check on len?
    packet->ad.nonce.sequenceNumber = 0; // TODO
    packet->ad.comp_id = comp_id;
    packet->ad.opcode = opcode;
    packet->ad.len = len;
    packet->ad.for_ap = true; // TODO use ifdefs w/ AP_BOOT_MSG to resolve this in common code?
}

/**
 * @brief Helper for constructing packets
 * 
 * @param component_id: mit_comp_id_t, id of component to make packet for
 * @param opcode: mit_opcode_t, opcode to make packet for
 * @param data: uint8_t *, ptr for data to store in message ield
 * @param len: uint8_t, len of data to copy into message field
 */
int make_mit_packet(mit_comp_id_t component_id, mit_opcode_t opcode, uint8_t * data, uint8_t len) {
    int ret;

    // TODO bounds check on len?
    mit_packet_t * packet = (mit_packet_t *)transmit_buffer;

    // Set Authenticated Data field
    set_ad(packet, component_id, opcode, len);

    /***** NONCE GENERATION/LOOKUP *****/

    // WARNING reusing a nonce is the worst thing you can possibly do.

    // if the nonce is 0, generate a random nonce
    while (memcmp(null_nonce, session.outgoing_nonce.rawBytes, sizeof(mit_nonce_t)) == 0) {
        get_rand_bytes(session.outgoing_nonce.rawBytes, sizeof(mit_nonce_t));
    }

    memcpy(packet->ad.nonce.rawBytes, session.outgoing_nonce.rawBytes, sizeof(mit_nonce_t));

    // TODO do we really need this :)
    if (memcmp(packet->ad.nonce.rawBytes, session.outgoing_nonce.rawBytes, sizeof(mit_nonce_t))) {
        printf("error: Failed to copy nonce!\n");
        return ERROR_RETURN;
    }

    /****************************/

    // Encrypt in data
    ret = mit_encrypt(packet, data, len);
    if (ret != SUCCESS_RETURN) {
        printf("error: encryption failed with error code %i\n", ret);
        memset(packet, 0, sizeof(mit_packet_t));
        return ERROR_RETURN;
    }

    // TODO best place to increase nonce?
    session.outgoing_nonce.sequenceNumber += 1;

    return SUCCESS_RETURN;
}

/******************************* POST BOOT FUNCTIONALITY *********************************/
/**
 * @brief Secure Send 
 * 
 * @param buffer: uint8_t*, pointer to data to be send
 * @param len: uint8_t, size of data to be sent 
 * 
 * Securely send data over I2C. This function is utilized in POST_BOOT functionality.
 * This function must be implemented by your team to align with the security requirements.
*/
void secure_send(uint8_t* buffer, uint8_t len) {
    // TODO gross allocation
    make_mit_packet(COMPONENT_ID, MIT_CMD_NONE, buffer, len);
    send_packet_and_ack((mit_packet_t *)transmit_buffer);
}

/**
 * @brief Secure Receive
 * 
 * @param buffer: uint8_t*, pointer to buffer to receive data to
 * 
 * @return int: number of bytes received, negative if error
 * 
 * Securely receive data over I2C. This function is utilized in POST_BOOT functionality.
 * This function must be implemented by your team to align with the security requirements.
*/
int secure_receive(uint8_t* buffer) {
    int ret;
    mit_packet_t * packet = (mit_packet_t *) receive_buffer;

    // TODO validate len?
    int len = wait_and_receive_packet(packet);

    // TODO validate received packet
    /*************** VALIDATE RECEIVED PACKET ****************/
    if (packet->ad.comp_id != COMPONENT_ID) {
        printf("error: rx packet (0x%08x) doesn't match given component id (0x%08x)\n", packet->ad.comp_id, COMPONENT_ID);
        return ERROR_RETURN;
    }

    // TODO use ifdefs for this section
    if (packet->ad.for_ap != false) {
        printf("error: rx packet not tagged for component\n");
        return ERROR_RETURN;
    }

    if (packet->ad.len == 0) {
        printf("error: rx packet has null message length\n");
        return ERROR_RETURN;
    }

    // not a post-boot cmd
    if (packet->ad.opcode != MIT_CMD_NONE) {
        printf("error: rx packet not post-boot-related, opcode 0x%02x\n", packet->ad.opcode);
        return ERROR_RETURN;
    }

    // We don't allow resetting nonces post-boot.
    if (memcmp(session.incoming_nonce.rawBytes, packet->ad.nonce.rawBytes, sizeof(mit_nonce_t)) == 0) {
        // incoming nonce matches expected nonce
        ret = mit_decrypt(packet, comp_plaintext);

        if (ret != SUCCESS_RETURN) {
            printf("error: decryption failed with error code %i\n", ret);
            memset(comp_plaintext, 0, COMP_PLAINTEXT_LEN);
            return ERROR_RETURN;
        }

    } else {
        printf("error: Incoming nonce (seq 0x%08x) doesn't match expected nonce (seq 0x%08x)\n",
            packet->ad.nonce.sequenceNumber, session.incoming_nonce.sequenceNumber
        );
        return ERROR_RETURN;
    }

    memcpy(buffer, comp_plaintext, len);

    // TODO best place for this?
    // increase incoming nonce
    session.incoming_nonce.sequenceNumber += 1;

    return len;
}

/******************************* FUNCTION DEFINITIONS *********************************/

// Example boot sequence
// Your design does not need to change this
void boot() {

    // POST BOOT FUNCTIONALITY
    // DO NOT REMOVE IN YOUR DESIGN
    #ifdef POST_BOOT
        POST_BOOT
    #else
    // Anything after this macro can be changed by your design
    // but will not be run on provisioned systems
    LED_Off(LED1);
    LED_Off(LED2);
    LED_Off(LED3);
    // LED loop to show that boot occurred
    while (1) {
        LED_On(LED1);
        MXC_Delay(500000);
        LED_On(LED2);
        MXC_Delay(500000);
        LED_On(LED3);
        MXC_Delay(500000);
        LED_Off(LED1);
        MXC_Delay(500000);
        LED_Off(LED2);
        MXC_Delay(500000);
        LED_Off(LED3);
        MXC_Delay(500000);
    }
    #endif
}

void send_ack();

// Handle a transaction from the AP
int component_process_cmd() {
    int ret;
    mit_packet_t * packet = (mit_packet_t *) receive_buffer;

    // Special handling for scan commands in non-established session
    if (packet->ad.comp_id != COMPONENT_ID) {                       // if not an established session
        if ((packet->ad.comp_id & 0xff) == (COMPONENT_ID & 0xff)) { // but addressed to us
            if (packet->ad.opcode == MIT_CMD_SCAN) {                // and is a scan
                if (packet->ad.for_ap == false) {                   // and is not for AP
                    if (packet->ad.len != 0) {                      // and has length
                        if (mit_decrypt(packet, comp_plaintext) == SUCCESS_RETURN) { // and is valid!
                            return process_scan();                  // process scan
                        } else {
                            printf("error: decryption failed for ephemeral scan request\n");
                            memset(comp_plaintext, 0, COMP_PLAINTEXT_LEN);
                            return ERROR_RETURN;
                        }
                    }
                }
            }
        }
    }

    // TODO validate received packet
    /*************** VALIDATE RECEIVED PACKET ****************/
    if (packet->ad.comp_id != COMPONENT_ID) {
        printf("error: rx packet (0x%08x) doesn't match given component id (0x%08x)\n", packet->ad.comp_id, COMPONENT_ID);
        return ERROR_RETURN;
    }

    // TODO use ifdefs for this section
    if (packet->ad.for_ap != false) {
        printf("error: rx packet not tagged for component\n");
        return ERROR_RETURN;
    }

    if (packet->ad.len == 0) {
        printf("error: rx packet has null message length\n");
        return ERROR_RETURN;
    }

    // if we currently have a null nonce, then trust the incoming nonce, as long as it passes authtag check.
    if (memcmp(session.incoming_nonce.rawBytes, null_nonce, sizeof(mit_nonce_t)) == 0) {
        // Validate authTag field
        ret = mit_decrypt(packet, comp_plaintext);

        if (ret != SUCCESS_RETURN) {
            printf("error: decryption failed with error code %i\n", ret);
            memset(comp_plaintext, 0, COMP_PLAINTEXT_LEN);
            return ERROR_RETURN;
        }

        // Save nonce into session
        memcpy(session.incoming_nonce.rawBytes, packet->ad.nonce.rawBytes, sizeof(mit_nonce_t));

    } else if (memcmp(session.incoming_nonce.rawBytes, packet->ad.nonce.rawBytes, sizeof(mit_nonce_t)) == 0) {
        // incoming nonce matches expected nonce
        ret = mit_decrypt(packet, comp_plaintext);

        if (ret != SUCCESS_RETURN) {
            printf("error: decryption failed with error code %i\n", ret);
            memset(comp_plaintext, 0, COMP_PLAINTEXT_LEN);
            return ERROR_RETURN;
        }

    } else {
        printf("error: Incoming nonce (seq 0x%08x) doesn't match expected nonce (seq 0x%08x)\n",
            packet->ad.nonce.sequenceNumber, session.incoming_nonce.sequenceNumber
        );
        return ERROR_RETURN;
    }

    // TODO best place for this?
    // increase incoming nonce
    session.incoming_nonce.sequenceNumber += 1;

    // Output to application processor dependent on command received
    switch (packet->ad.opcode) {
    case MIT_CMD_BOOT:
        return process_boot();
    case MIT_CMD_SCAN:
        return process_scan();
    case MIT_CMD_VALIDATE:
        return process_validate();
    case MIT_CMD_ATTEST:
        return process_attest();
    default:
        printf("Error: Unrecognized command received %d\n", packet->ad.opcode);
        return ERROR_RETURN;
    }
}

int process_boot() {
    // The AP requested a boot. Set `component_boot` for the main loop and
    // respond with the boot message

    // Copy boot message into message & create packet

    // TODO gross data allocation ?
    // TODO +1 needed ?
    uint8_t len = sprintf((char *)working_buffer, "%s", COMPONENT_BOOT_MSG) + 1;
    int ret = make_mit_packet(COMPONENT_ID, MIT_CMD_BOOT, working_buffer, len);

    if (ret != SUCCESS_RETURN) {
        return ret;
    }

    send_packet_and_ack((mit_packet_t *)transmit_buffer);
    // Call the boot function
    boot();

    // We should never reach this.
    return ERROR_RETURN;
}


int process_scan() {
    // The AP requested a scan. Respond with the Component ID

    // TODO gross data allocation
    mit_comp_id_t component_id = COMPONENT_ID;
    int ret = make_mit_packet(COMPONENT_ID, MIT_CMD_SCAN, &component_id, sizeof(mit_comp_id_t));

    if (ret != SUCCESS_RETURN) {
        return ret;
    }

    send_packet_and_ack((mit_packet_t *)transmit_buffer);
    return SUCCESS_RETURN;
}

int process_validate() {
    // The AP requested a validation. Respond with the Component ID

    // TODO gross data allocation ?
    mit_comp_id_t component_id = COMPONENT_ID;
    int ret = make_mit_packet(COMPONENT_ID, MIT_CMD_VALIDATE, &component_id, sizeof(mit_comp_id_t));

    if (ret != SUCCESS_RETURN) {
        return ret;
    }

    send_packet_and_ack((mit_packet_t *)transmit_buffer);
    return SUCCESS_RETURN;
}

int process_attest() {
    // The AP requested attestation. Respond with the attestation data

    // TODO gross data allocation ?
    // TODO + 1 needed?
    uint8_t len = sprintf((char*)working_buffer, "LOC>%s\nDATE>%s\nCUST>%s\n",
                ATTESTATION_LOC, ATTESTATION_DATE, ATTESTATION_CUSTOMER) + 1;
    int ret = make_mit_packet(COMPONENT_ID, MIT_CMD_ATTEST, working_buffer, len);

    if (ret != SUCCESS_RETURN) {
        return ret;
    }

    send_packet_and_ack((mit_packet_t *)transmit_buffer);
    return SUCCESS_RETURN;
}

/*********************************** MAIN *************************************/

int main(void) {
    // Enable Global Interrupts
    __enable_irq();

    // MIT: Initialize our custom features
    common_init();
    session_init();
    
    // Initialize Component
    i2c_addr_t addr = component_id_to_i2c_addr(COMPONENT_ID);
    board_link_init(addr);
    
    LED_On(LED2);

    while (1) {
        wait_and_receive_packet(receive_buffer);

        if (component_process_cmd() != SUCCESS_RETURN) {
            send_ack();
        }
    }
}
