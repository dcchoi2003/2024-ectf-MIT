/**
 * @file ap_attest.c
 * @brief Attest-related functions for AP
 */

#include "ap_common.h"

// Attest a component if the PIN is correct
void attempt_attest() {
    char * buf = get_uart_buf();

    if (validate_pin()) {
        return;
    }
    uint32_t component_id;
    recv_input("Component ID: ", 10); // 0x + 8 chars
    sscanf(buf, "%x", &component_id);
    if (attest_component(component_id) == SUCCESS_RETURN) {
        print_success("Attest\n");
    }
}

// Send attestation request to specific component
int attest_component(uint32_t component_id) {
    // TODO check validity of component id

    // Create command message
    uint8_t dummy = 0x55;
    int ret = make_mit_packet(component_id, MIT_CMD_ATTEST, &dummy, 1);
    if (ret != SUCCESS_RETURN) {
        return ret;
    }

    // Send out command and receive result
    int len = issue_cmd(component_id);
    if (len == ERROR_RETURN) {
        print_error("Could not attest component\n");
        return ERROR_RETURN;
    }

    mit_packet_t * packet = get_rx_packet();

    // Print out attestation data 
    print_info("C>0x%08x\n", packet->ad.comp_id);
    print_info("%s", ap_plaintext);
    return SUCCESS_RETURN;
}

// Compare the entered PIN to the correct PIN
int validate_pin(void) {
    char * buf = get_uart_buf();
    recv_input("Enter pin: ", 6);
    if (!strcmp(buf, AP_PIN)) {
        print_debug("Pin Accepted!\n");
        return SUCCESS_RETURN;
    }
    print_error("Invalid PIN!\n");
    return ERROR_RETURN;
}
