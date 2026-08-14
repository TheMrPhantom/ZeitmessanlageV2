#ifndef __TIMEPANEL_CLIENT_H
#define __TIMEPANEL_CLIENT_H

#include <stdint.h>

#include "esp_err.h"

esp_err_t init_timepanel_client(void);
void timepanel_set_competitor(const char *first_name,
                              const char *last_name,
                              const char *dog_name);
void timepanel_send_competitor(void);
void timepanel_send_reset(void);
void timepanel_send_start(int64_t offset_ms);
void timepanel_send_stop(int64_t elapsed_ms);
void timepanel_send_fault(uint16_t faults);
void timepanel_send_refusal(uint16_t refusals);
void timepanel_send_fault_increment(void);
void timepanel_send_refusal_increment(void);
void timepanel_send_dis(void);
void timepanel_send_parcours_timer(uint32_t duration_ms);

#endif
