#ifndef __BUTTON_H
#define __BUTTON_H
#include <sys/time.h>
typedef struct timeval timeval_t;

#define TIME_US(t) ((int64_t)t.tv_sec * 1000000L + (int64_t)t.tv_usec)

typedef struct
{
    // What pin to change
    int pinNumber;
    // Is button glowing
    int state;
} glow_state_t;

#define BUTTON_GLOW_TYPE_FAULT 37
#define BUTTON_GLOW_TYPE_REFUSAL 36
#define BUTTON_GLOW_TYPE_DIS 39
#define BUTTON_GLOW_TYPE_RESET 38
#define BUTTON_GLOW_TYPE_ACTIVATE 35

void Button_Task(void *params);
void init_glow_pins();
#endif