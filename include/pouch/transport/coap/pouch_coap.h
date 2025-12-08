#pragma once

#include <zephyr/net/tls_credentials.h>

int pouch_coap_init(const sec_tag_t *sec_tag_list, size_t sec_tag_count);
int pouch_coap_sync(void);
