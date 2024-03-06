/**
 * @file ap_postboot.c
 * @brief Post-Boot-related functions for AP
 */

#include "ap_common.h"

/**
 * @brief Secure Send 
 * 
 * @param address: i2c_addr_t, I2C address of recipient
 * @param buffer: uint8_t*, pointer to data to be send
 * @param len: uint8_t, size of data to be sent 
 * 
 * Securely send data over I2C. This function is utilized in POST_BOOT functionality.
 * This function must be implemented by your team to align with the security requirements.

*/
int secure_send(uint8_t address, uint8_t* buffer, uint8_t len) {
    // TODO bounds check on len?

    mit_comp_id_t component_id = 0;
    int ret = i2c_addr_to_component_id(address, &component_id);

    if (ret == ERROR_RETURN) {
        print_error("secure_receive: bad address provided 0x%02x\n", address);
        return ERROR_RETURN;
    }

    ret = make_mit_packet(component_id, MIT_CMD_NONE, buffer, len);
    if (ret != SUCCESS_RETURN) {
        return ret;
    }
    return send_mit_packet(component_id, get_tx_packet());
}

/**
 * @brief Secure Receive
 * 
 * @param address: i2c_addr_t, I2C address of sender
 * @param buffer: uint8_t*, pointer to buffer to receive data to
 * 
 * @return int: number of bytes received, negative if error
 * 
 * Securely receive data over I2C. This function is utilized in POST_BOOT functionality.
 * This function must be implemented by your team to align with the security requirements.
*/
int secure_receive(i2c_addr_t address, uint8_t* buffer) {
    // TODO bounds check on len?

    // TODO impl
    mit_comp_id_t component_id = 0;
    int ret = i2c_addr_to_component_id(address, &component_id);

    if (ret == ERROR_RETURN) {
        print_error("secure_receive: bad address provided 0x%02x\n", address);
        return ERROR_RETURN;
    }

    mit_packet_t * packet = get_rx_packet();

    int len = poll_and_receive_packet(address, packet);

    if (len == ERROR_RETURN) {
        print_error("issue_cmd: poll_and_receive_packet error\n");
        return ERROR_RETURN;
    }

    /*** Validate received packet ***/
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

    // We don't allow resetting nonces post-boot
    if (memcmp(session->incoming_nonce.rawBytes, packet->ad.nonce.rawBytes, sizeof(mit_nonce_t)) == 0) {
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

    memcpy(buffer, ap_plaintext, len);

    // TODO best place for this?
    // increase incoming nonce
    session->incoming_nonce.sequenceNumber += 1;

    /********************************************************/

    return len;
}

/**
 * @brief Get Provisioned IDs
 * 
 * @param uint32_t* buffer
 * 
 * @return int: number of ids
 * 
 * Return the currently provisioned IDs and the number of provisioned IDs
 * for the current AP. This functionality is utilized in POST_BOOT functionality.
 * This function must be implemented by your team.
*/
int get_provisioned_ids(uint32_t* buffer) {
    for (int id = 0; id < get_num_components(); id++) {
        buffer[id] = get_component_id(id);
    }
    return get_num_components();
}
