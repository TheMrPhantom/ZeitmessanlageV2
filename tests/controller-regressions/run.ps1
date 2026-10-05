# Run from PowerShell on Windows with LLVM/Clang installed. No ESP-IDF or board is needed.
$ErrorActionPreference = 'Stop'
$repoRoot = (Resolve-Path (Join-Path $PSScriptRoot '../..')).Path
$outputDir = Join-Path $repoRoot 'dogdog-controller/build/controller-regressions'
New-Item -ItemType Directory -Force -Path $outputDir | Out-Null
$clang = (Get-Command clang.exe -ErrorAction SilentlyContinue).Source
if (-not $clang) { $clang = 'C:/Program Files/LLVM/bin/clang.exe' }
if (-not (Test-Path $clang)) { throw 'Install LLVM/Clang or put clang.exe on PATH' }
$network = Get-Content -Raw (Join-Path $repoRoot 'components/lora-network/src/LoraNetwork.c')
$controller = Get-Content -Raw (Join-Path $repoRoot 'dogdog-controller/main/src/Lora.c')
$display = Get-Content -Raw (Join-Path $repoRoot 'dogdog-controller/main/src/SevenSegment.c')
$header = Get-Content -Raw (Join-Path $repoRoot 'components/lora-network/include/LoraNetwork.h')
$displayHeader = Get-Content -Raw (Join-Path $repoRoot 'dogdog-controller/main/include/SevenSegment.h')
$types = [regex]::Match($header, '(?s)// Packet Header.*?(?=typedef struct timeval)').Value
$displayType = [regex]::Match($displayHeader, '(?s)typedef struct SevenSegmentDisplay.*?} SevenSegmentDisplay;').Value
$common = @'
#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>
#undef NULL
#define NULL 0
#define PRId64 "lld"
#define ESP_LOGE(...) ((void)0)
#define ESP_LOGW(...) ((void)0)
#define ESP_LOGI(...) ((void)0)
#define pcTaskGetName(task) "test"
#define pdTRUE 1
#define pdPASS 1
#define pdFAIL 0
#define pdMS_TO_TICKS(ms) (ms)
#define portMAX_DELAY UINT32_MAX
typedef int QueueHandle_t;
typedef int SemaphoreHandle_t;
typedef int TaskHandle_t;
typedef uint32_t TickType_t;
typedef int BaseType_t;
static int controller_id = 1, start_id = 2, stop_id = 3;
void *memcpy(void *dst, const void *src, size_t n) {
    unsigned char *d = dst; const unsigned char *s = src;
    for (size_t i = 0; i < n; ++i) d[i] = s[i];
    return dst;
}
void *memset(void *dst, int value, size_t n) {
    unsigned char *d = dst;
    for (size_t i = 0; i < n; ++i) d[i] = (unsigned char)value;
    return dst;
}
static union { uint64_t align; unsigned char bytes[512]; } blocks[32];
static bool allocated[32];
static int allocation_count, allocation_fail_at, free_count, heap_errors;
void *calloc(size_t n, size_t size) {
    ++allocation_count;
    if (allocation_fail_at == allocation_count || n * size > 512) return NULL;
    for (int i = 0; i < 32; ++i) if (!allocated[i]) {
        allocated[i] = true; memset(blocks[i].bytes, 0, n * size); return blocks[i].bytes;
    }
    return NULL;
}
void free(void *p) {
    if (!p) return;
    for (int i = 0; i < 32; ++i) if (p == blocks[i].bytes) {
        if (!allocated[i]) ++heap_errors;
        allocated[i] = false; ++free_count; return;
    }
    ++heap_errors;
}
static void reset_heap(void) {
    memset(allocated, 0, sizeof(allocated));
    allocation_count = allocation_fail_at = free_count = heap_errors = 0;
}
'@
function Read-Function([string]$source, [string]$name) {
    $code = [regex]::Match($source, "(?ms)^(?:static )?[^\r\n]+\b$name\([^\r\n]*\)\r?\n\{.*?^\}").Value
    if (-not $code) { throw "Production function $name not found" }
    return $code
}
function Run-Test([string]$name, [string]$code) {
    $cPath = Join-Path $outputDir "$name.c"
    $exePath = Join-Path $outputDir "$name.exe"
    Set-Content -LiteralPath $cPath -Value $code -Encoding ASCII
    & $clang -ffreestanding -fno-builtin -fno-stack-protector -fuse-ld=lld -nostdlib -Wall -Werror -Wno-unused-function -Wno-unused-variable '-Wl,/entry:test_main,/subsystem:console,/nodefaultlib' $cPath -o $exePath
    if ($LASTEXITCODE -ne 0) { throw "$name compilation failed" }
    & $exePath
    if ($LASTEXITCODE -ne 0) { throw "$name failed, assertion $LASTEXITCODE" }
    Write-Output "Passed: $name"
}

$packetCode = foreach ($name in @('create_dogdog_packet_from_bytes', 'has_payload_length', 'create_time_sync_information', 'create_trigger_information', 'create_final_time_information', 'create_sensor_state_information', 'create_ack_information', 'populate_sensor_status')) {
    Read-Function $network $name
}
$queueMock = @'
static QueueHandle_t sevenSegmentQueue = 1;
static unsigned queue_calls;
static bool queue_accepts;
static SevenSegmentDisplay queued_display;
BaseType_t xQueueSend(QueueHandle_t queue, const void *p, TickType_t wait) {
    if (queue != sevenSegmentQueue || wait != 0) return 0;
    ++queue_calls;
    if (!queue_accepts) return 0;
    queued_display = *(const SevenSegmentDisplay *)p;
    return 1;
}
#define SEVEN_SEGMENT_SENSOR_STATUS 5
'@
$packetTests = @'
int test_main(void) {
    // Keep a backing array larger than the claimed frame; truncated lengths must be rejected.
    unsigned char frame[256] = {0};
    uint32_t magic = LORA_MAGIC;
    memcpy(frame + 1, &magic, 4); // Deliberately unaligned receive buffer.
    unsigned char *data = frame + 1;
    data[4] = LORA_PROTOCOL_VERSION; data[5] = 2; data[7] = LORA_ACK;
    data[9] = 2; data[10] = 1; data[11] = 7;
    if (create_dogdog_packet_from_bytes(NULL, 12)) return 1;
    for (unsigned n = 0; n < 12; ++n) {
        if (create_dogdog_packet_from_bytes(data, n)) return 2;
    }
    if (allocation_count != 0) return 3;
    DogDogPacket *packet = create_dogdog_packet_from_bytes(data, 12);
    if (!packet) return 4;
    PacketTypeAck *ack = create_ack_information(packet);
    if (!ack || ack->station_id != 1 || ack->packet_id != 7) return 5;
    free(ack); free(packet->payload); free(packet);
    data[8] = 0xff; data[9] = 0xff;
    if (create_dogdog_packet_from_bytes(data, 12)) return 6;
    data[8] = 0; data[9] = 2;
    data[0] ^= 1;
    if (create_dogdog_packet_from_bytes(data, 12)) return 7;
    data[0] ^= 1;
    reset_heap(); allocation_fail_at = 1;
    if (create_dogdog_packet_from_bytes(data, 12)) return 8;
    reset_heap(); allocation_fail_at = 2;
    if (create_dogdog_packet_from_bytes(data, 12) || free_count != 1) return 9;

    reset_heap();
    unsigned char payload[40] = {0};
    DogDogPacket input = {.payload = payload + 1};
    int64_t timestamp = INT64_C(1750000000123456);
    memcpy(input.payload, &timestamp, 8);
    for (unsigned n = 0; n < 8; ++n) {
        input.payload_length = n;
        if (create_time_sync_information(&input) || create_final_time_information(&input)) return 10;
    }
    input.payload_length = 8;
    PacketTypeTimeSync *sync = create_time_sync_information(&input);
    PacketTypeFinalTime *final = create_final_time_information(&input);
    if (!sync || !final || sync->timestamp != timestamp || final->timestamp != timestamp) return 11;
    free(sync); free(final);
    input.payload_length = 1;
    if (create_ack_information(&input)) return 12;
    for (unsigned n = 0; n < 8 + sizeof(PacketTypeSensorState); ++n) {
        input.payload_length = n;
        if (create_trigger_information(&input)) return 13;
    }
    input.payload_length = 8 + sizeof(PacketTypeSensorState);
    PacketTypeTrigger *trigger = create_trigger_information(&input);
    if (!trigger || trigger->timestamp != timestamp) return 14;
    free(trigger);
    input.payload_length = 8;
    if (create_sensor_state_information(&input)) return 15;
    input.payload_length = 9; input.payload[0] = 64;
    uint64_t states = UINT64_C(1) << 63;
    memcpy(input.payload + 1, &states, 8);
    PacketTypeSensorState *state = create_sensor_state_information(&input);
    if (!state || state->sensor_states != states) return 16;
    SensorStatus sensor;
    if (!populate_sensor_status(&sensor, state, 2, true) || !sensor.status[63] || sensor.status[0]) return 17;
    free(sensor.status);
    state->num_sensors = 65;
    if (populate_sensor_status(&sensor, state, 2, true) || sensor.status) return 18;
    state->num_sensors = 0;
    if (!populate_sensor_status(&sensor, state, 2, false) || sensor.status || sensor.num_sensors) return 19;
    free(state);

    reset_heap();
    PacketTypeSensorState small = {.num_sensors = 2, .sensor_states = 1};
    allocation_fail_at = 1;
    queue_sensor_status(&small, 2, false);
    if (queue_calls || heap_errors) return 20;
    reset_heap();
    for (unsigned i = 0; i < 1000; ++i) queue_sensor_status(&small, 2, false);
    if (free_count != 1000 || heap_errors) return 21;
    queue_accepts = true;
    queue_sensor_status(&small, 3, true);
    if (!queued_display.sensorStatus.status || queued_display.sensorStatus.sensor != SENSOR_STOP) return 22;
    free(queued_display.sensorStatus.status);
    return heap_errors ? 23 : 0;
}
'@
Run-Test 'packet-bounds-and-sensor-ownership' (@($common, $types, $displayType, $queueMock, ($packetCode -join "`n"), (Read-Function $controller 'queue_sensor_status'), $packetTests) -join "`n")

$pending = [regex]::Match($network, '(?s)#define MAX_PENDING_ACKS.*?(?=extern int controller_id)').Value
$ackMock = @'
static QueueHandle_t loraSendQueue = 2;
static int lock_depth, notify_errors, notify_count, notified_value, deleted, resent, notify_mode;
BaseType_t xSemaphoreTake(SemaphoreHandle_t mutex, TickType_t wait) {
    (void)mutex; (void)wait; ++lock_depth; return 1;
}
BaseType_t xSemaphoreGive(SemaphoreHandle_t mutex) { (void)mutex; --lock_depth; return 1; }
BaseType_t xTaskNotifyGive(TaskHandle_t task) {
    if (lock_depth != 1 || deleted || task != 9) ++notify_errors;
    ++notify_count; ++notified_value; return 1;
}
TaskHandle_t xTaskGetCurrentTaskHandle(void) { return 9; }
void vTaskDelete(void *task) { (void)task; deleted = 1; }
BaseType_t xQueueSend(QueueHandle_t queue, const void *packet, TickType_t wait) {
    (void)packet; (void)wait;
    if (queue == loraSendQueue) ++resent;
    return 1;
}
bool dispatch_pending_ack(uint8_t station, uint8_t packet);
uint32_t ulTaskNotifyTake(BaseType_t clear, TickType_t timeout) {
    (void)clear;
    if (timeout && notify_mode) {
        dispatch_pending_ack(2, 7);
        if (notify_mode == 2) return 0; // ACK arrived just after the timeout was decided.
    }
    unsigned result = notified_value; notified_value = 0; return result;
}
'@
$ackCode = foreach ($name in @('register_pending_ack', 'unregister_pending_ack', 'dispatch_pending_ack', 'ResendTask')) { Read-Function $network $name }
$ackTests = @'
int test_main(void) {
    pending_ack_mutex = 1;
    for (int mode = 0; mode < 3; ++mode) {
        reset_heap(); memset(pending_acks, 0, sizeof(pending_acks));
        lock_depth = notify_errors = notify_count = notified_value = deleted = resent = 0;
        notify_mode = mode;
        DogDogPacket *packet = calloc(1, sizeof(*packet));
        packet->payload = calloc(1, 2); packet->station_id = 2; packet->packet_id = 7;
        ResendTask(packet);
        if (!deleted || lock_depth || notify_errors) return 1;
        if (resent != (mode == 0 ? 1 : 0)) return 2;
        if (free_count != (mode == 0 ? 0 : 2) || heap_errors) return 3;
        // Once the retry task exits, subsequent ACKs must never notify its stale handle.
        if (dispatch_pending_ack(2, 7)) return 4;
        if (mode == 0) { free(packet->payload); free(packet); }
    }
    memset(pending_acks, 0, sizeof(pending_acks)); deleted = 0;
    for (unsigned i = 0; i < MAX_PENDING_ACKS; ++i) {
        if (!register_pending_ack(2, i, 9)) return 5;
    }
    if (register_pending_ack(2, 99, 9)) return 6;
    if (!dispatch_pending_ack(2, 0) || notify_errors) return 7;
    if (!register_pending_ack(2, 99, 9)) return 8;
    return 0;
}
'@
# Make the forward declaration match the actual private linkage.
$ackMock = $ackMock.Replace('bool dispatch_pending_ack(', 'static bool dispatch_pending_ack(')
Run-Test 'ack-timeout-and-task-lifetime' (@($common, $types, $pending, $ackMock, ($ackCode -join "`n"), $ackTests) -join "`n")

$timeCode = foreach ($name in @('display_time_ms', 'displayed_time_text_to_ms')) { Read-Function $display $name }
$timeTests = @'
int test_main(void) {
    if (display_time_ms(-1) != 0 || display_time_ms(INT64_MIN) != 0) return 1;
    if (display_time_ms(12349) != 12340) return 2;
    if (display_time_ms(INT64_C(3000000123)) != INT64_C(3000000120)) return 3;
    if (displayed_time_text_to_ms("3000000.12") != INT64_C(3000000120)) return 4;
    if (displayed_time_text_to_ms("12,34") != 12340 || displayed_time_text_to_ms("12.3") != 12300) return 5;
    if (displayed_time_text_to_ms("999999999999999999999999999999") != INT64_MAX) return 6;
    if (displayed_time_text_to_ms("9223372036854775.99") != INT64_MAX) return 7;
    SevenSegmentDisplay display = {.time = INT64_C(3000000123)};
    if (display.time != INT64_C(3000000123)) return 8;
    return 0;
}
'@
Run-Test '64-bit-display-time' (@($common, $types, $displayType, ($timeCode -join "`n"), $timeTests) -join "`n")

$timerSource = Get-Content -Raw (Join-Path $repoRoot 'dogdog-controller/main/src/Timer.c')
$timerHeader = Get-Content -Raw (Join-Path $repoRoot 'dogdog-controller/main/include/Timer.h')
$timerType = [regex]::Match($timerHeader, '(?s)typedef struct TimerTrigger.*?} TimerTrigger;').Value
$timerDefines = [regex]::Matches($timerHeader + $displayHeader, '(?m)^#define (?:TIMER_|SEVEN_SEGMENT_)[^\r\n]+') | ForEach-Object { $_.Value }
$timerCode = $timerSource.Substring($timerSource.IndexOf('extern QueueHandle_t sevenSegmentQueue;'))
# The production task exits once the simulated event is consumed; its body is unchanged.
$timerCode = $timerCode.Replace('while (true)', 'while (mock_event_count > 0)')
$timerMock = @'
typedef void *QueueSetHandle_t;
typedef struct { int state; int pinNumber; } glow_state_t;
typedef struct { int64_t tv_sec; int64_t tv_usec; } timeval_t;
#define TIME_US(t) ((t).tv_sec * INT64_C(1000000) + (t).tv_usec)
#define IS_THS_MODE 1
#define IS_SIMPLE_AGILITY_MODE 0
#define BUZZER_TRIGGER 1
#define BUTTON_GLOW_GPIO_TYPE_RESET 1
static QueueHandle_t sevenSegmentQueue = (void *)1, resetQueue = (void *)2;
static QueueHandle_t triggerQueue = (void *)3, timeQueue = (void *)4;
static QueueHandle_t buzzerQueue = (void *)5, buttonQueue = (void *)6;
static QueueSetHandle_t triggerAndResetQueue = (void *)7;
static char *pc_programm = "ths";
static bool isDis, sensors_active;
static int mock_event_count, mock_command, mock_history_count, mock_temp_count;
static QueueHandle_t mock_selected;
static TimerTrigger mock_trigger;
static int64_t mock_wall_us, mock_monotonic_us, mock_display_ms, mock_time_value;
int gettimeofday(timeval_t *time, void *tz) {
    (void)tz; time->tv_sec = mock_wall_us / 1000000; time->tv_usec = mock_wall_us % 1000000; return 0;
}
int64_t esp_timer_get_time(void) { return mock_monotonic_us; }
QueueHandle_t xQueueSelectFromSet(QueueSetHandle_t set, TickType_t wait) {
    (void)set; (void)wait; --mock_event_count; return mock_selected;
}
BaseType_t xQueueReceive(QueueHandle_t queue, void *item, TickType_t wait) {
    (void)wait;
    if (queue == triggerQueue) *(TimerTrigger *)item = mock_trigger;
    if (queue == resetQueue) *(int *)item = mock_command;
    return 1;
}
BaseType_t xQueueSend(QueueHandle_t queue, const void *item, TickType_t wait) {
    (void)wait;
    if (queue == timeQueue) memcpy(&mock_time_value, item, sizeof(mock_time_value));
    if (queue == sevenSegmentQueue) {
        const SevenSegmentDisplay *display = item;
        if (display->type == SEVEN_SEGMENT_SET_TIME) mock_display_ms = display->time;
        if (display->type == SEVEN_SEGMENT_TEMP_TIME) { ++mock_temp_count; mock_display_ms = display->time; }
        if (display->type == SEVEN_SEGMENT_STORE_TO_HISTORY) { ++mock_history_count; mock_display_ms = display->time; }
    }
    return 1;
}
void horn_timer_broadcast_elapsed_us(int64_t time) { (void)time; }
void timepanel_send_start(int64_t time) { (void)time; }
void horn_timer_broadcast_reset(void) {}
void timepanel_send_reset(void) {}
void timepanel_send_dis(void) {}
void increaseKey(const char *key) { (void)key; }
int printf(const char *format, ...) { (void)format; return 0; }
int snprintf(char *buf, size_t len, const char *format, ...) { (void)buf; (void)len; (void)format; return 0; }
'@
$timerTests = @'
static void event(QueueHandle_t queue) {
    mock_event_count = 1; mock_selected = queue; Timer_Task(NULL);
}
int test_main(void) {
    int64_t epoch = INT64_C(1750000000000000);
    mock_wall_us = epoch; mock_monotonic_us = 10000000;
    mock_command = TIMER_RESTART_LAST_TRIGGER;
    event(resetQueue);
    if (timerIsRunning) return 1;
    mock_trigger = (TimerTrigger){.is_start = true, .timestamp = epoch - 123456};
    event(triggerQueue);
    if (!timerIsRunning || mock_display_ms != 123 || mock_time_value != -1) return 2;
    // Wall-clock jumps in both directions must leave the live duration intact.
    mock_wall_us = 0; mock_monotonic_us = 10500000; event(NULL);
    if (mock_display_ms != 623) return 3;
    mock_wall_us = epoch * 2; mock_monotonic_us = 11000000; event(NULL);
    if (mock_display_ms != 1123) return 4;
    mock_trigger = (TimerTrigger){.timestamp = mock_wall_us};
    event(triggerQueue);
    if (!timerIsRunning || mock_temp_count) return 11;
    // An out-of-order stop timestamp must not terminate the ongoing run.
    mock_wall_us = epoch + 3000000;
    mock_trigger = (TimerTrigger){.timestamp = epoch - 2000000};
    event(triggerQueue);
    if (!timerIsRunning || mock_temp_count) return 5;
    mock_trigger.timestamp = epoch + 2500000; event(triggerQueue);
    if (timerIsRunning || mock_temp_count != 1 || mock_display_ms != 2623) return 6;
    mock_trigger.is_final_time = true; mock_trigger.timestamp = epoch + 2600000;
    event(triggerQueue);
    if (mock_history_count != 1 || mock_display_ms != 2723) return 7;
    mock_command = TIMER_RESET; event(resetQueue);
    if (timerTime || timerIsRunning || mock_display_ms || mock_time_value != -2) return 8;
    // A delayed final-time response after reset cannot fabricate a run from timestamp zero.
    event(triggerQueue);
    if (mock_history_count != 1 || timerIsRunning || mock_display_ms) return 9;
    mock_command = TIMER_RESTART_LAST_TRIGGER; event(resetQueue);
    if (!timerIsRunning || running_elapsed_us() != 400000 || mock_time_value != -1) return 10;
    return 0;
}
'@
$timerCommon = $common.Replace('typedef int QueueHandle_t;', 'typedef void *QueueHandle_t;')
Run-Test 'timer-clock-jumps-and-stale-triggers' (@($timerCommon, $types, $displayType, $timerType, ($timerDefines -join "`n"), $timerMock, $timerCode, $timerTests) -join "`n")

$initMock = @'
typedef struct { int64_t tv_sec; int64_t tv_usec; } timeval_t;
#define TIME_US(t) ((t).tv_sec * INT64_C(1000000) + (t).tv_usec)
#define ESP_OK 0
#define ESP_ERR_NO_MEM 1
#define ESP_ERROR_CHECK(expr) do { if ((expr) != ESP_OK) ++init_errors; } while (0)
static QueueHandle_t loraSendQueue, localReceiveTimestampQueue, loraInterruptQueue, ackQueue;
static int mock_interrupts = 1, init_errors, created_queues, delivered_timestamps;
static int64_t delivered_time;
void LoraInterruptTask(void *params);
void AckDispatchTask(void *params) { (void)params; }
QueueHandle_t xQueueCreate(unsigned count, size_t size) { (void)count; (void)size; return ++created_queues; }
SemaphoreHandle_t xSemaphoreCreateMutex(void) { return 1; }
BaseType_t xQueueReceive(QueueHandle_t queue, void *item, TickType_t timeout) {
    (void)timeout;
    if (queue != loraInterruptQueue || !mock_interrupts) return 0;
    --mock_interrupts; *(int *)item = 0; return 1;
}
BaseType_t xQueueSend(QueueHandle_t queue, const void *item, TickType_t timeout) {
    (void)timeout;
    if (!queue || queue != localReceiveTimestampQueue) { ++init_errors; return 0; }
    ++delivered_timestamps; memcpy(&delivered_time, item, sizeof(delivered_time)); return 1;
}
BaseType_t xTaskCreate(void (*function)(void *), const char *name, uint32_t stack, void *params, unsigned priority, TaskHandle_t *handle) {
    (void)name; (void)stack; (void)priority; (void)handle;
    // Simulate the high-priority interrupt task preempting initialization immediately.
    if (function == LoraInterruptTask) function(params);
    return 1;
}
int gettimeofday(timeval_t *time, void *tz) { (void)tz; time->tv_sec = 1; time->tv_usec = 0; return 0; }
void LoRaInit(void) {}
int16_t LoRaBegin(uint32_t hz, int8_t power, float voltage, bool ldo) {
    (void)hz; (void)power; (void)voltage; (void)ldo; return 0;
}
void LoRaConfig(uint8_t sf, uint8_t bw, uint8_t rate, uint16_t preamble, uint8_t len, bool crc, bool invert) {
    (void)sf; (void)bw; (void)rate; (void)preamble; (void)len; (void)crc; (void)invert;
}
void vTaskDelay(TickType_t delay) { (void)delay; }
int _fltused = 0;
'@
$initCode = (Read-Function $network 'LoraInterruptTask').Replace('while (true)', 'while (mock_interrupts > 0)')
$initTests = @'
int test_main(void) {
    init_lora();
    if (init_errors || delivered_timestamps != 1 || delivered_time != 1000000) return 1;
    if (!pending_ack_mutex || !localReceiveTimestampQueue) return 2;
    return 0;
}
'@
Run-Test 'receive-interrupt-during-startup' (@($common, $types, $pending, $initMock, $initCode, (Read-Function $network 'init_lora'), $initTests) -join "`n")
