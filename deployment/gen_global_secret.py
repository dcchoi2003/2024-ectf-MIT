import os

OUTPUT_FILE = "global_secrets.h"

secret = os.urandom(16).hex()
secret_c_str = "".join(["\\x" + secret[i:i+2] for i in range(0, len(secret), 2)])

output = f"""\
#define SECRET ("{secret_c_str}")
"""

with open(OUTPUT_FILE, "w") as f:
	f.write(output)