#include "sha256_literal.h"
#include "global_secrets.h"
#include "ectf_params.h"

static constexpr sha256_literal::HashType hashed_pin = sha256_literal::compute({ATTEST_SECRET_KEY});

//static constexpr sha256::HashType hashed_pin = "123456" _sha256;

const uint8_t * getHashedPinPtrCpp(void) {
    return (const uint8_t *)hashed_pin.data();
}

#ifdef __cplusplus
extern "C" {
#endif

const uint8_t * getHashedPinPtr(void) {
    return getHashedPinPtrCpp();
}

#ifdef __cplusplus
}
#endif
