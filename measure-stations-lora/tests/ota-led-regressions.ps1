# Compile the production OTA callback and animation with simulated FreeRTOS/LED I/O.
# Run: powershell -ExecutionPolicy Bypass -File tests/ota-led-regressions.ps1
# Or use an existing Linux compiler: ./tests/ota-led-regressions.ps1 -WslDistribution Debian
param([string]$WslDistribution = '')

$ErrorActionPreference = 'Stop'
$projectRoot = (Resolve-Path (Join-Path $PSScriptRoot '..')).Path
$outputDir = Join-Path $projectRoot 'build/ota-led-regressions'
New-Item -ItemType Directory -Force -Path $outputDir | Out-Null
if (-not $WslDistribution) {
    $clang = (Get-Command clang.exe -ErrorAction SilentlyContinue).Source
    if (-not $clang) { $clang = 'C:/Program Files/LLVM/bin/clang.exe' }
    if (-not (Test-Path -LiteralPath $clang)) { throw 'Put host LLVM/Clang on PATH, or use -WslDistribution' }
}

function Read-Function([string]$source, [string]$name) {
    $code = [regex]::Match($source, "(?ms)^(?:static )?[^\r\n]+\b$name\([^\r\n]*\)\r?\n\{.*?^\}").Value
    if (-not $code) { throw "Production function $name not found" }
    return $code
}

$led = Get-Content -LiteralPath (Join-Path $projectRoot 'main/src/LED.c') -Raw
$main = Get-Content -LiteralPath (Join-Path $projectRoot 'main/src/main.c') -Raw
$otaHeader = Get-Content -LiteralPath (Join-Path $projectRoot '../components/dd-ota/include/OTA.h') -Raw
$events = [regex]::Match($otaHeader, '(?s)typedef enum dogdog_ota_event.*?} dogdog_ota_event_t;').Value
if (-not $events) { throw 'OTA events not found' }
$buzzerHeader = Get-Content -LiteralPath (Join-Path $projectRoot 'main/include/Buzzer.h') -Raw
$buzzerDefines = [regex]::Matches($buzzerHeader, '(?m)^#define [^\r\n]+').Value -join "`n"

$mocks = @'
#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>
#define ESP_OK 0
#define ESP_ERR_NO_MEM 1
#define ESP_LOG_INFO 3
#define ESP_LOGE(...) ((void)0)
#define ESP_LOGI(...) ((void)0)
#define ESP_ERROR_CHECK_WITHOUT_ABORT(expr) do { if ((expr) != ESP_OK) ++reported_errors; } while (0)
#define pdPASS 1
#define pdTRUE 1
#define pdMS_TO_TICKS(ms) (ms)
#define portMAX_DELAY UINT32_MAX
typedef void *TaskHandle_t;
typedef void *SemaphoreHandle_t;
typedef void *QueueHandle_t;
typedef uint32_t TickType_t;
typedef int BaseType_t;
static const char *OTA_TAG = "OTA_LED";
static bool is_initialized;
static int number_of_leds = 1;
static TaskHandle_t ota_pulse_task_handle;
static SemaphoreHandle_t ota_pulse_stopped = (void *)2;
static QueueHandle_t buzzerQueue;
static unsigned frames, creates, deletes, waits, notifications, buzzer_calls, errors, reported_errors;
static unsigned last_r, last_g, last_b, min_g, max_g, auto_stop_after;
static bool stop_requested, stopped, create_fails;
static void (*scheduled_task)(void *);
void esp_log_level_set(const char *tag, int level) { (void)tag; (void)level; }
void set_all_leds(uint8_t r, uint8_t g, uint8_t b) {
    ++frames; last_r = r; last_g = g; last_b = b;
    if (g) {
        if (g < min_g) min_g = g;
        if (g > max_g) max_g = g;
        if (!(r < b && b < g)) ++errors;
    }
}
BaseType_t xTaskCreate(void (*task)(void *), const char *name, unsigned stack,
                      void *params, unsigned priority, TaskHandle_t *handle) {
    (void)name; (void)stack; (void)priority;
    if (params || *handle) ++errors;
    ++creates;
    if (create_fails) return 0;
    scheduled_task = task; *handle = (void *)1;
    return pdPASS;
}
uint32_t ulTaskNotifyTake(BaseType_t clear, TickType_t wait) {
    if (clear != pdTRUE || wait == 0) ++errors;
    ++waits;
    if (stop_requested || (auto_stop_after && waits >= auto_stop_after)) {
        stop_requested = false;
        return 1;
    }
    return 0;
}
void vTaskDelete(TaskHandle_t task) { if (task) ++errors; ++deletes; }
BaseType_t xSemaphoreGive(SemaphoreHandle_t semaphore) {
    if (semaphore != ota_pulse_stopped || stopped) ++errors;
    stopped = true; return 1;
}
BaseType_t xTaskNotifyGive(TaskHandle_t task) {
    if (task != (void *)1) ++errors;
    stop_requested = true; ++notifications; return 1;
}
BaseType_t xSemaphoreTake(SemaphoreHandle_t semaphore, TickType_t wait) {
    if (semaphore != ota_pulse_stopped || wait != portMAX_DELAY || !scheduled_task) ++errors;
    // Let the notified worker finish its last write before the caller resumes.
    if (!stopped) scheduled_task(NULL);
    if (!stopped) ++errors;
    stopped = false; stop_requested = false; return 1;
}
BaseType_t xQueueSend(QueueHandle_t queue, const void *item, TickType_t wait) {
    if (queue != (void *)3 || wait || *(const int *)item != Buzzer_INDICATE_OTA) ++errors;
    ++buzzer_calls; return 1;
}
'@

$checks = @'
int test_main(void) {
    // A station without the OTA network must not show the OTA indicator.
    measure_station_ota_status(DOGDOG_OTA_EVENT_CHECKING, NULL);
    if (creates || frames) return 1;
    start_ota_led_pulse();
    if (creates || frames) return 2;
    is_initialized = true;

    // XLR OTA-only boot: no buzzer queue, but an immediate visible LED frame.
    measure_station_ota_status(DOGDOG_OTA_EVENT_UPDATING, NULL);
    if (creates != 1 || !frames || last_r != 64 || last_g != 224 || last_b != 208) return 3;
    measure_station_ota_status(DOGDOG_OTA_EVENT_UPDATING, NULL);
    measure_station_ota_status(DOGDOG_OTA_EVENT_RESTARTING_FOR_UPDATE, NULL);
    if (creates != 1 || buzzer_calls) return 4;
    measure_station_ota_status(DOGDOG_OTA_EVENT_FAILED, NULL);
    if (ota_pulse_task_handle || last_r || last_g || last_b || deletes != 1) return 5;
    unsigned notifications_before = notifications;
    stop_ota_led_pulse();
    if (notifications != notifications_before) return 6;

    // Connection and version probing need feedback even when no update is available.
    buzzerQueue = (void *)3;
    measure_station_ota_status(DOGDOG_OTA_EVENT_WIFI_FOUND, NULL);
    if (creates != 2 || buzzer_calls != 1 || last_g != 224) return 7;
    measure_station_ota_status(DOGDOG_OTA_EVENT_CONNECTING, NULL);
    if (creates != 2) return 8;
    measure_station_ota_status(DOGDOG_OTA_EVENT_NO_UPDATE, NULL);
    if (ota_pulse_task_handle || last_r || last_g || last_b) return 9;
    measure_station_ota_status(DOGDOG_OTA_EVENT_CONNECTING, NULL);
    if (creates != 3 || last_g != 224) return 10;
    stop_ota_led_pulse();

    // Exercise the production fade for two cycles, then deliver a stop notification.
    min_g = 255; max_g = 0; waits = 0; auto_stop_after = 161;
    ota_led_pulse_task(NULL);
    if (waits != 161 || min_g >= 32 || max_g != 224 || last_r || last_g || last_b || !stopped) return 11;
    stopped = false; auto_stop_after = 0;

    // Task allocation failure must be reported without waiting on a nonexistent worker.
    create_fails = true;
    start_ota_led_pulse(); stop_ota_led_pulse();
    if (reported_errors != 1 || ota_pulse_task_handle) return 12;
    return errors ? 13 : 0;
}
'@

$functions = foreach ($name in @('ota_led_pulse_task', 'start_ota_led_pulse', 'stop_ota_led_pulse')) {
    Read-Function $led $name
}
$code = @($events, $buzzerDefines, $mocks, ($functions -join "`n"), (Read-Function $main 'measure_station_ota_status'), $checks) -join "`n"
$cPath = Join-Path $outputDir 'ota-led.c'
$exePath = Join-Path $outputDir 'ota-led.exe'
Set-Content -LiteralPath $cPath -Value $code -Encoding ASCII
if ($WslDistribution) {
    $windowsDir = $outputDir.Replace('\', '/')
    $linuxDir = & wsl.exe -d $WslDistribution --exec wslpath -u $windowsDir
    if ($LASTEXITCODE -ne 0) { throw 'Cannot resolve test output directory in WSL' }
    $linuxDir = $linuxDir.Trim()
    & wsl.exe -d $WslDistribution --exec gcc -Dtest_main=main -fno-builtin -Wall -Werror -Wno-unused-variable "$linuxDir/ota-led.c" -o "$linuxDir/ota-led"
    if ($LASTEXITCODE -ne 0) { throw 'OTA LED test compilation failed' }
    & wsl.exe -d $WslDistribution --exec "$linuxDir/ota-led"
} else {
    & $clang --target=x86_64-pc-windows-msvc -ffreestanding -fno-builtin -fno-stack-protector -fuse-ld=lld -nostdlib -Wall -Werror -Wno-unused-variable '-Wl,/entry:test_main,/subsystem:console,/nodefaultlib' $cPath -o $exePath
    if ($LASTEXITCODE -ne 0) { throw 'OTA LED test compilation failed' }
    & $exePath
}
if ($LASTEXITCODE -ne 0) { throw "OTA LED regression failed, assertion $LASTEXITCODE" }
Write-Output 'Passed: OTA activation, immediate XLR feedback, fade, cleanup, and task failure'
