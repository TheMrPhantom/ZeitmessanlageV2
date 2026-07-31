#ifndef __LED_H
#define __LED_H
#include <stdint.h>

/*Initialize the led library*/
void init_led(int num_leds);
void set_led(uint8_t led, uint8_t r, uint8_t g, uint8_t b);
void set_all_leds(uint8_t r, uint8_t g, uint8_t b);

#endif
