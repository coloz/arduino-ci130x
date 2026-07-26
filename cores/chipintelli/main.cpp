#include "Arduino.h"
#include "chipintelli_cwsl_bridge.h"

extern "C" {
#include "FreeRTOS.h"
#include "task.h"
#include "system_msg_deal.h"
#include "command_info.h"
}

static TaskHandle_t s_arduinoTask;
static volatile chipintelli_sdk_state_t s_sdkState = CHIPINTELLI_SDK_NOT_STARTED;
static chipintelli_asr_callback_t s_asrCallback;
static void *s_asrCallbackArg;
static chipintelli_cwsl_callback_t s_cwslCallback;
static void *s_cwslCallbackArg;
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
        serialEventRun();
        // taskYIELD() only rotates among equal-priority tasks. Block for one
        // tick so the lower-priority FreeRTOS idle task can run housekeeping.
        vTaskDelay(1);
    }
}

extern "C" int ci_arduino_sdk_start(void);
extern "C" bool chipintelli_sdk_begin(void) {
    taskENTER_CRITICAL();
    const chipintelli_sdk_state_t state = s_sdkState;
    if (state == CHIPINTELLI_SDK_STARTING || state == CHIPINTELLI_SDK_READY) {
        taskEXIT_CRITICAL();
        return true;
    }
    if (state == CHIPINTELLI_SDK_FAILED) {
        taskEXIT_CRITICAL();
        return false;
    }
    s_sdkState = CHIPINTELLI_SDK_STARTING;
    taskEXIT_CRITICAL();

    if (ci_arduino_sdk_start() == 0) {
        taskENTER_CRITICAL();
        s_sdkState = CHIPINTELLI_SDK_FAILED;
        taskEXIT_CRITICAL();
        return false;
    }
    return true;
}

extern "C" chipintelli_sdk_state_t chipintelli_sdk_state(void) {
    taskENTER_CRITICAL();
    const chipintelli_sdk_state_t state = s_sdkState;
    taskEXIT_CRITICAL();
    return state;
}

// Called only by the Arduino-adapted vendor initialization task.
extern "C" void chipintelli_sdk_notify_ready(void) {
    taskENTER_CRITICAL();
    if (s_sdkState == CHIPINTELLI_SDK_STARTING) {
        s_sdkState = CHIPINTELLI_SDK_READY;
    }
    taskEXIT_CRITICAL();
}

extern "C" void chipintelli_sdk_notify_failed(void) {
    taskENTER_CRITICAL();
    s_sdkState = CHIPINTELLI_SDK_FAILED;
    taskEXIT_CRITICAL();
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

static bool isValidCwslWordType(chipintelli_cwsl_word_type_t wordType,
                                bool allowAll) {
    return wordType == CHIPINTELLI_CWSL_COMMAND_WORD ||
           wordType == CHIPINTELLI_CWSL_WAKE_WORD ||
           (allowAll && wordType == CHIPINTELLI_CWSL_ALL_WORDS);
}

extern "C" bool chipintelli_cwsl_profile_enabled(void) {
#if USE_CWSL
    return true;
#else
    return false;
#endif
}

extern "C" void chipintelli_cwsl_set_callback(chipintelli_cwsl_callback_t callback,
                                                void *arg) {
    taskENTER_CRITICAL();
    s_cwslCallback = callback;
    s_cwslCallbackArg = arg;
    taskEXIT_CRITICAL();
}

extern "C" void chipintelli_cwsl_notify_event(uint8_t eventType,
                                                uint32_t commandId,
                                                uint16_t groupId,
                                                uint8_t wordType,
                                                uint8_t attempt,
                                                uint8_t result,
                                                uint32_t distance) {
    static_assert(static_cast<int>(CHIPINTELLI_CWSL_LEARNING_STARTED) ==
                      static_cast<int>(CI_ARDUINO_CWSL_EVENT_LEARNING_STARTED),
                  "CWSL event ABI mismatch");
    static_assert(static_cast<int>(CHIPINTELLI_CWSL_RECOGNIZED) ==
                      static_cast<int>(CI_ARDUINO_CWSL_EVENT_RECOGNIZED),
                  "CWSL event ABI mismatch");
    static_assert(static_cast<int>(CHIPINTELLI_CWSL_DELETE_FAILED) ==
                      static_cast<int>(CI_ARDUINO_CWSL_EVENT_DELETE_FAILED),
                  "CWSL event ABI mismatch");

    taskENTER_CRITICAL();
    chipintelli_cwsl_callback_t callback = s_cwslCallback;
    void *callbackArg = s_cwslCallbackArg;
    taskEXIT_CRITICAL();
    if (callback) {
        const chipintelli_cwsl_event_t event = {
            eventType,
            wordType,
            attempt,
            result,
            commandId,
            groupId,
            distance
        };
        callback(&event, callbackArg);
    }
}

extern "C" bool chipintelli_cwsl_learn(uint32_t commandId,
                                         uint16_t groupId,
                                         chipintelli_cwsl_word_type_t wordType) {
#if USE_CWSL
    if (chipintelli_sdk_state() != CHIPINTELLI_SDK_READY ||
        !isValidCwslWordType(wordType, false) ||
        commandId > UINT16_MAX || groupId > UINT8_MAX) {
        return false;
    }
    return ci_arduino_cwsl_learn_word(commandId, groupId, wordType) == 0;
#else
    (void)commandId;
    (void)groupId;
    (void)wordType;
    return false;
#endif
}

extern "C" bool chipintelli_cwsl_cancel(void) {
#if USE_CWSL
    return chipintelli_sdk_state() == CHIPINTELLI_SDK_READY &&
           ci_arduino_cwsl_cancel_learning() == 0;
#else
    return false;
#endif
}

extern "C" bool chipintelli_cwsl_erase(uint32_t commandId,
                                         uint16_t groupId,
                                         chipintelli_cwsl_word_type_t wordType) {
#if USE_CWSL
    if (chipintelli_sdk_state() != CHIPINTELLI_SDK_READY ||
        !isValidCwslWordType(wordType, true) ||
        (commandId != UINT32_MAX && commandId > UINT16_MAX) ||
        (groupId != UINT16_MAX && groupId > UINT8_MAX)) {
        return false;
    }
    return ci_arduino_cwsl_delete_word(commandId, groupId, wordType) == 0;
#else
    (void)commandId;
    (void)groupId;
    (void)wordType;
    return false;
#endif
}

extern "C" chipintelli_cwsl_state_t chipintelli_cwsl_state(void) {
#if USE_CWSL
    if (chipintelli_sdk_state() != CHIPINTELLI_SDK_READY) {
        return CHIPINTELLI_CWSL_UNAVAILABLE;
    }
    return static_cast<chipintelli_cwsl_state_t>(ci_arduino_cwsl_get_status());
#else
    return CHIPINTELLI_CWSL_UNAVAILABLE;
#endif
}

extern "C" int chipintelli_cwsl_template_count(
    chipintelli_cwsl_word_type_t wordType) {
#if USE_CWSL
    if (chipintelli_sdk_state() != CHIPINTELLI_SDK_READY ||
        !isValidCwslWordType(wordType, true)) {
        return -1;
    }
    return ci_arduino_cwsl_get_template_count(wordType);
#else
    (void)wordType;
    return -1;
#endif
}

extern "C" int chipintelli_cwsl_remaining_templates(void) {
#if USE_CWSL
    return chipintelli_sdk_state() == CHIPINTELLI_SDK_READY
               ? ci_arduino_cwsl_get_remaining_templates()
               : -1;
#else
    return -1;
#endif
}

extern "C" int chipintelli_cwsl_max_templates(void) {
#if USE_CWSL
    return chipintelli_sdk_state() == CHIPINTELLI_SDK_READY
               ? ci_arduino_cwsl_get_max_templates()
               : -1;
#else
    return -1;
#endif
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

    taskENTER_CRITICAL();
    chipintelli_asr_callback_t callback = s_asrCallback;
    void *callbackArg = s_asrCallbackArg;
    taskEXIT_CRITICAL();
    if (callback) {
        uint16_t frames = 0;
        int16_t resultScore = static_cast<int16_t>(score);
        if (s_pendingAsrValid && s_pendingAsrHandle == handle &&
            static_cast<uint8_t>(s_pendingAsrScore) == score) {
            frames = s_pendingAsrFrames;
            resultScore = s_pendingAsrScore;
        }
        chipintelli_asr_result_t result = {
            cmd_info_get_command_id(handle),
            cmd_info_get_semantic_id(handle),
            resultScore,
            frames,
            cmd_info_get_command_string(handle)
        };
        callback(&result, callbackArg);
    }
    s_pendingAsrValid = false;
}
