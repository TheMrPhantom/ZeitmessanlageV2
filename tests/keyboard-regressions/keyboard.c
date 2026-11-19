// Include the production source so these checks exercise every submitted report
// and real callback/state transition rather than a reimplemented sender.
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "string.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#include "tinyusb.h"
#include "Keyboard.h"

void *memcpy(void *destination, const void *source, size_t count) {
    uint8_t *d = destination; const uint8_t *s = source;
    for (size_t i = 0; i < count; ++i) d[i] = s[i];
    return destination;
}
void *memset(void *destination, int value, size_t count) {
    uint8_t *d = destination;
    for (size_t i = 0; i < count; ++i) d[i] = (uint8_t)value;
    return destination;
}
int memcmp(const void *left, const void *right, size_t count) {
    const uint8_t *a = left, *b = right;
    for (size_t i = 0; i < count; ++i) if (a[i] != b[i]) return (int)a[i] - b[i];
    return 0;
}
size_t strlen(const char *text) { size_t n = 0; while (text[n]) ++n; return n; }

struct TestSemaphore { bool mutex; unsigned count; };
static struct TestSemaphore semaphores[8];
static unsigned semaphore_count, lock_takes, lock_gives, lock_depth, critical_depth;
static TickType_t now, ready_after;
static bool mounted = true, suspended, endpoint_busy;
static bool background_recovery;
static int harness_error;
static unsigned submissions, attempts, rejections, completions;
static uint8_t submitted[64][9];
static TickType_t submitted_at[64];

enum Outcome { COMPLETE, IMMEDIATE_COMPLETE, FAIL, SILENT, SILENT_READY, DISCONNECT, WRONG_INSTANCE, WRONG_REPORT, WRONG_LENGTH };
struct Plan { enum Outcome outcome; unsigned delay; };
static struct Plan plans[64];
struct Event { bool used; enum Outcome outcome; TickType_t at; uint8_t report[9]; };
static struct Event events[64];

static void tick(void) {
    ++now;
    if (now > 100000) { harness_error = 1; mounted = false; }
    for (unsigned i = 0; i < 64; ++i) {
        if (!events[i].used || (int32_t)(now - events[i].at) < 0) continue;
        struct Event event = events[i];
        events[i].used = false;
        endpoint_busy = false;
        if (event.outcome == SILENT_READY) {
            // The stack exposes endpoint readiness before dispatching its callback.
            // Readiness alone must never permit overwriting that callback's buffer.
        } else if (event.outcome == DISCONNECT) {
            mounted = false;
            tud_umount_cb();
        } else if (event.outcome == FAIL) {
            tud_hid_report_failed_cb(0, HID_REPORT_TYPE_INPUT, event.report, 0);
        } else {
            ++completions;
            if (event.outcome == WRONG_REPORT) event.report[0] = HID_ITF_PROTOCOL_MOUSE;
            tud_hid_report_complete_cb(event.outcome == WRONG_INSTANCE ? 1 : 0,
                                      event.report,
                                      event.outcome == WRONG_LENGTH ? 1 : sizeof(event.report));
        }
    }
}
TickType_t xTaskGetTickCount(void) { return now; }
void vTaskDelay(TickType_t ticks) { for (TickType_t i = 0; i < (ticks ? ticks : 1); ++i) tick(); }
void test_enter_critical(void) { ++critical_depth; }
void test_exit_critical(void) { if (!critical_depth) harness_error = 2; else --critical_depth; }
SemaphoreHandle_t xSemaphoreCreateMutex(void) {
    if (semaphore_count == 8) return NULL;
    struct TestSemaphore *s = &semaphores[semaphore_count++];
    s->mutex = true; s->count = 1; return s;
}
SemaphoreHandle_t xSemaphoreCreateBinary(void) {
    if (semaphore_count == 8) return NULL;
    struct TestSemaphore *s = &semaphores[semaphore_count++];
    s->mutex = false; s->count = 0; return s;
}
BaseType_t xSemaphoreTake(SemaphoreHandle_t semaphore, TickType_t wait) {
    if (!semaphore) { harness_error = 3; return pdFALSE; }
    TickType_t started = now;
    while (!semaphore->count && (TickType_t)(now - started) < wait && wait != portMAX_DELAY) tick();
    if (!semaphore->count) return pdFALSE;
    --semaphore->count;
    if (semaphore->mutex) { ++lock_takes; ++lock_depth; }
    return pdTRUE;
}
BaseType_t xSemaphoreGive(SemaphoreHandle_t semaphore) {
    if (!semaphore) { harness_error = 4; return pdFALSE; }
    if (semaphore->mutex) { ++lock_gives; if (!lock_depth) harness_error = 5; else --lock_depth; }
    semaphore->count = 1; return pdTRUE;
}
BaseType_t xTaskCreate(void (*task)(void *), const char *name, unsigned stack,
                       void *parameter, UBaseType_t priority, TaskHandle_t *handle) {
    (void)task; (void)name; (void)stack; (void)parameter; (void)priority;
    if (handle) *handle = (void *)1;
    return pdPASS;
}
void test_error_check(int error) { if (error) harness_error = 6; }
esp_err_t tinyusb_driver_install(const tinyusb_config_t *config) { (void)config; return ESP_OK; }
bool tud_mounted(void) { return mounted; }
bool tud_suspended(void) { return suspended; }
bool tud_hid_ready(void) { return mounted && !suspended && !endpoint_busy && (int32_t)(now - ready_after) >= 0; }
bool tud_hid_keyboard_report(uint8_t report_id, uint8_t modifier, const uint8_t keycode[6]) {
    ++attempts;
    if (!lock_depth || critical_depth) harness_error = 7;
    keyboard_delivery_status_t status = keyboard_get_delivery_status();
    if (status != KEYBOARD_DELIVERY_PENDING &&
        !(background_recovery && keycode == NULL && status == KEYBOARD_DELIVERY_UNCONFIRMED)) harness_error = 10;
    if (rejections) { --rejections; return false; }
    if (!tud_hid_ready() || submissions == 64) { harness_error = 8; return false; }
    unsigned n = submissions++;
    submitted[n][0] = report_id;
    submitted[n][1] = modifier;
    submitted[n][2] = 0;
    if (keycode) memcpy(submitted[n] + 3, keycode, 6);
    else memset(submitted[n] + 3, 0, 6);
    submitted_at[n] = now;
    endpoint_busy = true;
    if (plans[n].outcome == IMMEDIATE_COMPLETE) {
        endpoint_busy = false; ++completions;
        tud_hid_report_complete_cb(0, submitted[n], sizeof(submitted[n]));
        return true;
    }
    if (plans[n].outcome != SILENT) {
        events[n].used = true;
        events[n].outcome = plans[n].outcome;
        events[n].at = now + (plans[n].delay ? plans[n].delay : 10);
        memcpy(events[n].report, submitted[n], 9);
    }
    return true;
}

#include "../../dogdog-controller/main/src/Keyboard.c"

static void reset_host(void) {
    now = 0; ready_after = 0; mounted = true; suspended = false; endpoint_busy = false;
    background_recovery = false;
    submissions = attempts = rejections = completions = 0;
    lock_takes = lock_gives = lock_depth = critical_depth = 0;
    memset(plans, 0, sizeof(plans)); memset(events, 0, sizeof(events));
    memset(submitted, 0, sizeof(submitted)); memset(submitted_at, 0, sizeof(submitted_at));
    tud_umount_cb(); tud_mount_cb();
    // Finish the all-keys-up synchronization required for a newly mounted host.
    // Keep its reports separate from the operation under test.
    if (sendKey(HID_KEY_ESCAPE) != pdTRUE) harness_error = 9;
    now = 0;
    submissions = attempts = completions = 0;
    lock_takes = lock_gives = 0;
    memset(events, 0, sizeof(events));
    memset(submitted, 0, sizeof(submitted)); memset(submitted_at, 0, sizeof(submitted_at));
}
static unsigned down_count(void) {
    unsigned result = 0;
    for (unsigned i = 0; i < submissions; ++i) if (submitted[i][3]) ++result;
    return result;
}

int test_main(void) {
    init_keyboard();
    if (harness_error) return 1;

    reset_host();
    plans[0].delay = 37; plans[1].delay = 49;
    if (sendKey(HID_KEY_D) != pdTRUE) return 10;
    if (submissions != 2 || completions != 2 || down_count() != 1) return 11;
    if (submitted[0][3] != HID_KEY_D || submitted[1][3] != 0 || submitted_at[1] < 37 || now < 86) return 12;
    if (keyboard_get_delivery_status() != KEYBOARD_DELIVERY_USB_CONFIRMED) return 13;
    if (lock_takes != 1 || lock_gives != 1 || lock_depth || harness_error) return 14;

    reset_host(); plans[0].delay = 150;
    if (sendKey(HID_KEY_D) != pdTRUE || submissions != 2 || down_count() != 1 || submitted_at[1] < 150) return 15;
    if (harness_error) return 16;
    reset_host(); plans[0].outcome = plans[1].outcome = IMMEDIATE_COMPLETE;
    if (sendKey(HID_KEY_D) != pdTRUE || submissions != 2 || completions != 2 || harness_error) return 17;

    reset_host(); ready_after = 20; rejections = 1;
    if (sendKey(HID_KEY_D) != pdTRUE || down_count() != 1 || submissions != 2 || attempts < 3) return 20;
    if (submitted_at[0] < 20 || harness_error) return 21;
    reset_host(); ready_after = 10000;
    if (sendKey(HID_KEY_D) != pdFALSE || submissions || now > 1000) return 22;
    reset_host(); rejections = 100;
    if (sendKey(HID_KEY_D) != pdFALSE || submissions || attempts < 3 || attempts > 6 || now > 1000) return 23;

    reset_host(); plans[0].outcome = FAIL;
    if (sendKey(HID_KEY_D) != pdTRUE || down_count() != 2 || submissions != 3) return 28;
    if (keyboard_get_delivery_status() != KEYBOARD_DELIVERY_USB_CONFIRMED || harness_error) return 29;

    reset_host(); plans[0].outcome = plans[1].outcome = plans[2].outcome = FAIL;
    if (sendKey(HID_KEY_D) != pdFALSE) return 30;
    if (keyboard_get_delivery_status() != KEYBOARD_DELIVERY_UNCONFIRMED || down_count() != 3) return 31;
    if (lock_takes != lock_gives || lock_depth || harness_error) return 32;

    reset_host(); plans[0].outcome = SILENT;
    if (sendKey(HID_KEY_D) != pdFALSE) return 40;
    if (keyboard_get_delivery_status() != KEYBOARD_DELIVERY_UNCONFIRMED || down_count() != 1 || now == 0) return 41;
    // Do not overwrite an unresolved endpoint buffer with a second operation.
    if (sendKey(HID_KEY_F) != pdFALSE || submissions != 1) return 46;
    // A stale completion cannot turn the previous failed operation into success.
    endpoint_busy = false;
    tud_hid_report_complete_cb(0, submitted[0], sizeof(submitted[0]));
    if (keyboard_get_delivery_status() != KEYBOARD_DELIVERY_UNCONFIRMED) return 42;
    unsigned before = submissions;
    if (sendKey(HID_KEY_F) != pdTRUE) return 43;
    if (submitted[before][3] != 0 || submitted[before + 1][3] != HID_KEY_F) return 44;
    if (harness_error) return 45;

    reset_host(); plans[0].outcome = SILENT_READY;
    if (sendKey(HID_KEY_D) != pdFALSE || submissions != 1 || endpoint_busy) return 47;
    // The eventual old down callback must be consumed before any release report.
    tud_hid_report_complete_cb(0, submitted[0], sizeof(submitted[0]));
    if (keyboard_get_delivery_status() != KEYBOARD_DELIVERY_UNCONFIRMED) return 53;
    before = submissions;
    if (sendKey(HID_KEY_F) != pdTRUE || submitted[before][3] != 0 || submitted[before + 1][3] != HID_KEY_F) return 54;
    if (harness_error) return 55;

    reset_host(); plans[1].outcome = SILENT_READY;
    if (sendKey(HID_KEY_D) != pdFALSE || submissions != 2) return 56;
    // An old key-down completion cannot satisfy the outstanding key-release.
    tud_hid_report_complete_cb(0, submitted[0], sizeof(submitted[0]));
    if (sendKey(HID_KEY_F) != pdFALSE || submissions != 2) return 57;
    tud_hid_report_complete_cb(0, submitted[1], sizeof(submitted[1]));
    if (keyboard_get_delivery_status() != KEYBOARD_DELIVERY_UNCONFIRMED) return 58;
    if (sendKey(HID_KEY_F) != pdTRUE || down_count() != 2 || harness_error) return 59;

    reset_host(); plans[1].outcome = FAIL;
    if (sendKey(HID_KEY_D) != pdTRUE || down_count() != 1 || submissions != 3) return 48;
    if (keyboard_get_delivery_status() != KEYBOARD_DELIVERY_USB_CONFIRMED || harness_error) return 49;

    reset_host(); plans[1].outcome = plans[2].outcome = plans[3].outcome = FAIL;
    if (sendKey(HID_KEY_D) != pdFALSE) return 50;
    if (keyboard_get_delivery_status() != KEYBOARD_DELIVERY_UNCONFIRMED || down_count() != 1) return 51;
    if (lock_takes != lock_gives || lock_depth || harness_error) return 52;
    // Run the exact release-only routine used by the background task, without
    // making a second key operation trigger cleanup on its behalf.
    before = submissions;
    if (xSemaphoreTake(&semaphores[0], 0) != pdTRUE) return 100;
    background_recovery = true;
    bool recovered = recover_keyboard_release(current_connection_epoch());
    background_recovery = false;
    xSemaphoreGive(&semaphores[0]);
    if (!recovered || submissions != before + 1 || down_count() != 1) return 101;
    for (unsigned i = before; i < submissions; ++i) if (submitted[i][3] != 0) return 102;
    if (keyboard_get_delivery_status() != KEYBOARD_DELIVERY_UNCONFIRMED || harness_error) return 103;
    before = submissions;
    if (sendKey(HID_KEY_F) != pdTRUE || submissions != before + 2 || submitted[before][3] != HID_KEY_F) return 104;

    reset_host(); plans[0].outcome = DISCONNECT;
    if (sendKey(HID_KEY_D) != pdFALSE) return 60;
    if (keyboard_get_delivery_status() != KEYBOARD_DELIVERY_UNCONFIRMED || down_count() != 1) return 61;
    mounted = true; endpoint_busy = false; tud_mount_cb();
    before = submissions;
    if (sendKey(HID_KEY_F) != pdTRUE) return 62;
    if (submitted[before][3] != 0 || submitted[before + 1][3] != HID_KEY_F) return 63;
    if (harness_error) return 64;

    reset_host(); plans[0].outcome = plans[1].outcome = plans[2].outcome = WRONG_INSTANCE;
    if (sendKey(HID_KEY_D) != pdFALSE || keyboard_get_delivery_status() != KEYBOARD_DELIVERY_UNCONFIRMED) return 70;
    reset_host(); plans[0].outcome = plans[1].outcome = plans[2].outcome = WRONG_REPORT;
    if (sendKey(HID_KEY_D) != pdFALSE || keyboard_get_delivery_status() != KEYBOARD_DELIVERY_UNCONFIRMED) return 71;
    reset_host(); plans[0].outcome = plans[1].outcome = plans[2].outcome = WRONG_LENGTH;
    if (sendKey(HID_KEY_D) != pdFALSE || keyboard_get_delivery_status() != KEYBOARD_DELIVERY_UNCONFIRMED) return 72;

    reset_host();
    if (sendText("12,3") != pdTRUE || down_count() != 4) return 80;
    if (lock_takes != 1 || lock_gives != 1 || lock_depth || harness_error) return 81;
    if (submitted[0][3] != HID_KEY_1 || submitted[2][3] != HID_KEY_2 || submitted[4][3] != HID_KEY_COMMA || submitted[6][3] != HID_KEY_3) return 82;
    reset_host(); plans[2].outcome = plans[3].outcome = plans[4].outcome = FAIL;
    if (sendText("123") != pdFALSE || down_count() != 4) return 83;
    if (keyboard_get_delivery_status() != KEYBOARD_DELIVERY_UNCONFIRMED || harness_error) return 84;
    for (unsigned i = 2; i < submissions; ++i) if (submitted[i][3] && submitted[i][3] != HID_KEY_2) return 85;

    reset_host();
    if (sendTextWithKeys(HID_KEY_D, "12", HID_KEY_F) != pdTRUE || down_count() != 4) return 86;
    if (lock_takes != 1 || lock_gives != 1 || lock_depth || harness_error) return 87;
    if (submitted[0][3] != HID_KEY_D || submitted[2][3] != HID_KEY_1 || submitted[4][3] != HID_KEY_2 || submitted[6][3] != HID_KEY_F) return 88;

    reset_host(); plans[0].outcome = plans[1].outcome = plans[2].outcome = FAIL;
    if (sendTextWithKeys(HID_KEY_D, "12", HID_KEY_ENTER) != pdFALSE || down_count() != 3) return 110;
    for (unsigned i = 0; i < submissions; ++i) if (submitted[i][3] != 0 && submitted[i][3] != HID_KEY_D) return 111;
    if (keyboard_get_delivery_status() != KEYBOARD_DELIVERY_UNCONFIRMED || harness_error) return 112;

    reset_host(); plans[4].outcome = plans[5].outcome = plans[6].outcome = FAIL;
    if (sendTextWithKeys(HID_KEY_D, "12", HID_KEY_ENTER) != pdFALSE || down_count() != 5) return 113;
    // A partial time must never be committed by sending the final Enter.
    for (unsigned i = 0; i < submissions; ++i) if (submitted[i][3] == HID_KEY_ENTER) return 114;
    if (keyboard_get_delivery_status() != KEYBOARD_DELIVERY_UNCONFIRMED || harness_error) return 115;

    reset_host(); mounted = false; tud_umount_cb();
    if (sendKey(HID_KEY_D) != pdFALSE || submissions || keyboard_get_delivery_status() != KEYBOARD_DELIVERY_UNCONFIRMED) return 90;
    reset_host(); suspended = true;
    if (sendKey(HID_KEY_D) != pdFALSE || submissions || keyboard_get_delivery_status() != KEYBOARD_DELIVERY_UNCONFIRMED) return 91;

    reset_host();
    // Another producer owns the mutex: this caller must never touch its reports.
    if (xSemaphoreTake(&semaphores[0], 0) != pdTRUE) return 92;
    if (sendKey(HID_KEY_D) != pdFALSE || submissions) return 93;
    xSemaphoreGive(&semaphores[0]);
    if (sendKey(HID_KEY_D) != pdTRUE || down_count() != 1 || harness_error) return 94;
    return harness_error ? 99 : 0;
}
