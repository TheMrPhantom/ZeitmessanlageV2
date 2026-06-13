#include <sys/time.h>
#include "esp_private/esp_clk.h"
typedef struct timeval timeval_t;

#define TIME_US(t) ((int64_t)t.tv_sec * 1000000L + (int64_t)t.tv_usec)

void Sensor_Interrupt_Task(void *params);
void Sensor_Status_Task(void *params);
void init_Pins();
int get_num_sensors();

typedef struct PinTrigger
{
    int pin;
    int state;
    esp_cpu_cycle_count_t triggered_at;
} PinTrigger;