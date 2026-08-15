#ifndef __OTA_H
#define __OTA_H

#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef enum dogdog_ota_event {
    DOGDOG_OTA_EVENT_WIFI_FOUND = 0,
    DOGDOG_OTA_EVENT_CONNECTING,
    DOGDOG_OTA_EVENT_UPDATING,
    DOGDOG_OTA_EVENT_NO_UPDATE,
    DOGDOG_OTA_EVENT_FAILED,
} dogdog_ota_event_t;

typedef void (*dogdog_ota_status_cb_t)(dogdog_ota_event_t event, void *user_ctx);

typedef struct dogdog_ota_config {
    const char *device_name;
    dogdog_ota_status_cb_t status_cb;
    void *user_ctx;
} dogdog_ota_config_t;

esp_err_t dogdog_ota_check_and_update_ex(const dogdog_ota_config_t *config);
esp_err_t dogdog_ota_check_and_update(const char *device_name);

void ota_task(void *pvParameters);

#ifdef __cplusplus
}
#endif

#endif // __OTA_H
