#ifndef DD_OTA_H
#define DD_OTA_H

#include <stdbool.h>

#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

/** Coarse-grained OTA progress states delivered from the OTA worker task. */
typedef enum
{
    DD_OTA_CONNECTING,
    DD_OTA_DOWNLOADING,
    DD_OTA_VALIDATING,
    DD_OTA_SUCCEEDED,
    DD_OTA_FAILED,
} dd_ota_state_t;

typedef void (*dd_ota_status_cb_t)(dd_ota_state_t state, esp_err_t error,
                                   void *context);

typedef struct
{
    dd_ota_status_cb_t status_cb;
    void *context;
} dd_ota_options_t;

/**
 * Consume an OTA reboot request left in RTC memory by dd_ota_request_reboot().
 *
 * The marker is one-shot and is accepted only after an ESP_RST_SW reset. Call
 * this once, near the beginning of app_main(), before starting normal radio or
 * network services.
 */
bool dd_ota_consume_reboot_request(void);

/** Store a one-shot OTA marker and immediately restart the device. */
void dd_ota_request_reboot(void) __attribute__((noreturn));

/**
 * Start the shared OTA worker.
 *
 * The options are copied before this function returns and may therefore be
 * stack allocated. Only one worker can run at a time. The worker owns Wi-Fi,
 * downloads <project-name>.bin, reports status, and restarts on both success
 * and failure.
 */
esp_err_t dd_ota_start(const dd_ota_options_t *options);

/**
 * Confirm a pending image after the application's critical initialization has
 * completed. The call is idempotent for already-valid images.
 */
esp_err_t dd_ota_mark_app_valid(void);

#ifdef __cplusplus
}
#endif

#endif // DD_OTA_H
