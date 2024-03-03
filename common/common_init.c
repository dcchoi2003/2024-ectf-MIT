/**
 * @file "common_init.c"
 * @brief Common initialization needs
 */

#include "common_init.h"
#include "simple_trng.h"

void common_init(void) {
    simple_trng_init();
}
