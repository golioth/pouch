#pragma once

#include <pouch/certificate.h>
#include <pouch/types.h>
#include <stdbool.h>
#include <mbedtls/x509_crt.h>
#include "pouch.h"

#define CERT_REF_HASH_ALG PSA_ALG_SHA_256
#define CERT_REF_LEN PSA_HASH_LENGTH(CERT_REF_HASH_ALG)
#define CERT_REF_SHORT_LEN 6

int cert_device_set(const struct pouch_cert *cert);
int cert_server_set(const struct pouch_cert *cert);
const uint8_t *cert_ref_get(void);

void cert_server_key_get(struct pubkey *out);
bool cert_has_server_info(void);
