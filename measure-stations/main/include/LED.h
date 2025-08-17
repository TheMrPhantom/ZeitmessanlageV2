#ifndef __LED_H
#define __LED_H
#include "freertos/FreeRTOS.h"

/*Initialize the led library*/
void LED_Task(void *params);
void init_led(int num_leds);
void set_led(uint8_t led, uint8_t r, uint8_t g, uint8_t b);

#endif // __MAIN_H