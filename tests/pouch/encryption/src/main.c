/*
 * Copyright (c) 2025 Golioth, Inc.
 */
#include <pouch/pouch.h>
#include <psa/crypto.h>


int main(void)
{
    struct pouch_config config = {
        .private_key = PSA_KEY_ID_NULL,
    };
    return pouch_init(&config);
}
