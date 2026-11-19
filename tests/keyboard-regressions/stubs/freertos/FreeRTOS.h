#ifndef TEST_FREERTOS_H
#define TEST_FREERTOS_H
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#define pdTRUE 1
#define pdFALSE 0
#define pdPASS 1
#define pdFAIL 0
#define pdMS_TO_TICKS(ms) ((TickType_t)(ms))
#define portMAX_DELAY UINT32_MAX
typedef int BaseType_t;
typedef unsigned UBaseType_t;
typedef uint32_t TickType_t;
typedef int portMUX_TYPE;
#define portMUX_INITIALIZER_UNLOCKED 0
void test_enter_critical(void);
void test_exit_critical(void);
#define portENTER_CRITICAL(mux) test_enter_critical()
#define portEXIT_CRITICAL(mux) test_exit_critical()
#endif
