#include <sys/time.h>
#include "Timer.h"
#include "SevenSegment.h"
#include "esp_private/esp_clk.h"
typedef struct timeval timeval_t;

#define TIME_US(t) ((int64_t)t.tv_sec * 1000000L + (int64_t)t.tv_usec)

void Sensor_Interrupt_Task(void *params);
void Sensor_Status_Task(void *params);
void sendSensorStatus(int triggeredPin, int pinToCheck);
void init_Sensor_Pins();