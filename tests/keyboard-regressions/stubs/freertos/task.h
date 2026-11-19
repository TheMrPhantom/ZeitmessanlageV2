#ifndef TEST_TASK_H
#define TEST_TASK_H
#include "FreeRTOS.h"
typedef void *TaskHandle_t;
TickType_t xTaskGetTickCount(void);
void vTaskDelay(TickType_t ticks);
BaseType_t xTaskCreate(void (*task)(void *), const char *name, unsigned stack, void *parameter, UBaseType_t priority, TaskHandle_t *handle);
#endif
