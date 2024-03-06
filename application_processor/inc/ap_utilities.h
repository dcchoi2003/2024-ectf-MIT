/**
 * @file "ap_utilities.h"
 * @brief Misc utilities for AP
 */

#ifndef _AP_UTILITIES_H_
#define _AP_UTILITIES_H_

#include "ap_common.h"

// Reset nonces for our i2c comms sessions
void session_init(void);

mit_session_t * get_session_of_component(mit_comp_id_t component_id);

// Return number of provisioned components
int get_num_components(void);

// Return component_id stored in slot `id`
mit_comp_id_t get_component_id(int id);

// Find component id from i2c addr
int i2c_addr_to_component_id(i2c_addr_t address, mit_comp_id_t * component_id);

// Test application has been booted before
void flash_first_boot(void);

// Swap component IN with component OUT
int swap_components(mit_comp_id_t component_id_in, mit_comp_id_t component_id_out);

// Send a command to a component and receive the result
int issue_cmd(mit_comp_id_t component_id);

// TODO remove *packet arg
// Send packet at *packet to addr
int send_mit_packet(i2c_addr_t addr, mit_packet_t * packet);

// Checks if component id is currently provisioned
bool is_valid_component(mit_comp_id_t component_id);

// Ephemeral scanner for List command
int ephemeral_handshake(mit_comp_id_t component_id);

// Helper to construct packet, stores in transmit_buffer
int make_mit_packet(mit_comp_id_t component_id, mit_opcode_t opcode, uint8_t * data, uint8_t len);

/******************************* GROSS INTERFACE ********************************/
mit_packet_t * get_rx_packet(void);
mit_packet_t * get_tx_packet(void);

#endif
