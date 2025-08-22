#include "KeyValue.h"

#define STORAGE_NAMESPACE "storage"

void storeValue(const char *key, uint32_t value)
{
    nvs_handle_t my_handle;

    esp_err_t err = nvs_open(STORAGE_NAMESPACE, NVS_READWRITE, &my_handle);
    if (err != ESP_OK)
    {
        printf("Error (%s) opening NVS handle!\n", esp_err_to_name(err));
    }
    else
    {
        printf("NVS write handle opened successfully.\n");

        // Store value
        err = nvs_set_i32(my_handle, key, value);
        if (err != ESP_OK)
        {
            printf("Error (%s) setting int value!\n", esp_err_to_name(err));
        }
        else
        {
            printf("Value stored successfully.\n");

            // Commit written value
            err = nvs_commit(my_handle);
            if (err != ESP_OK)
            {
                printf("Error (%s) committing value!\n", esp_err_to_name(err));
            }
            else
            {
                printf("Value committed successfully.\n");
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
        printf("Error (%s) opening NVS handle!\n", esp_err_to_name(err));
    }
    else
    {
        printf("NVS read handle opened successfully.\n");

        // Read value back
        int32_t value_read = 0; // variable to hold the read value
        err = nvs_get_i32(my_handle, key, &value_read);
        switch (err)
        {
        case ESP_OK:
            printf("Value read successfully: %lu\n", value_read);
            break;
        case ESP_ERR_NVS_NOT_FOUND:
            printf("The value is not initialized yet!\n");
            break;
        default:
            printf("Error (%s) reading!\n", esp_err_to_name(err));
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