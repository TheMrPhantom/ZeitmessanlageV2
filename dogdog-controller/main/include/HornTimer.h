#ifndef __HORN_TIMER_H
#define __HORN_TIMER_H

#include <stdint.h>
#include "esp_err.h"

esp_err_t init_horn_timer_broadcast(void);
void horn_timer_broadcast_elapsed_us(int64_t elapsed_us);
void horn_timer_broadcast_reset(void);

#endif
