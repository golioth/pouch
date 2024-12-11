/*
 * Copyright (c) 2024 Golioth
 */

struct toothfairy_peripheral;

struct toothfairy_peripheral *toothfairy_peripheral_create(void);
int toothfairy_peripheral_destroy(struct toothfairy_peripheral *tf_peripheral);
int toothfairy_peripheral_start(struct toothfairy_peripheral *tf_peripheral);
int toothfairy_peripheral_stop(struct toothfairy_peripheral *tf_peripheral);
