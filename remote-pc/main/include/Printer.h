#ifndef __PRINTER_H
#define __PRINTER_H

#include "driver/gpio.h"

#define UART_NUM UART_NUM_1   // Use UART1 (not UART0)
#define TXD_PIN (GPIO_NUM_17) // Change this to any GPIO
#define RXD_PIN (GPIO_NUM_18) // Change this to any GPIO

void uart_init();
void send_to_printer(const char *data, size_t len);
void print_text(const char *text);
void setup_large_bold_text();
void feed_paper();
void init_printer();

#endif // __PRINTER_H