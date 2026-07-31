#include "KeyValue.h"
#include <limits.h>

#define STORAGE_NAMESPACE "storage"

esp_err_t storeValue(const char *key, int32_t value)
{
    if (key == NULL)
    {
        return ESP_ERR_INVALID_ARG;
    }

    nvs_handle_t my_handle;

    esp_err_t err = nvs_open(STORAGE_NAMESPACE, NVS_READWRITE, &my_handle);
    if (err != ESP_OK)
    {
        ESP_LOGE("KeyValue", "Error (%s) opening NVS handle!", esp_err_to_name(err));
        return err;
    }

    err = nvs_set_i32(my_handle, key, value);
    if (err != ESP_OK)
    {
        ESP_LOGE("KeyValue", "Error (%s) setting int value!", esp_err_to_name(err));
        nvs_close(my_handle);
        return err;
    }

    err = nvs_commit(my_handle);
    if (err != ESP_OK)
    {
        ESP_LOGE("KeyValue", "Error (%s) committing value!", esp_err_to_name(err));
    }
    nvs_close(my_handle);
    return err;
}

esp_err_t getValueChecked(const char *key, int32_t *value)
{
    if (key == NULL || value == NULL)
    {
        return ESP_ERR_INVALID_ARG;
    }

    nvs_handle_t my_handle;
    esp_err_t err = nvs_open(STORAGE_NAMESPACE, NVS_READONLY, &my_handle);
    if (err != ESP_OK)
    {
        if (err != ESP_ERR_NVS_NOT_FOUND)
        {
            ESP_LOGE("KeyValue", "Error (%s) opening NVS handle!", esp_err_to_name(err));
        }
        return err;
    }

    err = nvs_get_i32(my_handle, key, value);
    nvs_close(my_handle);
    if (err != ESP_OK && err != ESP_ERR_NVS_NOT_FOUND)
    {
        ESP_LOGE("KeyValue", "Error (%s) reading key '%s'!", esp_err_to_name(err), key);
    }
    return err;
}

esp_err_t increaseKey(const char *key)
{
    int32_t value = 0;
    esp_err_t err = getValueChecked(key, &value);
    if (err != ESP_OK && err != ESP_ERR_NVS_NOT_FOUND)
    {
        return err;
    }
    if (value < 0)
    {
        ESP_LOGW("KeyValue", "Resetting negative counter '%s'", key);
        value = 0;
    }
    if (value == INT32_MAX)
    {
        ESP_LOGW("KeyValue", "Counter '%s' is saturated", key);
        return ESP_ERR_INVALID_SIZE;
    }
    return storeValue(key, value + 1);
}
