#!/usr/bin/env python3
# og author: utaha1228

KEY_SIZE = 32
OUTPUT_FILE = "global_secrets.h"

with open("/dev/random", "rb") as f:
    secret = f.read(KEY_SIZE).hex()
    attest = f.read(KEY_SIZE).hex()

secret_c_str = "".join(["\\x" + secret[i:i+2] for i in range(0, len(secret), 2)])
attest_c_str = ",".join(["0x" + attest[i:i+2] for i in range(0, len(secret), 2)])

output = f"""\
#define SHARED_SECRET_KEY (uint8_t *)("{secret_c_str}")
#define ATTEST_SECRET_KEY {attest_c_str}
"""

with open(OUTPUT_FILE, "w") as f:
	f.write(output)
