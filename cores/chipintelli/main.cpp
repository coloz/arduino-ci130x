#include "Arduino.h"

extern "C" {
#include "FreeRTOS.h"
#include "task.h"
#include "system_msg_deal.h"
#include "command_info.h"
}

static TaskHandle_t s_arduinoTask;
static chipintelli_asr_callback_t s_asrCallback;
static void *s_asrCallbackArg;
static cmd_handle_t s_pendingAsrHandle;
static uint16_t s_pendingAsrFrames;
static int16_t s_pendingAsrScore;
static bool s_pendingAsrValid;

static void initializeDefaultPins() {
    // Arduino owns only pins exposed by the selected variant. Leave flash,
    // reset and unbonded pads untouched; make every usable pin a high-impedance
    // GPIO until the sketch explicitly selects another function.
    for (uint8_t pin = 0; pin < NUM_DIGITAL_PINS; ++pin) {
        if (g_APinDescription[pin].capabilities & PIN_CAP_GPIO) {
            pinMode(pin, INPUT);
            detachInterrupt(pin);
        }
    }
}

static void arduinoTask(void *) {
    initializeDefaultPins();
    setup();
    for (;;) {
        loop();
        // taskYIELD() only rotates among equal-priority tasks. Block for one
        // tick so the lower-priority FreeRTOS idle task can run housekeeping.
        vTaskDelay(1);
    }
}

extern "C" int ci_arduino_sdk_start(void);
extern "C" bool chipintelli_sdk_begin(void) {
    return ci_arduino_sdk_start() != 0;
}

extern "C" void __real_vTaskStartScheduler(void);
extern "C" void __wrap_vTaskStartScheduler(void) {
    if (!s_arduinoTask) {
        // Match the SDK init-task priority. FreeRTOS time slicing then lets
        // setup()/loop() run even if a vendor initialization wait spins, while
        // arduinoTask still blocks for one tick after every loop iteration.
        BaseType_t result = xTaskCreate(arduinoTask, "arduino", 2048, nullptr, 4, &s_arduinoTask);
        if (result != pdPASS) {
            // This SDK builds configASSERT() as a no-op. Do not start a system
            // that appears alive but can never execute setup()/loop().
            for (;;) {}
        }
    }
    __real_vTaskStartScheduler();
}

extern "C" void chipintelli_asr_set_callback(chipintelli_asr_callback_t callback, void *arg) {
    taskENTER_CRITICAL();
    s_asrCallback = callback;
    s_asrCallbackArg = arg;
    taskEXIT_CRITICAL();
}

extern "C" uint32_t __real_deal_asr_msg_by_cmd_id(sys_msg_asr_data_t *, cmd_handle_t, uint16_t);
extern "C" uint32_t __wrap_deal_asr_msg_by_cmd_id(sys_msg_asr_data_t *message,
                                                    cmd_handle_t handle,
                                                    uint16_t commandId) {
    const uint32_t handled = __real_deal_asr_msg_by_cmd_id(message, handle, commandId);
    if (message &&
        (message->asr_status == MSG_ASR_STATUS_GOOD_RESULT ||
         message->asr_status == MSG_CWSL_STATUS_GOOD_RESULT)) {
        s_pendingAsrHandle = handle;
        s_pendingAsrFrames = message->asr_frames;
        s_pendingAsrScore = message->asr_score;
        s_pendingAsrValid = true;
    }
    return handled;
}

extern "C" void __real_sys_asr_result_hook(cmd_handle_t, uint8_t);
extern "C" void __wrap_sys_asr_result_hook(cmd_handle_t handle, uint8_t score) {
    // Preserve the SDK hook first: it forwards the result over the configured
    // UART/I2C protocol before Arduino user code is notified.
    __real_sys_asr_result_hook(handle, score);

    chipintelli_asr_callback_t callback = s_asrCallback;
    if (callback) {
        uint16_t frames = 0;
        if (s_pendingAsrValid && s_pendingAsrHandle == handle &&
            static_cast<uint8_t>(s_pendingAsrScore) == score) {
            frames = s_pendingAsrFrames;
        }
        chipintelli_asr_result_t result = {
            cmd_info_get_command_id(handle),
            cmd_info_get_semantic_id(handle),
            static_cast<int16_t>(score),
            frames,
            cmd_info_get_command_string(handle)
        };
        callback(&result, s_asrCallbackArg);
    }
    s_pendingAsrValid = false;
}
