
#ifndef _NICE_H_
#define _NICE_H_

#ifdef __cplusplus
const uint8_t * getHashedPinPtrCpp(void);
#endif

#ifdef __cplusplus
extern "C" {
#endif

const uint8_t * getHashedPinPtr(void);

#ifdef __cplusplus
}
#endif

#endif
