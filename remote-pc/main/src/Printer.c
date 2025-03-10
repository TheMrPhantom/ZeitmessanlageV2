#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "driver/uart.h"
#include "esp_log.h"
#include <string.h>
#include "Printer.h"

static const char *TAG = "QR701_PRINTER";

void uart_init()
{
    const uart_config_t uart_config = {
        .baud_rate = 9600,
        .data_bits = UART_DATA_8_BITS,
        .parity = UART_PARITY_DISABLE,
        .stop_bits = UART_STOP_BITS_1,
        .flow_ctrl = UART_HW_FLOWCTRL_DISABLE};

    uart_driver_install(UART_NUM, 1024, 0, 0, NULL, 0);
    uart_param_config(UART_NUM, &uart_config);
    uart_set_pin(UART_NUM, TXD_PIN, RXD_PIN, UART_PIN_NO_CHANGE, UART_PIN_NO_CHANGE);
}

void send_to_printer(const char *data, size_t len)
{
    uart_write_bytes(UART_NUM, data, len);
}

void print_text(const char *text)
{
    // Fix: Set ASCII before every line to clear incorrect characters
    send_to_printer("\x1B\x74\x00", 3); // ASCII mode
    send_to_printer("\x1B\x52\x00", 3); // USA charset

    send_to_printer(text, strlen(text));
    send_to_printer("\r\n", 2); // Proper newline
}

void setup_large_bold_text()
{
    // Fix: Set ASCII mode at the start
    send_to_printer("\x1B\x74\x00", 3);
    send_to_printer("\x1B\x52\x00", 3);

    // Set max darkness
    send_to_printer("\x1D\x7C\x08", 3);

    // Enable bold
    send_to_printer("\x1B\x45\x01", 3);

    // Set double size (big font)
    send_to_printer("\x1D\x21\x11", 3); // 0x11 = Double width + Double height
}

void feed_paper()
{
    send_to_printer("\x1B\x4A\x50", 3); // Feed ~80 pixels
}

void init_printer()
{
    uart_init();
    setup_large_bold_text();
    ESP_LOGI(TAG, "Printer initialized");
}