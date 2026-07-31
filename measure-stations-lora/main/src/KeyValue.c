#include "KeyValue.h"

#include <inttypes.h>
#include <limits.h>

#include "esp_log.h"
#include "nvs.h"

#define STORAGE_NAMESPACE "storage"

static const char *TAG = "KeyValue";

esp_err_t storeValue(const char *key, int32_t value)
{
    if (key == NULL)
    {
        return ESP_ERR_INVALID_ARG;
    }

    nvs_handle_t handle;
    esp_err_t err = nvs_open(STORAGE_NAMESPACE, NVS_READWRITE, &handle);
    if (err != ESP_OK)
    {
        ESP_LOGE(TAG, "Error (%s) opening NVS write handle", esp_err_to_name(err));
        return err;
    }

    int32_t current_value;
    err = nvs_get_i32(handle, key, &current_value);
    if (err == ESP_OK && current_value == value)
    {
        // Avoid an unnecessary flash write on every boot.
        nvs_close(handle);
        return ESP_OK;
    }
    if (err != ESP_OK && err != ESP_ERR_NVS_NOT_FOUND && err != ESP_ERR_NVS_TYPE_MISMATCH)
    {
        nvs_close(handle);
        ESP_LOGE(TAG, "Error (%s) checking existing NVS key '%s'", esp_err_to_name(err), key);
        return err;
    }

    err = nvs_set_i32(handle, key, value);
    if (err == ESP_OK)
    {
        err = nvs_commit(handle);
    }
    nvs_close(handle);

    if (err != ESP_OK)
    {
        ESP_LOGE(TAG, "Error (%s) storing NVS key '%s'", esp_err_to_name(err), key);
    }
    return err;
}

esp_err_t getValue(const char *key, int32_t *value)
{
    if (key == NULL || value == NULL)
    {
        return ESP_ERR_INVALID_ARG;
    }

    nvs_handle_t handle;
    esp_err_t err = nvs_open(STORAGE_NAMESPACE, NVS_READONLY, &handle);
    if (err != ESP_OK)
    {
        if (err != ESP_ERR_NVS_NOT_FOUND)
        {
            ESP_LOGE(TAG, "Error (%s) opening NVS read handle", esp_err_to_name(err));
        }
        return err;
    }

    int32_t stored_value = 0;
    err = nvs_get_i32(handle, key, &stored_value);
    nvs_close(handle);

    if (err == ESP_OK)
    {
        *value = stored_value;
        ESP_LOGI(TAG, "Read NVS key '%s': %" PRId32, key, stored_value);
    }
    else if (err != ESP_ERR_NVS_NOT_FOUND)
    {
        ESP_LOGE(TAG, "Error (%s) reading NVS key '%s'", esp_err_to_name(err), key);
    }
    return err;
}

esp_err_t increaseKey(const char *key)
{
    int32_t value = 0;
    esp_err_t err = getValue(key, &value);
    if (err != ESP_OK && err != ESP_ERR_NVS_NOT_FOUND)
    {
        return err;
    }
    if (value == INT32_MAX)
    {
        return ESP_ERR_INVALID_STATE;
    }
    return storeValue(key, value + 1);
}
