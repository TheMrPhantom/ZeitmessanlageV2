#include <stdio.h>
#include "nvs_flash.h"
#include "nvs.h"
#include "esp_err.h"

void storeValue(const char *key, uint32_t value);
int getValue(const char *key);
void increaseKey(const char *key);