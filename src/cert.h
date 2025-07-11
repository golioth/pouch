#pragma once

#include <pouch/types.h>
#include <stdbool.h>
#include <mbedtls/x509_crt.h>
#include "pouch.h"

#define CERT_REF_HASH_ALG PSA_ALG_SHA_256
#define CERT_REF_LEN PSA_HASH_LENGTH(CERT_REF_HASH_ALG)
#define CERT_REF_SHORT_LEN 6

/* Max serial number length is 20 bytes according to spec:
 * https://datatracker.ietf.org/doc/html/rfc5280#section-4.1.2.2
 */
#define CERT_SERIAL_MAXLEN 20

int cert_device_set(const struct pouch_cert *cert);
int cert_server_set(const struct pouch_cert *cert);
const uint8_t *cert_ref_get(void);

void cert_server_key_get(struct pubkey *out);
bool cert_has_server_info(void);
