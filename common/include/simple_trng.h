/**
 * @file "simple_trng.h"
 * @brief TRNG Interface
 */

#ifndef _SIMPLE_TRNG_H_
#define _SIMPLE_TRNG_H_

#include "trng.h"
#include <string.h>

/**
 * @brief Initialize TRNG hardware 
 */
void simple_trng_init(void);

/**
 * @brief Collect random bytes
 * 
 * @param buf: uint8_t*, ptr to random bytes store
 * @param num_bytes: int, number of random bytes to fetch
 */
void get_rand_bytes(uint8_t * buf, int num_bytes);

#endif