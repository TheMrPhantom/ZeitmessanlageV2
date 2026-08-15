#include "PcSerial.h"
#include "TimepanelClient.h"

#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include <stdio.h>
#include <string.h>

#define PC_SERIAL_LINE_MAX 192

static const char *TAG = "PcSerial";

static char *trim_field(char *value)
{
    while (*value == ' ' || *value == '\t')
    {
        value++;
    }

    char *end = value + strlen(value);
    while (end > value &&
           (end[-1] == ' ' || end[-1] == '\t' || end[-1] == '\r' || end[-1] == '\n'))
    {
        end--;
        *end = '\0';
    }

    return value;
}

static char *take_field(char **cursor)
{
    if (*cursor == NULL)
    {
        return "";
    }

    char *field = *cursor;
    char *separator = strchr(field, '|');

    if (separator)
    {
        *separator = '\0';
        *cursor = separator + 1;
    }
    else
    {
        *cursor = NULL;
    }

    return trim_field(field);
}

static void split_full_name(const char *full_name,
                            char *first_name,
                            size_t first_name_len,
                            char *last_name,
                            size_t last_name_len)
{
    snprintf(first_name, first_name_len, "%s", full_name);
    last_name[0] = '\0';

    char *last_space = strrchr(first_name, ' ');
    if (last_space == NULL)
    {
        return;
    }

    *last_space = '\0';
    snprintf(last_name, last_name_len, "%s", trim_field(last_space + 1));

    char *trimmed_first_name = trim_field(first_name);
    if (trimmed_first_name != first_name)
    {
        memmove(first_name, trimmed_first_name, strlen(trimmed_first_name) + 1);
    }
}

static void handle_competitor_line(char *line)
{
    char *cursor = line;
    char *command = take_field(&cursor);

    if (strcmp(command, "competitor") != 0)
    {
        ESP_LOGW(TAG, "Ignoring unknown serial command: %s", command);
        return;
    }

    char *first_name = take_field(&cursor);
    char *last_name = take_field(&cursor);
    char *dog_name = take_field(&cursor);

    if (dog_name[0] == '\0' && last_name[0] != '\0')
    {
        static char split_first_name[64];
        static char split_last_name[64];

        dog_name = last_name;
        split_full_name(first_name,
                        split_first_name,
                        sizeof(split_first_name),
                        split_last_name,
                        sizeof(split_last_name));
        first_name = split_first_name;
        last_name = split_last_name;
    }

    if (first_name[0] == '\0' && last_name[0] == '\0' && dog_name[0] == '\0')
    {
        ESP_LOGW(TAG, "Ignoring empty competitor serial command");
        return;
    }

    ESP_LOGI(TAG,
             "Received competitor from PC: first='%s' last='%s' dog='%s'",
             first_name,
             last_name,
             dog_name);
    timepanel_set_competitor(first_name, last_name, dog_name);
}

static void process_line(char *line)
{
    char *trimmed = trim_field(line);

    if (trimmed[0] == '\0')
    {
        return;
    }

    handle_competitor_line(trimmed);
}

void Pc_Serial_Task(void *params)
{
    (void)params;

    char line[PC_SERIAL_LINE_MAX] = {0};
    size_t line_len = 0;

    ESP_LOGI(TAG, "PC serial competitor receiver started");

    while (true)
    {
        int value = getchar();

        if (value == EOF)
        {
            vTaskDelay(pdMS_TO_TICKS(20));
            continue;
        }

        char c = (char)value;
        if (c == '\r' || c == '\n')
        {
            if (line_len > 0)
            {
                line[line_len] = '\0';
                process_line(line);
                line_len = 0;
            }
            continue;
        }

        if (line_len < sizeof(line) - 1)
        {
            line[line_len++] = c;
        }
        else
        {
            ESP_LOGW(TAG, "Serial command too long, dropping line");
            line_len = 0;
        }
    }
}
