/**
 * @file "ap_utilities.c"
 * @brief Misc utilities for AP
 */

#include "ap_common.h"

/********************************* CONSTANTS **********************************/

// Passed in through ectf-params.h
// Example of format of ectf-params.h shown here
/*
#define AP_PIN "123456"
#define AP_TOKEN "0123456789abcdef"
#define COMPONENT_IDS 0x11111124, 0x11111125
#define COMPONENT_CNT 2
#define AP_BOOT_MSG "Test boot message"
*/

// Flash Macros
#define FLASH_ADDR ((MXC_FLASH_MEM_BASE + MXC_FLASH_MEM_SIZE) - (2 * MXC_FLASH_PAGE_SIZE))
#define FLASH_MAGIC 0xDEADBEEF

/******************************** TYPE DEFINITIONS ********************************/

// Datatype for information stored in flash
typedef struct {
    uint32_t flash_magic;
    uint32_t component_cnt;
    uint32_t component_ids[32];
} flash_entry;

/********************************* GLOBAL VARIABLES **********************************/
// Variable for information stored in flash memory
flash_entry flash_status;

// Buffers for board link communication
uint8_t receive_buffer[MAX_I2C_MESSAGE_LEN];
uint8_t transmit_buffer[MAX_I2C_MESSAGE_LEN];

/********************************* UTILITIES **********************************/

// TODO gross data allocation?
uint8_t ap_plaintext[AP_PLAINTEXT_LEN];

// TODO store in ecc ram?
// TODO do we need 32 sessions? supporting only 2 component ids seems fine.
mit_session_t sessions[32];

// TODO replace with CHACHA20_POLY1305_AEAD_IV_SIZE
uint8_t null_nonce[MIT_NONCE_SIZE] = {0};

void session_init(void) {
    // Copy stored component_ids into sessions.
    // Initialize nonce's to {0}.
    for (int i = 0; i < get_num_components(); i++) {
        sessions[i].component_id = get_component_id(i);
        memset(sessions[i].outgoing_nonce.rawBytes, 0, sizeof(mit_nonce_t));
        memset(sessions[i].incoming_nonce.rawBytes, 0, sizeof(mit_nonce_t));

        // Initialize outgoing nonces to some random value
        while (memcmp(sessions[i].outgoing_nonce.rawBytes, null_nonce, sizeof(mit_nonce_t)) == 0) {
            get_rand_bytes(sessions[i].outgoing_nonce.rawBytes, sizeof(mit_nonce_t));
        }
    }
}

mit_session_t * get_session_of_component(mit_comp_id_t component_id) {
    for (int i = 0; i < get_num_components(); i++) {
        if (sessions[i].component_id == component_id) {
            return &sessions[i];
        }
    }

    return NULL;
}

// TODO bake num_components into source code vs this struct?
// Return number of provisioned components
int get_num_components(void) {
    return flash_status.component_cnt;
}

// Return component_id stored in slot `id`
mit_comp_id_t get_component_id(int id) {
    if (id < flash_status.component_cnt) {
        return flash_status.component_ids[id];
    }

    return ERROR_RETURN;
}

// Find component id from i2c addr
int i2c_addr_to_component_id(i2c_addr_t addr, mit_comp_id_t * component_id) {
    for (int i = 0; i < get_num_components(); i++) {
        mit_comp_id_t provisioned_component = flash_status.component_ids[i];
        i2c_addr_t provisioned_addr = component_id_to_i2c_addr(provisioned_component);
        if (provisioned_addr == addr) {
            *component_id = provisioned_component;
            return SUCCESS_RETURN;
        }
    }

    return ERROR_RETURN;
}

// Test application has been booted before
void flash_first_boot(void) {
    flash_simple_read(FLASH_ADDR, (uint32_t*)&flash_status, sizeof(flash_entry));

    // Write Component IDs from flash if first boot e.g. flash unwritten
    if (flash_status.flash_magic != FLASH_MAGIC) {
        print_debug("First boot, setting flash!\n");
        flash_simple_erase_page(FLASH_ADDR);

        flash_status.flash_magic = FLASH_MAGIC;
        flash_status.component_cnt = COMPONENT_CNT;
        uint32_t component_ids[COMPONENT_CNT] = {COMPONENT_IDS};
        memcpy(flash_status.component_ids, component_ids, 
            COMPONENT_CNT*sizeof(uint32_t));

        flash_simple_write(FLASH_ADDR, (uint32_t*)&flash_status, sizeof(flash_entry));
    }
}

// Swap component IN with component OUT
int swap_components(mit_comp_id_t component_id_in, mit_comp_id_t component_id_out) {
    // Find the component to swap out
    for (unsigned i = 0; i < flash_status.component_cnt; i++) {
        if (flash_status.component_ids[i] == component_id_out) {
            // Swap out component id
            flash_status.component_ids[i] = component_id_in;

            // Reset session info
            sessions[i].component_id = component_id_in;
            memset(sessions[i].outgoing_nonce.rawBytes, 0, sizeof(mit_nonce_t));
            memset(sessions[i].incoming_nonce.rawBytes, 0, sizeof(mit_nonce_t));
            get_rand_bytes(sessions[i].outgoing_nonce.rawBytes, sizeof(mit_nonce_t));

            // write updated component_ids to flash
            flash_simple_erase_page(FLASH_ADDR);
            flash_simple_write(FLASH_ADDR, (uint32_t*)&flash_status, sizeof(flash_entry));

            return SUCCESS_RETURN;
        }
    }

    return ERROR_RETURN;
}

// Send a command to a component and receive the result
int issue_cmd(mit_comp_id_t component_id) {
    int ret;

    i2c_addr_t addr = component_id_to_i2c_addr((uint32_t)component_id);

    // TODO validate current tx packet belongs to stated component id
    mit_packet_t * packet = get_tx_packet();
    if (packet->ad.comp_id != component_id) {
        print_error("issue_cmd: packet in buf doesnt match given component id\n");
        return ERROR_RETURN;
    }

    // TODO cleanup use of transmit_buffer, receive_buffer here. Not necessary as args?
    // Send message
    int result = send_mit_packet(addr, (mit_packet_t *)transmit_buffer);
    if (result == ERROR_RETURN) {
        print_error("issue_cmd: send_mit_packet error\n");
        return ERROR_RETURN;
    }

    // Receive message
    int len = poll_and_receive_packet(addr, receive_buffer);
    if (len == ERROR_RETURN) {
        print_error("issue_cmd: poll_and_receive_packet error\n");
        return ERROR_RETURN;
    }

    /*************** VALIDATE RECEIVED PACKET ****************/
    packet = get_rx_packet();

    if (packet->ad.comp_id != component_id) {
        print_error("rx packet (0x%08x) doesn't match given component id (0x%08x)\n", packet->ad.comp_id, component_id);
        return ERROR_RETURN;
    }

    if (packet->ad.for_ap != true) {
        print_error("rx packet not tagged for AP\n");
        return ERROR_RETURN;
    }

    if (packet->ad.len == 0) {
        print_error("rx packet has null message length\n");
        return ERROR_RETURN;
    }

    mit_session_t * session = get_session_of_component(component_id);
    if (session == NULL) {
        print_error("Session not found for component id 0x%08x\n", component_id);
    }

    // if we currently have a null nonce, then trust the incoming nonce, as long as it passes authtag check.
    if (memcmp(session->incoming_nonce.rawBytes, null_nonce, sizeof(mit_nonce_t)) == 0) {
        // Validate packet
        ret = mit_decrypt(packet, ap_plaintext);

        if (ret != SUCCESS_RETURN) {
            print_error("decryption failed with error %i\n", ret);
            memset(ap_plaintext, 0, AP_PLAINTEXT_LEN);
            return ERROR_RETURN;
        }

        // Save nonce into session
        memcpy(session->incoming_nonce.rawBytes, packet->ad.nonce.rawBytes, sizeof(mit_nonce_t));

    } else if (memcmp(session->incoming_nonce.rawBytes, packet->ad.nonce.rawBytes, sizeof(mit_nonce_t)) == 0) {
        // incoming nonce matches expected nonce
        ret = mit_decrypt(packet, ap_plaintext);

        if (ret != SUCCESS_RETURN) {
            print_error("decryption failed with error %i\n", ret);
            memset(ap_plaintext, 0, AP_PLAINTEXT_LEN);
            return ERROR_RETURN;
        }

    } else {
        print_error("Incoming nonce (seq 0x%08x) doesn't match expected nonce (seq 0x%08x)\n",
            packet->ad.nonce.sequenceNumber, session->incoming_nonce.sequenceNumber
        );
        return ERROR_RETURN;
    }

    // TODO best place for this?
    // increase incoming nonce
    session->incoming_nonce.sequenceNumber += 1;

    /********************************************************/

    return len;
}

/******************************* MIT UTILITIES ********************************/

// TODO remove
// Send packet at *packet to addr
int send_mit_packet(i2c_addr_t addr, mit_packet_t * packet) {
    uint8_t len = packet->ad.len + sizeof(mit_ad_t) + sizeof(mit_authtag_t);
    return send_packet(addr, len, packet);
}

void set_ad(mit_packet_t * packet, mit_comp_id_t comp_id, mit_opcode_t opcode, uint8_t len) {
    // TODO limits check on len?
    // packet->ad.nonce.sequenceNumber = 0; // TODO
    packet->ad.comp_id = comp_id;
    packet->ad.opcode = opcode;
    packet->ad.len = len;
    packet->ad.for_ap = false; // TODO use ifdefs w/ AP_BOOT_MSG to resolve this in common code?
}

/**
 * @brief Determine if component_id is valid
 * 
 * @param component_id: mit_comp_id_t, id of component
 * 
 * Returns true if component found in status structs, else false
 */
bool is_valid_component(mit_comp_id_t component_id) {
    for (int i = 0; i < flash_status.component_cnt; i++) {
        if (flash_status.component_ids[i] == component_id) {
            return true;
        }
    }

    return false;
}


// TODO do we really need this? could just stuff random bytes in packet instead.
mit_nonce_t ephemeral_nonce;

/**
 * @brief Ephemeral scanner for List command
 * 
 * This is a mashup of make_mit_packet and issue_cmd.
 * The reason is because both do not allow for creating / decoding
 * packets for non-provisioned components, but we need to be
 * able to scan for all connected devices for the List command.s
 */
int ephemeral_handshake(mit_comp_id_t component_id) {
    int ret;

    // TODO bounds check on len?
    mit_packet_t * packet = (mit_packet_t *)transmit_buffer;

    // Set Authenticated Data field
    set_ad(packet, component_id, MIT_CMD_SCAN, 1);

    /***** NONCE GENERATION/LOOKUP *****/

    // WARNING reusing a nonce is the worst thing you can possibly do.

    mit_session_t * session = get_session_of_component(component_id);
    if (session == NULL) {
        // If component_id doesn't match a provisioned component, use an ephemeral nonce
        get_rand_bytes(ephemeral_nonce.rawBytes, sizeof(mit_nonce_t));
    } else {
        // if the nonce is 0, generate a random nonce
        while (memcmp(null_nonce, session->outgoing_nonce.rawBytes, sizeof(mit_nonce_t)) == 0) {
            get_rand_bytes(session->outgoing_nonce.rawBytes, sizeof(mit_nonce_t));
        }
        memcpy(ephemeral_nonce.rawBytes, session->outgoing_nonce.rawBytes, sizeof(mit_nonce_t));
    }

    memcpy(packet->ad.nonce.rawBytes, ephemeral_nonce.rawBytes, sizeof(mit_nonce_t));

    // TODO necessary? check if we actually copied
    if (memcmp(packet->ad.nonce.rawBytes, ephemeral_nonce.rawBytes, sizeof(mit_nonce_t))) {
        print_error("Failed to copy nonce!\n");
        return ERROR_RETURN;
    }

    /****************************/

    // Encrypt in data
    // Doesn't matter what we send in this message.
    ret = mit_encrypt(packet, ap_plaintext, 1);

    if (ret != SUCCESS_RETURN) {
        print_error("encryption failed with error %i\n", ret);
        memset(packet, 0, sizeof(mit_packet_t));
        return ERROR_RETURN;
    }

    // TODO best place to increase nonce?
    if (session != NULL) {
        session->outgoing_nonce.sequenceNumber += 1;
    }

    /********** get response ************/
    /********** get response ************/
    /********** get response ************/
    /********** get response ************/
    /********** get response ************/
    /********** get response ************/
    i2c_addr_t addr = component_id_to_i2c_addr(component_id);

    // TODO validate current tx packet belongs to stated component id
    packet = get_tx_packet();
    if (packet->ad.comp_id != component_id) {
        print_error("ephemeral_handshake: packet in buf doesnt match given component id\n");
        return ERROR_RETURN;
    }

    // TODO cleanup use of transmit_buffer, receive_buffer here. Not necessary as args?
    // Send message
    ret = send_mit_packet(addr, (mit_packet_t *)transmit_buffer);
    if (ret == ERROR_RETURN) {
        // print_error("ephemeral_handshake: send_mit_packet error\n");
        return ERROR_RETURN;
    }

    // Receive message
    int len = poll_and_receive_packet(addr, receive_buffer);
    if (len == ERROR_RETURN) {
        print_error("ephemeral_handshake: poll_and_receive_packet error\n");
        return ERROR_RETURN;
    }

    /*************** VALIDATE RECEIVED PACKET ****************/
    packet = get_rx_packet();

    // Masked comparison since we may be blindly scanning.
    if ((packet->ad.comp_id & 0xff) != (component_id & 0xff)) {
        print_error("rx packet (0x%08x) doesn't match given component id (0x%08x)\n", packet->ad.comp_id, component_id);
        return ERROR_RETURN;
    }

    if (packet->ad.for_ap != true) {
        print_error("rx packet not tagged for AP\n");
        return ERROR_RETURN;
    }

    if (packet->ad.len == 0) {
        print_error("rx packet has null message length\n");
        return ERROR_RETURN;
    }

    /**
     * If we don't have a session, trust as long as authTag checks out.
     * 
     * If we already had an established session with this component (session != NULL)
     * then decrypt and authenticate as normal, etc.
     */
    if (session == NULL) {
        // Validate authTag field
        ret = mit_decrypt(packet, ap_plaintext);

        if (ret != SUCCESS_RETURN) {
            print_error("decryption failed in ephemeral_handshake, errcode %i\n", ret);
            memset(packet, 0, sizeof(mit_packet_t));
            return ERROR_RETURN;
        }
    } else {
        // if we currently have a null nonce, then trust the incoming nonce, as long as it passes authtag check.
        if (memcmp(session->incoming_nonce.rawBytes, null_nonce, sizeof(mit_nonce_t)) == 0) {
            // Validate packet
            ret = mit_decrypt(packet, ap_plaintext);

            if (ret != SUCCESS_RETURN) {
                print_error("decryption failed in ephemeral_handshake, errcode %i\n", ret);
                memset(ap_plaintext, 0, AP_PLAINTEXT_LEN);
                return ERROR_RETURN;
            }

            // Save nonce into session
            memcpy(session->incoming_nonce.rawBytes, packet->ad.nonce.rawBytes, sizeof(mit_nonce_t));

        } else if (memcmp(session->incoming_nonce.rawBytes, packet->ad.nonce.rawBytes, sizeof(mit_nonce_t)) == 0) {
            // incoming nonce matches expected nonce
            ret = mit_decrypt(packet, ap_plaintext);

            if (ret != SUCCESS_RETURN) {
                print_error("decryption failed in ephemeral_handshake, errcode %i\n", ret);
                memset(ap_plaintext, 0, AP_PLAINTEXT_LEN);
                return ERROR_RETURN;
            }

        } else {
            print_error("Incoming nonce (seq 0x%08x) doesn't match expected nonce (seq 0x%08x)\n",
                packet->ad.nonce.sequenceNumber, session->incoming_nonce.sequenceNumber
            );
            return ERROR_RETURN;
        }
    }

    // Print scanned component id
    print_info("F>0x%08x\n", packet->ad.comp_id);

    // TODO best place for this?
    // increase incoming nonce
    if (session != NULL) {
        session->incoming_nonce.sequenceNumber += 1;
    }

    /********************************************************/

    return SUCCESS_RETURN;
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

    mit_session_t * session = get_session_of_component(component_id);
    if (session == NULL) {
        print_error("No session found for component_id 0x%08x\n", component_id);
        return ERROR_RETURN;
    }

    // if the nonce is 0, generate a random nonce
    while (memcmp(null_nonce, session->outgoing_nonce.rawBytes, sizeof(mit_nonce_t)) == 0) {
        get_rand_bytes(session->outgoing_nonce.rawBytes, sizeof(mit_nonce_t));
    }

    memcpy(packet->ad.nonce.rawBytes, session->outgoing_nonce.rawBytes, sizeof(mit_nonce_t));

    // TODO do we really need this :)
    if (memcmp(packet->ad.nonce.rawBytes, session->outgoing_nonce.rawBytes, sizeof(mit_nonce_t))) {
        print_error("Failed to copy nonce!\n");
        return ERROR_RETURN;
    }
    /****************************/

    ret = mit_encrypt(packet, data, len);

    if (ret != SUCCESS_RETURN) {
        print_error("encryption failed with error %i\n", ret);
        memset(packet, 0, sizeof(mit_packet_t)); // clear packet
        return ERROR_RETURN;
    }

    // TODO best place to increase nonce?
    session->outgoing_nonce.sequenceNumber += 1;

    return SUCCESS_RETURN;
}

/******************************* GROSS INTERFACE ********************************/
mit_packet_t * get_rx_packet(void) {
    return (mit_packet_t *)receive_buffer;
}

mit_packet_t * get_tx_packet(void) {
    return (mit_packet_t *)transmit_buffer;
}
