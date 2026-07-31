#ifndef KEY_VALUE_H
#define KEY_VALUE_H

#include <stdint.h>

#include "esp_err.h"

esp_err_t storeValue(const char *key, int32_t value);
esp_err_t getValue(const char *key, int32_t *value);
esp_err_t increaseKey(const char *key);

#endif
