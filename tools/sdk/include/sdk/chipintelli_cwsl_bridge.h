#pragma once

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

enum {
    CI_ARDUINO_CWSL_EVENT_LEARNING_STARTED = 1,
    CI_ARDUINO_CWSL_EVENT_RECORDING_STARTED,
    CI_ARDUINO_CWSL_EVENT_ATTEMPT_RESULT,
    CI_ARDUINO_CWSL_EVENT_LEARNING_SUCCEEDED,
    CI_ARDUINO_CWSL_EVENT_LEARNING_FAILED,
    CI_ARDUINO_CWSL_EVENT_LEARNING_CANCELLED,
    CI_ARDUINO_CWSL_EVENT_DELETE_SUCCEEDED,
    CI_ARDUINO_CWSL_EVENT_RECOGNIZED,
    CI_ARDUINO_CWSL_EVENT_DELETE_FAILED
};

void chipintelli_cwsl_notify_event(uint8_t event_type,
                                   uint32_t command_id,
                                   uint16_t group_id,
                                   uint8_t word_type,
                                   uint8_t attempt,
                                   uint8_t result,
                                   uint32_t distance);

int ci_arduino_cwsl_learn_word(uint32_t command_id,
                               uint16_t group_id,
                               uint8_t word_type);
int ci_arduino_cwsl_cancel_learning(void);
int ci_arduino_cwsl_delete_word(uint32_t command_id,
                                uint16_t group_id,
                                uint8_t word_type);
int ci_arduino_cwsl_get_status(void);
int ci_arduino_cwsl_get_template_count(uint8_t word_type);
int ci_arduino_cwsl_get_remaining_templates(void);
int ci_arduino_cwsl_get_max_templates(void);

#ifdef __cplusplus
}
#endif
