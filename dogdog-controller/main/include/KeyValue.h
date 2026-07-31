#ifndef __KEY_VALUE_H
#define __KEY_VALUE_H

#include <stdio.h>
#include "nvs_flash.h"
#include "nvs.h"
#include "esp_err.h"
#include "esp_log.h"
#include <stdint.h>

esp_err_t storeValue(const char *key, int32_t value);
esp_err_t getValueChecked(const char *key, int32_t *value);
esp_err_t increaseKey(const char *key);

#endif
