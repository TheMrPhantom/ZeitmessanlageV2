$ErrorActionPreference = 'Stop'
# Reuse the freestanding test harness and run the earlier controller regressions.
. (Join-Path $PSScriptRoot '../controller-regressions/run.ps1')
$outputDir = Join-Path $repoRoot 'dogdog-controller/build/firmware-crash-regressions'
New-Item -ItemType Directory -Force -Path $outputDir | Out-Null
$stationLora = Get-Content -Raw (Join-Path $repoRoot 'measure-stations-lora/main/src/Lora.c')
$stationSensor = Get-Content -Raw (Join-Path $repoRoot 'measure-stations-lora/main/src/Sensor.c')
$controllerSensor = Get-Content -Raw (Join-Path $repoRoot 'dogdog-controller/main/src/Sensor.c')
$driver = Get-Content -Raw (Join-Path $repoRoot 'components/lora-network/src/ra01s.c')
$led = Get-Content -Raw (Join-Path $repoRoot 'measure-stations-lora/main/src/LED.c')

$queueTests = @'
static QueueHandle_t loraSendQueue;
static int accepts, calls;
static DogDogPacket *queued;
BaseType_t xQueueSend(QueueHandle_t queue, const void *item, TickType_t wait) {
    if (!queue || wait) return 0;
    ++calls; if (!accepts) return 0;
    queued = *(DogDogPacket *const *)item; return 1;
}
'@
$queueChecks = @'
int test_main(void) {
    if (send_dogdog_packet(NULL) != pdFAIL || calls || free_count) return 1;
    for (int mode = 0; mode < 3; ++mode) {
        reset_heap(); calls = 0; loraSendQueue = mode ? 1 : 0; accepts = mode == 2;
        for (int i = 0; i < 10000; ++i) {
            DogDogPacket *p = calloc(1, sizeof(*p));
            if (!p) return 2;
            p->payload = calloc(1, 8);
            if (!p->payload) return 3;
            if (send_dogdog_packet(p) != accepts) return 4;
            if (accepts) { free(queued->payload); free(queued); }
        }
        if (free_count != 20000 || heap_errors) return 5;
    }
    return 0;
}
'@
Run-Test 'outgoing-packet-ownership' (@($common, $types, $queueTests, (Read-Function $network 'send_dogdog_packet'), $queueChecks) -join "`n")

$syncMock = @'
typedef struct { int64_t tv_sec, tv_usec; } timeval_t;
#define TIME_US(t) ((t).tv_sec * INT64_C(1000000) + (t).tv_usec)
static int station_id = 2, critical_depth, errors, notifications, led_updates;
static int64_t time_offset_to_controller, timesync_current_time, timesync_received_time;
static int64_t timesync_processing_time, time_of_controller;
static TaskHandle_t sensorInterruptTaskHandle = 9;
static QueueHandle_t loraSendQueue = 1, ackQueue = 2;
static bool is_time_synced;
#define taskENTER_CRITICAL(lock) (++critical_depth)
#define taskEXIT_CRITICAL(lock) (--critical_depth)
int gettimeofday(timeval_t *time, void *tz) {
    (void)tz; if (critical_depth) ++errors; time->tv_sec = 1; time->tv_usec = 500; return 0;
}
void vTaskDelay(TickType_t ticks) { (void)ticks; }
BaseType_t xTaskNotifyGive(TaskHandle_t task) { if (task != 9) ++errors; ++notifications; return 1; }
void set_all_leds(uint8_t r, uint8_t g, uint8_t b) { (void)r; (void)g; (void)b; ++led_updates; }
BaseType_t xQueueSend(QueueHandle_t queue, const void *item, TickType_t ticks) {
    (void)queue; (void)item; (void)ticks; return 1;
}
int64_t get_last_release_timestamp(void) { return 0; }
DogDogPacket *create_dogdog_packet_from_ack_information(PacketTypeAck *ack) { (void)ack; return NULL; }
DogDogPacket *create_dogdog_packet_from_final_time_information(PacketTypeFinalTime *time) { (void)time; return NULL; }
'@
$syncChecks = @'
int test_main(void) {
    int64_t timestamp = INT64_C(1750000000000000);
    DogDogPacket packet = {.type = LORA_TIME_SYNC, .payload = (uint8_t *)&timestamp,
                          .payload_length = 8, .local_time_received = 1000000};
    for (int i = 0; i < 10000; ++i) HandleReceivedPacket(&packet);
    if (allocation_count != 10000 || free_count != 10000 || heap_errors) return 1;
    if (critical_depth || errors || notifications != 1 || led_updates != 1) return 2;
    if (time_offset_to_controller != timestamp - 1000000) return 3;
    allocation_fail_at = allocation_count + 1;
    HandleReceivedPacket(&packet);
    return free_count == 10000 && !critical_depth ? 0 : 4;
}
'@
$syncCode = foreach ($name in @('has_payload_length', 'create_time_sync_information', 'create_ack_information')) { Read-Function $network $name }
Run-Test 'station-time-sync-memory-and-critical-section' (@($common, $types, $syncMock, ($syncCode -join "`n"), (Read-Function $stationLora 'HandleReceivedPacket'), $syncChecks) -join "`n")

$txMock = @'
#define SX126X_IRQ_TX_DONE 1
#define SX126X_IRQ_TIMEOUT 2
#define SX126X_IRQ_ALL 0xffff
#define SX126X_CMD_SET_PACKET_PARAMS 0x8c
#define SX126x_TXMODE_SYNC 1
static uint8_t PacketParams[6];
static bool txActive, debugPrint;
static int txLost, mode, polls, delays, restarts, writes;
static TickType_t ticks;
TickType_t xTaskGetTickCount(void) { return ticks; }
void vTaskDelay(TickType_t n) { ticks += n; ++delays; }
void WriteCommand(uint8_t command, uint8_t *bytes, uint8_t count) { (void)command; (void)bytes; (void)count; }
void ClearIrqStatus(uint16_t mask) { (void)mask; }
void WriteBuffer(uint8_t *bytes, int16_t len) { (void)bytes; (void)len; ++writes; }
void SetTx(uint32_t timeout) { (void)timeout; }
void SetRx(uint32_t timeout) { (void)timeout; ++restarts; }
uint16_t GetIrqStatus(void) {
    ++polls;
    if (mode == 0 && polls > 3) return SX126X_IRQ_TX_DONE;
    if (mode == 1 && polls > 3) return SX126X_IRQ_TIMEOUT;
    return 0;
}
'@
$txChecks = @'
int test_main(void) {
    uint8_t bytes[255] = {0};
    if (LoRaSend(NULL, 10, 1) || LoRaSend(bytes, 0, 1) || LoRaSend(bytes, 256, 1) || writes) return 1;
    for (mode = 0; mode < 3; ++mode) {
        polls = delays = restarts = 0; ticks = UINT32_MAX - 100;
        bool sent = LoRaSend(bytes, sizeof(bytes), SX126x_TXMODE_SYNC);
        if (sent != (mode == 0) || txActive || restarts != 1 || !delays) return 2;
        if (mode == 2 && delays != 1000) return 3;
    }
    if (txLost != 2) return 4;
    mode = 0; polls = 0;
    return LoRaSend(bytes, 1, SX126x_TXMODE_SYNC) ? 0 : 5;
}
'@
Run-Test 'radio-transmit-yields-and-recovers' (@($common, $txMock, (Read-Function $driver 'LoRaSend'), $txChecks) -join "`n")

$ledMock = @'
#define ESP_OK 0
#define ESP_ERROR_CHECK_WITHOUT_ABORT(expr) ((void)(expr))
static SemaphoreHandle_t led_mutex = 1;
static int led_handle = 1, number_of_leds = 10;
static bool is_initialized;
static float brightness = 1;
static int depth, errors, pixels, refreshes;
BaseType_t xSemaphoreTake(SemaphoreHandle_t mutex, TickType_t wait) {
    if (mutex != 1 || wait != portMAX_DELAY || depth) ++errors; ++depth; return 1;
}
BaseType_t xSemaphoreGive(SemaphoreHandle_t mutex) { (void)mutex; if (depth != 1) ++errors; --depth; return 1; }
int led_strip_set_pixel(int handle, unsigned pixel, unsigned r, unsigned g, unsigned b) {
    (void)r; (void)g; (void)b;
    if (handle != 1 || pixel >= 10 || depth != 1) ++errors; ++pixels; return ESP_OK;
}
int led_strip_refresh(int handle) {
    if (handle != 1 || depth != 1) ++errors; ++refreshes;
    return refreshes % 2; // Inject transient failures: always release the mutex.
}
int _fltused = 0;
'@
$ledChecks = @'
int test_main(void) {
    set_led(0, 1, 2, 3); set_all_leds(1, 2, 3);
    if (pixels || refreshes) return 1;
    is_initialized = true; set_led(10, 1, 2, 3);
    if (pixels || refreshes || depth) return 2;
    for (int i = 0; i < 1000; ++i) { set_led(0, 1, 2, 3); set_all_leds(1, 2, 3); }
    if (errors || depth || pixels != 11000 || refreshes != 2000) return 3;
    return 0;
}
'@
Run-Test 'led-updates-serialize-and-release-after-error' (@($common, $ledMock, (Read-Function $led 'set_led'), (Read-Function $led 'set_all_leds'), $ledChecks) -join "`n")

# Interrupt registration fires an edge synchronously; both ISR queues must exist.
$sensorMock = @'
typedef struct { int64_t tv_sec, tv_usec; } timeval_t;
typedef struct { int pin, state; uint32_t triggered_at; } PinTrigger;
static const int sensorPins[] = {1, 2};
static timeval_t last_start_trigger_time, last_sensor_stop_time;
static QueueHandle_t sensorStatusQueue;
static int errors, registrations;
#define ESP_OK 0
#define ESP_ERR_NO_MEM 1
#define ESP_ERROR_CHECK(expr) do { if ((expr) != ESP_OK) ++errors; } while (0)
QueueHandle_t xQueueCreate(unsigned count, size_t size) {
    if (count != 1 || size != sizeof(int)) ++errors; return 1;
}
void init_Pins(void) { ++registrations; if (!sensorStatusQueue) ++errors; }
void init_Sensor_Pins(void) { init_Pins(); }
BaseType_t ulTaskNotifyTake(BaseType_t clear, TickType_t wait) { (void)clear; (void)wait; return 1; }
void Sensor_Status_Task(void *params) { (void)params; }
BaseType_t xTaskCreate(void (*task)(void *), const char *name, unsigned stack, void *params, unsigned priority, TaskHandle_t *handle) {
    (void)task; (void)name; (void)stack; (void)params; (void)priority; (void)handle;
    if (!sensorStatusQueue) ++errors; return pdPASS;
}
int getValue(const char *key) { (void)key; return 0; }
int gettimeofday(timeval_t *time, void *tz) { (void)tz; *time = (timeval_t){0}; return 0; }
void vTaskDelete(void *task) { (void)task; }
'@
$sensorChecks = @'
int test_main(void) {
    Sensor_Interrupt_Task(NULL);
    return errors || registrations != 1 ? 1 : 0;
}
'@
foreach ($source in @($stationSensor, $controllerSensor)) {
    $prefix = Read-Function $source 'Sensor_Interrupt_Task'
    $prefix = $prefix.Substring(0, $prefix.IndexOf('    while (true)')) + "}`n"
    $name = if ($source -eq $stationSensor) { 'station-sensor-interrupt-startup' } else { 'controller-sensor-interrupt-startup' }
    Run-Test $name (@($common, $sensorMock, $prefix, $sensorChecks) -join "`n")
}

$mainSource = Get-Content -Raw (Join-Path $repoRoot 'dogdog-controller/main/src/main.c')
$buttonSource = Get-Content -Raw (Join-Path $repoRoot 'dogdog-controller/main/src/ButtonInput.c')
$buttonHeader = Get-Content -Raw (Join-Path $repoRoot 'dogdog-controller/main/include/ButtonInput.h')
$buttonType = [regex]::Match($buttonHeader, '(?s)typedef struct sensor_interrupt_t.*?} sensor_interrupt_t;').Value
$mainQueues = [regex]::Match($mainSource, '(?s)    sensorInterruptQueue = xQueueCreate.*?(?=    increaseKey)').Value
if (-not $mainQueues) { throw 'Controller queue initialization not found' }
$buttonMock = @'
typedef int QueueSetHandle_t;
typedef struct { int pinNumber, state; } glow_state_t;
typedef struct { int station, signal; } StationConnectivityStatus;
typedef struct { bool is_start; int64_t timestamp; bool is_final_time; } TimerTrigger;
static QueueHandle_t sensorInterruptQueue, buttonInterruptQueue, buttonQueue, resetQueue, triggerQueue;
static QueueHandle_t networkFaultQueue, sevenSegmentQueue, timeQueue, sendQueue, buzzerQueue;
static QueueSetHandle_t triggerAndResetQueue;
static int queue_count, errors, level;
static size_t sizes[16];
static sensor_interrupt_t delivered;
#define IRAM_ATTR
#define ESP_OK 0
#define ESP_ERR_NO_MEM 1
#define ESP_FAIL 2
#define GPIO_INTR_NEGEDGE 2
#define GPIO_INTR_POSEDGE 1
#define ESP_ERROR_CHECK(expr) do { if ((expr) != ESP_OK) ++errors; } while (0)
QueueHandle_t xQueueCreate(unsigned count, size_t size) { (void)count; sizes[++queue_count] = size; return queue_count; }
QueueSetHandle_t xQueueCreateSet(unsigned count) { (void)count; return 99; }
BaseType_t xQueueAddToSet(QueueHandle_t queue, QueueSetHandle_t set) { return queue && set ? pdPASS : pdFAIL; }
int gpio_get_level(int pin) { (void)pin; return level; }
BaseType_t xQueueSendFromISR(QueueHandle_t queue, const void *item, void *wake) {
    (void)wake; if (queue != buttonInterruptQueue) { ++errors; return pdFAIL; }
    memcpy(&delivered, item, sizes[queue]); return pdTRUE;
}
'@
$buttonChecks = @'
int test_main(void) {
    initialize_queues();
    if (errors || !buttonQueue) return 1;
    for (int i = 0; i < 1000; ++i) {
        level = i & 1; delivered = (sensor_interrupt_t){0};
        gpio_interrupt_handler((void *)(intptr_t)47);
        if (delivered.pinNumber != 47 || delivered.edge != (level ? GPIO_INTR_POSEDGE : GPIO_INTR_NEGEDGE)) return 2;
    }
    return errors ? 3 : 0;
}
'@
# ESP32 pointers and int are both 32 bits; widen this cast for the 64-bit host.
$buttonIsr = (Read-Function $buttonSource 'gpio_interrupt_handler').Replace('(int)args', '(int)(intptr_t)args')
Run-Test 'controller-button-queue-keeps-both-edge-fields' (@($common, $types, $displayType, $buttonType, $buttonMock, "void initialize_queues(void) {`n$mainQueues}`n", $buttonIsr, $buttonChecks) -join "`n")
