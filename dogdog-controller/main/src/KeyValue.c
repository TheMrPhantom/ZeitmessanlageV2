#include "KeyValue.h"

#define STORAGE_NAMESPACE "storage"

void storeValue(const char *key, uint32_t value)
{
    nvs_handle_t my_handle;

    esp_err_t err = nvs_open(STORAGE_NAMESPACE, NVS_READWRITE, &my_handle);
    if (err != ESP_OK)
    {
        ESP_LOGE("KeyValue", "Error (%s) opening NVS handle!", esp_err_to_name(err));
    }
    else
    {
        ESP_LOGI("KeyValue", "NVS write handle opened successfully.");
        // Store value
        err = nvs_set_i32(my_handle, key, value);
        if (err != ESP_OK)
        {
            ESP_LOGE("KeyValue", "Error (%s) setting int value!", esp_err_to_name(err));
        }
        else
        {
            ESP_LOGI("KeyValue", "Value stored successfully.");
            // Commit written value
            err = nvs_commit(my_handle);
            if (err != ESP_OK)
            {
                ESP_LOGE("KeyValue", "Error (%s) committing value!", esp_err_to_name(err));
            }
            else
            {
                ESP_LOGI("KeyValue", "Value committed successfully.");
            }
        }
        // Close NVS handle
        nvs_close(my_handle);
    }
}

int getValue(const char *key)
{
    nvs_handle_t my_handle;

    esp_err_t err = nvs_open(STORAGE_NAMESPACE, NVS_READWRITE, &my_handle);
    if (err != ESP_OK)
    {
        ESP_LOGE("KeyValue", "Error (%s) opening NVS handle!", esp_err_to_name(err));
    }
    else
    {
        ESP_LOGI("KeyValue", "NVS read handle opened successfully.");
        // Read value back
        int32_t value_read = 0; // variable to hold the read value
        err = nvs_get_i32(my_handle, key, &value_read);
        switch (err)
        {
        case ESP_OK:
            ESP_LOGI("KeyValue", "Value read successfully: %lu", value_read);
            break;
        case ESP_ERR_NVS_NOT_FOUND:
            ESP_LOGI("KeyValue", "The value is not initialized yet!");
            break;
        default:
            ESP_LOGE("KeyValue", "Error (%s) reading!", esp_err_to_name(err));
        }
        // Close NVS handle
        nvs_close(my_handle);
        return value_read;
    }
    return 0;
}

void increaseKey(const char *key)
{
    storeValue(key, getValue(key) + 1);
}