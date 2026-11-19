#ifndef TEST_ESP_ERR_H
#define TEST_ESP_ERR_H
typedef int esp_err_t;
#define ESP_OK 0
#define ESP_ERR_NO_MEM 0x101
void test_error_check(int error);
#define ESP_ERROR_CHECK(error) test_error_check(error)
#endif
