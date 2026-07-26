#include <stdbool.h>
#include "ci_log.h"
#include "cwsl_app_handle.h"
#include "cwsl_manage.h"
#include "cwsl_template_manager.h"
#include "prompt_player.h"
#include "status_share.h"
#include "chipintelli_cwsl_bridge.h"
#if USE_CWSL
extern void set_state_enter_wakeup(uint32_t exit_wakup_ms);
extern void cwsl_set_reserved_num(int num);


#define CWSL_CMD_NUMBER ((sizeof(reg_cmd_list) / sizeof(reg_cmd_list[0])))     // 可学习的命令词数量
#define CWSL_TPL_MINWORD            (2)                                        // 自学习模板的最小字数，默认 2 个字，可设置模板最小 2 、3 个字;
#define MAX_LEARN_REPEAT_NUMBER     ( CWSL_REG_TIMES + 2 )                     // 学习时，重复的总次数, 建议范围 CWSL_REG_TIMES + 1、CWSL_REG_TIMES + 2、CWSL_REG_TIMES + 3

typedef struct cwsl_reg_asr_struct
{
    int reg_cmd_id;         // 命令词ID
    int reg_play_id;        // 学习提示播报音ID
}cwsl_reg_asr_struct_t;

const cwsl_reg_asr_struct_t reg_cmd_list[]=
{   //命令词ID     //学习提示播报音ID
    {2,             1001},
    {3,             1002},
    {4,             1003},
    {5,             1004},
    {6,             1005},
    {7,             1006},
};

typedef enum
{
    CWSL_APP_REC, // 识别模式
    CWSL_APP_REG, // 学习模式
    CWSL_APP_DEL, // 删除模式
} cwsl_app_mode_t;

typedef struct
{
    int word_id;                    // 正在学习的命令词ID
    cwsl_word_type_t word_type;     // 正在学习的命令词类型
    cwsl_app_mode_t app_mode;       // 当前工作模式
    uint8_t continus_flag;          // 是否连续学习，用于简化连续学习命令词时的提示音,0:非连续学习; 1:连续学习
} cwsl_app_t;

cwsl_app_t cwsl_app;

typedef struct
{
    uint8_t active;
    uint8_t cancel_requested;
    uint8_t started;
    uint8_t record_requested;
    uint8_t submitted;
    uint8_t vendor_started;
    uint8_t vendor_completed;
    uint32_t command_id;
    uint16_t group_id;
    cwsl_word_type_t word_type;
    uint32_t generation;
} cwsl_arduino_operation_t;

typedef struct
{
    uint8_t valid;
    uint8_t event_type;
    uint8_t attempt;
    uint8_t result;
    uint8_t needs_reset;
    cwsl_arduino_operation_t operation;
} cwsl_arduino_terminal_t;

typedef struct
{
    uint8_t learning_active;
    uint8_t delete_active;
    cwsl_arduino_operation_t learning;
    cwsl_arduino_operation_t delete_operation;
} cwsl_arduino_reset_context_t;

typedef enum
{
    CWSL_OFFICIAL_CONTINUATION_NONE,
    CWSL_OFFICIAL_CONTINUATION_START_RECORD,
    CWSL_OFFICIAL_CONTINUATION_NEXT_WORD,
} cwsl_official_continuation_t;

typedef struct
{
    uint8_t learning_active;
    uint8_t start_pending;
    uint8_t started;
    uint8_t record_requested;
    uint8_t continuation_cancelled;
    uint8_t delete_mode_active;
    uint8_t delete_pending;
    cwsl_official_continuation_t continuation;
    uint32_t command_id;
    uint16_t group_id;
    cwsl_word_type_t word_type;
} cwsl_official_state_t;

static cwsl_arduino_operation_t sg_arduino_learning;
static cwsl_arduino_operation_t sg_arduino_delete;
static cwsl_official_state_t sg_official;
static uint32_t sg_arduino_next_generation;
static uint8_t sg_arduino_submission_in_progress;
static uint8_t sg_arduino_official_dispatch;
static uint8_t sg_arduino_progress_in_progress;
static uint8_t sg_arduino_transition;
static uint8_t sg_arduino_reset_pending;
static uint8_t sg_arduino_learning_rearm_blocked;
static uint8_t sg_arduino_record_end_in_progress;
static uint8_t sg_arduino_manual_end_quarantined;
static uint8_t sg_arduino_system_task_suspend_count;
static cwsl_arduino_terminal_t sg_arduino_deferred_terminal;

static int get_next_reg_cmd_word_index();
static void cwsl_play_done_callback_with_start_record(cmd_handle_t cmd_handle);

int cwsl_app_reset();

static void cwsl_arduino_notify(const cwsl_arduino_operation_t *operation,
                                uint8_t event_type,
                                uint8_t attempt,
                                uint8_t result,
                                uint32_t distance)
{
    chipintelli_cwsl_notify_event(event_type,
                                  operation->command_id,
                                  operation->group_id,
                                  (uint8_t)operation->word_type,
                                  attempt,
                                  result,
                                  distance);
}

static int cwsl_arduino_get_operation(const cwsl_arduino_operation_t *operation,
                                      cwsl_arduino_operation_t *snapshot)
{
    int active;

    taskENTER_CRITICAL();
    active = operation->active;
    if (active && snapshot != NULL)
    {
        *snapshot = *operation;
    }
    taskEXIT_CRITICAL();
    return active;
}

static void cwsl_arduino_clear_operation_locked(
                                        cwsl_arduino_operation_t *operation)
{
    operation->active = 0;
    operation->cancel_requested = 0;
    operation->started = 0;
    operation->record_requested = 0;
    operation->submitted = 0;
    operation->vendor_started = 0;
    operation->vendor_completed = 0;
}

static int cwsl_official_has_continuation_locked(void)
{
    return sg_official.continuation != CWSL_OFFICIAL_CONTINUATION_NONE;
}

static void cwsl_official_cancel_learning_locked(void)
{
    sg_official.learning_active = 0;
    sg_official.start_pending = 0;
    sg_official.started = 0;
    sg_official.record_requested = 0;
    if (cwsl_official_has_continuation_locked())
    {
        /* The prompt player invokes queued completion callbacks even when a
         * prompt is stopped.  Keep the token until that old callback drains. */
        sg_official.continuation_cancelled = 1;
    }
}

static void cwsl_arduino_lowlevel_reset(void)
{
    cwsl_manage_reset();
    #if USE_AEC_MODULE
    ciss_set(CI_SS_CWSL_AEC_MUTE_STATE, CI_SS_CWSL_AEC_MUTE_OFF);
    #endif
}

static TaskHandle_t cwsl_arduino_suspend_system_task(void)
{
    TaskHandle_t task_handle = xTaskGetHandle("UserTaskManageP");

    if (task_handle == NULL || task_handle == xTaskGetCurrentTaskHandle())
    {
        return NULL;
    }

    taskENTER_CRITICAL();
    if (sg_arduino_system_task_suspend_count < UINT8_MAX)
    {
        if (sg_arduino_system_task_suspend_count == 0)
        {
            vTaskSuspend(task_handle);
        }
        ++sg_arduino_system_task_suspend_count;
    }
    taskEXIT_CRITICAL();
    return task_handle;
}

static void cwsl_arduino_resume_task(TaskHandle_t task_handle)
{
    taskENTER_CRITICAL();
    if (task_handle != NULL && sg_arduino_system_task_suspend_count > 0)
    {
        --sg_arduino_system_task_suspend_count;
        if (sg_arduino_system_task_suspend_count == 0)
        {
            vTaskResume(task_handle);
        }
    }
    taskEXIT_CRITICAL();
}

static int cwsl_arduino_finish_transition(int reset_performed)
{
    for (;;)
    {
        int perform_reset = 0;

        taskENTER_CRITICAL();
        if (sg_arduino_reset_pending && !reset_performed)
        {
            sg_arduino_reset_pending = 0;
            perform_reset = 1;
        }
        else
        {
            sg_arduino_reset_pending = 0;
            sg_arduino_transition = 0;
            if (!sg_arduino_learning.active && !sg_arduino_delete.active)
            {
                cwsl_app.app_mode = CWSL_APP_REC;
            }
        }
        taskEXIT_CRITICAL();

        if (!perform_reset)
        {
            return reset_performed;
        }
        cwsl_arduino_lowlevel_reset();
        reset_performed = 1;
    }
}

static void cwsl_arduino_run_terminal(
                                        const cwsl_arduino_terminal_t *terminal)
{
    int reset_performed = terminal->needs_reset;

    if (terminal->needs_reset)
    {
        cwsl_arduino_lowlevel_reset();
    }
    else
    {
        #if USE_AEC_MODULE
        ciss_set(CI_SS_CWSL_AEC_MUTE_STATE, CI_SS_CWSL_AEC_MUTE_OFF);
        #endif
    }

    /* Cleanup is complete before admission is released or user code runs. */
    cwsl_arduino_finish_transition(reset_performed);
    cwsl_arduino_notify(&terminal->operation,
                        terminal->event_type,
                        terminal->attempt,
                        terminal->result,
                        0);
}

/* Returns 1 when this callback owns the terminal, 2 when the submitting API
 * must run it after its vendor call returns, 0 for the official flow, and -1
 * for a stale or mismatched callback. */
static int cwsl_arduino_begin_terminal(
                                        cwsl_arduino_operation_t *operation,
                                        uint32_t expected_generation,
                                        uint8_t event_type,
                                        uint8_t attempt,
                                        uint8_t result,
                                        uint8_t needs_reset,
                                        cwsl_arduino_terminal_t *terminal)
{
    int status = 0;

    taskENTER_CRITICAL();
    if (operation->active &&
        (expected_generation == 0 ||
         operation->generation == expected_generation))
    {
        if (!operation->submitted ||
            sg_arduino_transition ||
            sg_arduino_progress_in_progress)
        {
            status = -1;
        }
        else
        {
            terminal->valid = 1;
            terminal->event_type = event_type;
            terminal->attempt = attempt;
            terminal->result = result;
            terminal->needs_reset = needs_reset;
            terminal->operation = *operation;
            cwsl_arduino_clear_operation_locked(operation);
            cwsl_app.app_mode = CWSL_APP_REC;
            sg_arduino_transition = 1;
            if (sg_arduino_submission_in_progress)
            {
                sg_arduino_deferred_terminal = *terminal;
                status = 2;
            }
            else
            {
                status = 1;
            }
        }
    }
    else if (sg_arduino_transition ||
             sg_arduino_submission_in_progress ||
             sg_arduino_deferred_terminal.valid)
    {
        status = -1;
    }
    taskEXIT_CRITICAL();
    return status;
}

static int cwsl_arduino_begin_reset_locked(
                                        cwsl_arduino_reset_context_t *context)
{
    context->learning_active = 0;
    context->delete_active = 0;

    /* Fully queued type-1/type-9 operations have guaranteed FIFO callbacks.
     * Keep their ownership until that callback supplies an unambiguous fence. */
    if (sg_arduino_learning.active &&
        sg_arduino_learning.submitted &&
        sg_arduino_learning.vendor_completed &&
        !sg_arduino_learning.started)
    {
        sg_arduino_learning.cancel_requested = 1;
        sg_arduino_reset_pending = 1;
        return 0;
    }
    if (sg_official.start_pending)
    {
        sg_arduino_reset_pending = 1;
        return 0;
    }
    if (sg_arduino_delete.active &&
        sg_arduino_delete.submitted &&
        sg_arduino_delete.vendor_completed)
    {
        sg_arduino_reset_pending = 1;
        return 0;
    }
    if (sg_official.delete_pending)
    {
        sg_arduino_reset_pending = 1;
        return 0;
    }

    /* The vendor record-end callback carries no generation.  A forced reset
     * can stop a recording before its type-6 callback reaches this task, so
     * fail closed for the rest of this boot.  Delete and recognition remain
     * available, but only a real reinitialization may admit another REG. */
    if (sg_arduino_record_end_in_progress ||
        (sg_arduino_learning.active &&
         sg_arduino_learning.record_requested) ||
        (sg_official.learning_active && sg_official.record_requested))
    {
        sg_arduino_learning_rearm_blocked = 1;
    }

    sg_arduino_transition = 1;
    sg_arduino_reset_pending = 0;
    if (sg_arduino_learning.active && sg_arduino_learning.submitted)
    {
        context->learning_active = 1;
        context->learning = sg_arduino_learning;
    }
    if (sg_arduino_delete.active &&
        sg_arduino_delete.submitted &&
        sg_arduino_delete.vendor_started)
    {
        context->delete_active = 1;
        context->delete_operation = sg_arduino_delete;
    }
    cwsl_arduino_clear_operation_locked(&sg_arduino_learning);
    cwsl_arduino_clear_operation_locked(&sg_arduino_delete);
    cwsl_official_cancel_learning_locked();
    sg_official.delete_mode_active = 0;
    sg_official.delete_pending = 0;
    cwsl_app.app_mode = CWSL_APP_REC;
    return 1;
}

static void cwsl_arduino_run_reset(
                                    const cwsl_arduino_reset_context_t *context)
{
    cwsl_arduino_lowlevel_reset();
    cwsl_arduino_finish_transition(1);

    if (context->learning_active)
    {
        cwsl_arduino_notify(&context->learning,
                            CI_ARDUINO_CWSL_EVENT_LEARNING_CANCELLED,
                            0,
                            (uint8_t)CWSL_REG_ABORT,
                            0);
    }
    if (context->delete_active)
    {
        cwsl_arduino_notify(&context->delete_operation,
                            CI_ARDUINO_CWSL_EVENT_DELETE_FAILED,
                            0,
                            (uint8_t)CWSL_REG_ABORT,
                            0);
    }
}

static int cwsl_arduino_claim_operation(cwsl_arduino_operation_t *operation,
                                        cwsl_app_mode_t app_mode,
                                        uint32_t command_id,
                                        uint16_t group_id,
                                        cwsl_word_type_t word_type,
                                        cwsl_arduino_operation_t *snapshot)
{
    int claimed = 0;

    taskENTER_CRITICAL();
    if (cwsl_app.app_mode == CWSL_APP_REC &&
        (app_mode != CWSL_APP_REG ||
         !sg_arduino_learning_rearm_blocked) &&
        !sg_arduino_learning.active &&
        !sg_arduino_delete.active &&
        !sg_official.learning_active &&
        !sg_official.delete_mode_active &&
        !sg_official.delete_pending &&
        !cwsl_official_has_continuation_locked() &&
        !sg_arduino_submission_in_progress &&
        !sg_arduino_official_dispatch &&
        !sg_arduino_progress_in_progress &&
        !sg_arduino_record_end_in_progress &&
        !sg_arduino_transition &&
        !sg_arduino_reset_pending)
    {
        memset(operation, 0, sizeof(*operation));
        operation->active = 1;
        operation->command_id = command_id;
        operation->group_id = group_id;
        operation->word_type = word_type;
        ++sg_arduino_next_generation;
        if (sg_arduino_next_generation == 0)
        {
            ++sg_arduino_next_generation;
        }
        operation->generation = sg_arduino_next_generation;
        cwsl_app.app_mode = app_mode;
        sg_arduino_submission_in_progress = 1;
        *snapshot = *operation;
        claimed = 1;
    }
    taskEXIT_CRITICAL();
    return claimed;
}

static int cwsl_arduino_mark_submitted(cwsl_arduino_operation_t *operation,
                                       uint32_t expected_generation)
{
    int submitted = 0;

    taskENTER_CRITICAL();
    if (operation->active &&
        operation->generation == expected_generation &&
        !sg_arduino_reset_pending &&
        !sg_arduino_transition)
    {
        operation->submitted = 1;
        submitted = 1;
    }
    taskEXIT_CRITICAL();
    return submitted;
}

/* This is the direct operation's linearization point.  A reset that runs
 * before it prevents the vendor call; a later reset only marks pending until
 * the complete vendor call returns. */
static int cwsl_arduino_prepare_vendor_call(
                                        cwsl_arduino_operation_t *operation,
                                        uint32_t expected_generation)
{
    int prepared = 0;

    taskENTER_CRITICAL();
    if (operation->active &&
        operation->submitted &&
        operation->generation == expected_generation &&
        !sg_arduino_reset_pending &&
        !sg_arduino_transition)
    {
        operation->vendor_started = 1;
        prepared = 1;
    }
    taskEXIT_CRITICAL();
    return prepared;
}

static void cwsl_arduino_abort_submission_before_vendor(
                                        cwsl_arduino_operation_t *operation,
                                        uint32_t expected_generation)
{
    int run_reset = 0;

    taskENTER_CRITICAL();
    if (operation->active && operation->generation == expected_generation)
    {
        cwsl_arduino_clear_operation_locked(operation);
    }
    sg_arduino_submission_in_progress = 0;
    sg_arduino_deferred_terminal.valid = 0;
    if (sg_arduino_reset_pending)
    {
        sg_arduino_reset_pending = 0;
        sg_arduino_transition = 1;
        run_reset = 1;
    }
    else if (!sg_arduino_learning.active && !sg_arduino_delete.active)
    {
        cwsl_app.app_mode = CWSL_APP_REC;
    }
    taskEXIT_CRITICAL();

    if (run_reset)
    {
        cwsl_arduino_lowlevel_reset();
        cwsl_arduino_finish_transition(1);
    }
}

static int cwsl_arduino_finish_submission(
                                        cwsl_arduino_operation_t *operation,
                                        uint32_t expected_generation,
                                        int vendor_result)
{
    cwsl_arduino_terminal_t terminal;
    cwsl_arduino_reset_context_t reset_context;
    int run_terminal = 0;
    int run_reset = 0;
    int run_failed_cleanup = 0;

    taskENTER_CRITICAL();
    if (vendor_result > 0 &&
        operation->active &&
        operation->generation == expected_generation)
    {
        operation->vendor_completed = 1;
    }
    else if (vendor_result == 0 &&
             operation->active &&
             operation->generation == expected_generation &&
             operation->vendor_started &&
             !sg_arduino_deferred_terminal.valid)
    {
        cwsl_arduino_clear_operation_locked(operation);
        cwsl_app.app_mode = CWSL_APP_REC;
        sg_arduino_transition = 1;
        run_failed_cleanup = 1;
    }
    sg_arduino_submission_in_progress = 0;
    if (sg_arduino_deferred_terminal.valid)
    {
        terminal = sg_arduino_deferred_terminal;
        sg_arduino_deferred_terminal.valid = 0;
        run_terminal = 1;
    }
    else if (!run_failed_cleanup &&
             sg_arduino_reset_pending &&
             !sg_arduino_official_dispatch &&
             !sg_arduino_transition)
    {
        run_reset = cwsl_arduino_begin_reset_locked(&reset_context);
    }
    taskEXIT_CRITICAL();

    if (run_terminal)
    {
        cwsl_arduino_run_terminal(&terminal);
    }
    else if (run_reset)
    {
        cwsl_arduino_run_reset(&reset_context);
    }
    else if (run_failed_cleanup)
    {
        cwsl_arduino_lowlevel_reset();
        cwsl_arduino_finish_transition(1);
    }

    taskENTER_CRITICAL();
    int still_active = operation->active &&
                       operation->generation == expected_generation;
    taskEXIT_CRITICAL();
    return still_active;
}

static int cwsl_arduino_begin_official_dispatch(void)
{
    int claimed = 0;

    taskENTER_CRITICAL();
    if (!sg_arduino_learning.active &&
        !sg_arduino_delete.active &&
        !cwsl_official_has_continuation_locked() &&
        !sg_arduino_submission_in_progress &&
        !sg_arduino_official_dispatch &&
        !sg_arduino_progress_in_progress &&
        !sg_arduino_transition &&
        !sg_arduino_reset_pending)
    {
        sg_arduino_official_dispatch = 1;
        claimed = 1;
    }
    taskEXIT_CRITICAL();
    return claimed;
}

static void cwsl_arduino_finish_official_dispatch(void)
{
    cwsl_arduino_reset_context_t reset_context;
    int run_reset = 0;

    taskENTER_CRITICAL();
    sg_arduino_official_dispatch = 0;
    if (sg_arduino_reset_pending &&
        !sg_arduino_submission_in_progress &&
        !sg_arduino_transition)
    {
        run_reset = cwsl_arduino_begin_reset_locked(&reset_context);
    }
    taskEXIT_CRITICAL();

    if (run_reset)
    {
        cwsl_arduino_run_reset(&reset_context);
    }
}

static void cwsl_arduino_finish_progress(void)
{
    cwsl_arduino_reset_context_t reset_context;
    int run_reset = 0;

    taskENTER_CRITICAL();
    sg_arduino_progress_in_progress = 0;
    if (sg_arduino_reset_pending &&
        !sg_arduino_submission_in_progress &&
        !sg_arduino_official_dispatch &&
        !sg_arduino_transition)
    {
        run_reset = cwsl_arduino_begin_reset_locked(&reset_context);
    }
    taskEXIT_CRITICAL();

    if (run_reset)
    {
        cwsl_arduino_run_reset(&reset_context);
    }
}

static int cwsl_arduino_begin_learning_progress(
                                        uint32_t expected_generation,
                                        int require_started,
                                        cwsl_arduino_operation_t *snapshot)
{
    int claimed = 0;

    taskENTER_CRITICAL();
    if (sg_arduino_learning.active &&
        sg_arduino_learning.submitted &&
        sg_arduino_learning.generation == expected_generation &&
        (!require_started || sg_arduino_learning.started) &&
        !sg_arduino_learning.cancel_requested &&
        !sg_arduino_submission_in_progress &&
        !sg_arduino_official_dispatch &&
        !sg_arduino_progress_in_progress &&
        !sg_arduino_transition &&
        !sg_arduino_reset_pending)
    {
        sg_arduino_progress_in_progress = 1;
        *snapshot = sg_arduino_learning;
        claimed = 1;
    }
    taskEXIT_CRITICAL();
    return claimed;
}

static int cwsl_arduino_learning_can_continue(uint32_t expected_generation)
{
    int can_continue;

    taskENTER_CRITICAL();
    can_continue = sg_arduino_learning.active &&
                   sg_arduino_learning.submitted &&
                   !sg_arduino_learning.cancel_requested &&
                   !sg_arduino_learning_rearm_blocked &&
                   !sg_arduino_reset_pending &&
                   !sg_arduino_transition &&
                   sg_arduino_learning.generation == expected_generation;
    taskEXIT_CRITICAL();
    return can_continue;
}

static int cwsl_arduino_start_direct_recording(uint32_t expected_generation)
{
    cwsl_arduino_operation_t learning;
    int accepted = 0;

    taskENTER_CRITICAL();
    if (sg_arduino_learning.active &&
        sg_arduino_learning.submitted &&
        sg_arduino_learning.started &&
        !sg_arduino_learning.record_requested &&
        !sg_arduino_learning.cancel_requested &&
        !sg_arduino_learning_rearm_blocked &&
        sg_arduino_learning.generation == expected_generation &&
        sg_arduino_progress_in_progress &&
        !sg_arduino_transition &&
        !sg_arduino_reset_pending)
    {
        sg_arduino_learning.record_requested = 1;
        learning = sg_arduino_learning;
        accepted = 1;
    }
    taskEXIT_CRITICAL();

    if (!accepted)
    {
        return -1;
    }

    int ret = cwsl_reg_record_start();
    if (ret != 0)
    {
        taskENTER_CRITICAL();
        if (sg_arduino_learning.active &&
            sg_arduino_learning.generation == expected_generation)
        {
            sg_arduino_learning.record_requested = 0;
        }
        taskEXIT_CRITICAL();
        return ret;
    }

    /* This vendor build never calls on_cwsl_record_start().  Define the
     * Arduino event precisely as successful submission of the record-start
     * request, which is the earliest reliable observable boundary. */
    cwsl_arduino_notify(&learning,
                        CI_ARDUINO_CWSL_EVENT_RECORDING_STARTED,
                        0,
                        0,
                        0);
    return 0;
}

/* Returns 1 for a direct recording, 2 for the official flow, -1 for the
 * quarantined callback after a forced reset, and 0 for unrelated stale data. */
static int cwsl_arduino_take_record_end(
                                        cwsl_arduino_operation_t *learning)
{
    int owner = 0;

    taskENTER_CRITICAL();
    if (sg_arduino_learning.active &&
        sg_arduino_learning.submitted &&
        sg_arduino_learning.started &&
        sg_arduino_learning.record_requested &&
        cwsl_app.app_mode == CWSL_APP_REG)
    {
        sg_arduino_learning.record_requested = 0;
        sg_arduino_record_end_in_progress = 1;
        *learning = sg_arduino_learning;
        owner = 1;
    }
    else if (!sg_arduino_learning.active &&
             sg_official.learning_active &&
             sg_official.started &&
             sg_official.record_requested &&
             !sg_official.start_pending &&
             cwsl_app.app_mode == CWSL_APP_REG)
    {
        sg_official.record_requested = 0;
        sg_arduino_record_end_in_progress = 1;
        owner = 2;
    }
    else if (sg_arduino_learning_rearm_blocked)
    {
        /* A forced reset interrupted an untagged producer session.  Ignore all
         * later record-end callbacks for the rest of this boot; the vendor
         * interface cannot prove which one is the final callback. */
        owner = -1;
    }
    taskEXIT_CRITICAL();
    return owner;
}

static void cwsl_arduino_finish_record_end(void)
{
    taskENTER_CRITICAL();
    if (sg_arduino_reset_pending)
    {
        sg_arduino_learning_rearm_blocked = 1;
    }
    sg_arduino_record_end_in_progress = 0;
    taskEXIT_CRITICAL();
}

/* The official application may force a type-6 message before the NN producer
 * emits its natural end.  Since those two producers share no epoch or once
 * flag, never admit another recording after taking that path in this boot. */
static int cwsl_arduino_prepare_manual_nn_end(void)
{
    int action = 0;

    taskENTER_CRITICAL();
    if (cwsl_app.app_mode == CWSL_APP_REG)
    {
        /* Still consume the ASR message when no recording owns it, but do not
         * manufacture a stale type-6. */
        action = 2;
        if ((sg_arduino_learning.active &&
             sg_arduino_learning.record_requested) ||
            (sg_official.learning_active && sg_official.record_requested))
        {
            sg_arduino_learning_rearm_blocked = 1;
            sg_arduino_manual_end_quarantined = 1;
            action = 1;
        }
    }
    else if (cwsl_app.app_mode == CWSL_APP_DEL)
    {
        action = 1;
    }
    taskEXIT_CRITICAL();
    return action;
}

static int cwsl_arduino_manual_nn_end_is_quarantined(void)
{
    int quarantined;

    taskENTER_CRITICAL();
    quarantined = sg_arduino_manual_end_quarantined;
    taskEXIT_CRITICAL();
    return quarantined;
}

static int cwsl_app_claim_official_mode(cwsl_app_mode_t app_mode)
{
    int claimed = 0;

    taskENTER_CRITICAL();
    if (cwsl_app.app_mode == CWSL_APP_REC &&
        (app_mode != CWSL_APP_REG ||
         !sg_arduino_learning_rearm_blocked) &&
        !sg_arduino_learning.active &&
        !sg_arduino_delete.active &&
        !sg_official.learning_active &&
        !sg_official.delete_mode_active &&
        !sg_official.delete_pending &&
        !cwsl_official_has_continuation_locked() &&
        !sg_arduino_submission_in_progress &&
        !sg_arduino_progress_in_progress &&
        !sg_arduino_record_end_in_progress &&
        !sg_arduino_transition &&
        !sg_arduino_reset_pending)
    {
        cwsl_app.app_mode = app_mode;
        if (app_mode == CWSL_APP_REG)
        {
            sg_official.learning_active = 1;
            sg_official.start_pending = 0;
            sg_official.started = 0;
            sg_official.record_requested = 0;
            sg_official.continuation_cancelled = 0;
        }
        else if (app_mode == CWSL_APP_DEL)
        {
            sg_official.delete_mode_active = 1;
            sg_official.delete_pending = 0;
        }
        claimed = 1;
    }
    taskEXIT_CRITICAL();
    return claimed;
}

static void cwsl_app_release_official_mode(void)
{
    taskENTER_CRITICAL();
    if (!sg_arduino_learning.active &&
        !sg_arduino_delete.active &&
        !sg_arduino_transition)
    {
        cwsl_official_cancel_learning_locked();
        sg_official.delete_mode_active = 0;
        sg_official.delete_pending = 0;
        cwsl_app.app_mode = CWSL_APP_REC;
    }
    taskEXIT_CRITICAL();
}

/* Returns 1 for a direct start, 2 when a cancelled/reset direct submission
 * reaches its FIFO fence, 0 for the official flow, and -1 for stale data. */
static int cwsl_arduino_mark_learning_started(uint32_t command_id,
                                              uint16_t group_id,
                                              cwsl_word_type_t word_type,
                                              cwsl_arduino_operation_t *snapshot)
{
    int result = 0;

    taskENTER_CRITICAL();
    if (sg_arduino_learning.active)
    {
        result = -1;
        if (sg_arduino_learning.submitted &&
            !sg_arduino_transition &&
            sg_arduino_learning.command_id == command_id &&
            sg_arduino_learning.group_id == group_id &&
            sg_arduino_learning.word_type == word_type)
        {
            *snapshot = sg_arduino_learning;
            if (sg_arduino_learning.cancel_requested ||
                sg_arduino_reset_pending)
            {
                result = 2;
            }
            else if (!sg_arduino_progress_in_progress)
            {
                sg_arduino_learning.started = 1;
                *snapshot = sg_arduino_learning;
                sg_arduino_progress_in_progress = 1;
                result = 1;
            }
        }
    }
    else if (sg_arduino_transition ||
             sg_arduino_submission_in_progress ||
             sg_arduino_progress_in_progress)
    {
        result = -1;
    }
    taskEXIT_CRITICAL();
    return result;
}

static int cwsl_official_submit_learning_reserved(uint32_t command_id,
                                                   uint16_t group_id,
                                                   cwsl_word_type_t word_type)
{
    int accepted = 0;
    int has_capacity = cwsl_tm_get_left_tpl_number() > 0;
    int wake_has_capacity = word_type != WAKEUP_WORD ||
                            cwsl_tm_get_reg_tpl_number(WAKEUP_WORD) <
                                CWSL_WAKEUP_NUMBER;

    taskENTER_CRITICAL();
    if (sg_arduino_official_dispatch &&
        sg_official.learning_active &&
        !sg_official.start_pending &&
        !cwsl_official_has_continuation_locked() &&
        !sg_arduino_learning_rearm_blocked &&
        has_capacity &&
        wake_has_capacity &&
        cwsl_app.app_mode == CWSL_APP_REG &&
        !sg_arduino_learning.active &&
        !sg_arduino_delete.active &&
        !sg_arduino_transition &&
        !sg_arduino_reset_pending)
    {
        sg_official.start_pending = 1;
        sg_official.started = 0;
        sg_official.record_requested = 0;
        sg_official.command_id = command_id;
        sg_official.group_id = group_id;
        sg_official.word_type = word_type;
        accepted = 1;
    }
    taskEXIT_CRITICAL();

    if (!accepted)
    {
        return -1;
    }

    int ret = cwsl_reg_word(command_id, group_id, word_type);
    if (ret != 0)
    {
        taskENTER_CRITICAL();
        if (sg_official.start_pending &&
            sg_official.command_id == command_id &&
            sg_official.group_id == group_id &&
            sg_official.word_type == word_type)
        {
            sg_official.start_pending = 0;
        }
        taskEXIT_CRITICAL();
    }
    return ret;
}

/* Returns 1 for an active official start, 2 when the callback is the FIFO
 * fence for a pending reset, and -1 when it is stale or mismatched. */
static int cwsl_official_mark_learning_started(uint32_t command_id,
                                                uint16_t group_id,
                                                cwsl_word_type_t word_type)
{
    int result = -1;

    taskENTER_CRITICAL();
    if (sg_official.start_pending &&
        sg_official.command_id == command_id &&
        sg_official.group_id == group_id &&
        sg_official.word_type == word_type)
    {
        sg_official.start_pending = 0;
        if (sg_arduino_reset_pending || sg_arduino_transition ||
            !sg_official.learning_active)
        {
            sg_arduino_reset_pending = 0;
            cwsl_official_cancel_learning_locked();
            sg_official.delete_mode_active = 0;
            cwsl_app.app_mode = CWSL_APP_REC;
            sg_arduino_transition = 1;
            result = 2;
        }
        else
        {
            sg_official.started = 1;
            result = 1;
        }
    }
    taskEXIT_CRITICAL();
    return result;
}

static int cwsl_official_arm_continuation(
                                    cwsl_official_continuation_t continuation)
{
    int armed = 0;

    taskENTER_CRITICAL();
    if (sg_official.learning_active &&
        sg_official.started &&
        !sg_official.record_requested &&
        !sg_official.start_pending &&
        !cwsl_official_has_continuation_locked() &&
        !sg_arduino_learning_rearm_blocked &&
        !sg_arduino_transition &&
        !sg_arduino_reset_pending)
    {
        sg_official.continuation = continuation;
        sg_official.continuation_cancelled = 0;
        if (continuation == CWSL_OFFICIAL_CONTINUATION_NEXT_WORD)
        {
            sg_official.started = 0;
        }
        armed = 1;
    }
    taskEXIT_CRITICAL();
    return armed;
}

/* Atomically drains a prompt token and, when still current, hands it directly
 * to an official vendor-action reservation. */
static int cwsl_official_take_continuation(
                                    cwsl_official_continuation_t continuation)
{
    int runnable = 0;

    taskENTER_CRITICAL();
    if (sg_official.continuation == continuation)
    {
        int current = !sg_official.continuation_cancelled &&
                      sg_official.learning_active &&
                      !sg_official.start_pending &&
                      cwsl_app.app_mode == CWSL_APP_REG &&
                      !sg_arduino_learning.active &&
                      !sg_arduino_delete.active &&
                      !sg_arduino_submission_in_progress &&
                      !sg_arduino_progress_in_progress &&
                      !sg_arduino_learning_rearm_blocked &&
                      !sg_arduino_transition &&
                      !sg_arduino_reset_pending;
        if (continuation == CWSL_OFFICIAL_CONTINUATION_START_RECORD)
        {
            current = current && sg_official.started;
        }
        sg_official.continuation = CWSL_OFFICIAL_CONTINUATION_NONE;
        sg_official.continuation_cancelled = 0;
        if (current)
        {
            if (sg_arduino_official_dispatch)
            {
                /* A prompt may complete while its originating official ASR
                 * handler still owns the reservation.  Borrow that owner. */
                runnable = 2;
            }
            else
            {
                sg_arduino_official_dispatch = 1;
                runnable = 1;
            }
        }
    }
    taskEXIT_CRITICAL();
    return runnable;
}

static int cwsl_official_start_recording_reserved(void)
{
    int accepted = 0;

    taskENTER_CRITICAL();
    if (sg_arduino_official_dispatch &&
        sg_official.learning_active &&
        sg_official.started &&
        !sg_official.record_requested &&
        !sg_official.start_pending &&
        !sg_arduino_learning_rearm_blocked &&
        cwsl_app.app_mode == CWSL_APP_REG &&
        !sg_arduino_transition &&
        !sg_arduino_reset_pending)
    {
        sg_official.record_requested = 1;
        accepted = 1;
    }
    taskEXIT_CRITICAL();

    if (!accepted)
    {
        return -1;
    }

    int ret = cwsl_reg_record_start();
    if (ret != 0)
    {
        taskENTER_CRITICAL();
        sg_official.record_requested = 0;
        taskEXIT_CRITICAL();
    }
    return ret;
}

static int cwsl_official_prompt_then_record(uint16_t command_id,
                                            int select_index,
                                            bool preemptive)
{
    if (!cwsl_official_arm_continuation(
                            CWSL_OFFICIAL_CONTINUATION_START_RECORD))
    {
        return -1;
    }

    uint32_t ret = prompt_play_by_cmd_id(command_id,
                                          select_index,
                                          cwsl_play_done_callback_with_start_record,
                                          preemptive);
    if (ret != 0)
    {
        taskENTER_CRITICAL();
        if (sg_official.continuation ==
                            CWSL_OFFICIAL_CONTINUATION_START_RECORD)
        {
            sg_official.continuation = CWSL_OFFICIAL_CONTINUATION_NONE;
            sg_official.continuation_cancelled = 0;
        }
        taskEXIT_CRITICAL();
        return -1;
    }
    return 0;
}

static int cwsl_official_submit_delete_reserved(uint32_t command_id,
                                                 uint16_t group_id,
                                                 cwsl_word_type_t word_type)
{
    int accepted = 0;

    taskENTER_CRITICAL();
    if (sg_arduino_official_dispatch &&
        sg_official.delete_mode_active &&
        !sg_official.delete_pending &&
        cwsl_app.app_mode == CWSL_APP_DEL &&
        !sg_arduino_learning.active &&
        !sg_arduino_delete.active &&
        !sg_arduino_transition &&
        !sg_arduino_reset_pending)
    {
        sg_official.delete_pending = 1;
        accepted = 1;
    }
    taskEXIT_CRITICAL();

    if (!accepted)
    {
        return -1;
    }

    int ret = cwsl_delete_word(command_id, group_id, word_type);
    if (ret != 0)
    {
        taskENTER_CRITICAL();
        sg_official.delete_pending = 0;
        taskEXIT_CRITICAL();
    }
    return ret;
}

static int cwsl_arduino_command_matches_type(uint32_t command_id,
                                             cwsl_word_type_t word_type)
{
    cmd_handle_t cmd_handle;
    uint32_t is_wakeup;

    if (command_id > UINT16_MAX ||
        (word_type != CMD_WORD && word_type != WAKEUP_WORD))
    {
        return 0;
    }

    cmd_handle = cmd_info_find_command_by_id((uint16_t)command_id);
    if (cmd_handle == (cmd_handle_t)INVALID_HANDLE || cmd_handle == NULL)
    {
        return 0;
    }

    is_wakeup = cmd_info_is_wakeup_word(cmd_handle);
    return ((word_type == WAKEUP_WORD) && is_wakeup) ||
           ((word_type == CMD_WORD) && !is_wakeup);
}

static int cwsl_arduino_template_exists(uint32_t command_id,
                                        uint16_t group_id,
                                        cwsl_word_type_t word_type)
{
    uint8_t indices[CWSL_MAX_TPL_NUM];
    int count = cwsl_tm_get_words_index(indices,
                                        CWSL_MAX_TPL_NUM,
                                        group_id,
                                        word_type);
    for (int index = 0; index < count; ++index)
    {
        if (cwsl_tm_get_tpl_cmd_id_by_index(indices[index]) == command_id)
        {
            return 1;
        }
    }
    return 0;
}

static int cwsl_arduino_delete_target_valid(uint32_t command_id,
                                             uint16_t group_id,
                                             cwsl_word_type_t word_type)
{
    int command_wildcard = command_id == UINT32_MAX;
    int group_wildcard = group_id == UINT16_MAX;

    if (command_wildcard || group_wildcard)
    {
        /* The public bulk APIs always wildcard both fields.  Reject partial
         * wildcards so a truncated or mistyped ID cannot delete a wider set. */
        return command_wildcard && group_wildcard;
    }
    if (word_type == ALL_WORD ||
        (command_id >= CWSL_REGISTRATION_NEXT &&
         command_id <= CWSL_DELETE_ALL) ||
        !cwsl_arduino_command_matches_type(command_id, word_type))
    {
        return 0;
    }
    return cwsl_arduino_template_exists(command_id, group_id, word_type);
}


///////////重新学习中的删除上一次学习词条的逻辑 ////////////
static uint16_t sg_prev_group_id = 0;
static uint32_t sg_prev_cmd_id = 0;
static cwsl_word_type_t sg_prev_wordtype ;

static uint16_t sg_prev_group_id_tmp = 0;
static uint32_t sg_prev_cmd_id_tmp = 0;
static cwsl_word_type_t sg_prev_wordtype_tmp ;
static int sg_prev_appword_id = 0;

void cwsl_set_prev_appwordid(int appword_id)
{
    sg_prev_appword_id = appword_id;
}

void cwsl_save_prev_info(uint32_t cmd_id,uint16_t group_id,cwsl_word_type_t wordtype)
{
    sg_prev_group_id_tmp = group_id;
    sg_prev_cmd_id_tmp = cmd_id;
    sg_prev_wordtype_tmp = wordtype;
}

void cwsl_update_prev_info(void)
{
    sg_prev_group_id = sg_prev_group_id_tmp;
    sg_prev_cmd_id = sg_prev_cmd_id_tmp;
    sg_prev_wordtype = sg_prev_wordtype_tmp;
}

void cwsl_clear_prev_info(void)
{
    sg_prev_group_id = (uint16_t)-1;
    sg_prev_cmd_id = (uint32_t)-1;
    sg_prev_wordtype = CMD_WORD;

    sg_prev_group_id_tmp = (uint16_t)-1;
    sg_prev_cmd_id_tmp = (uint32_t)-1;
    sg_prev_wordtype_tmp = CMD_WORD;
    sg_prev_appword_id = (int)-1;
}

void cwsl_get_prev_info(uint32_t* cmd_id,uint16_t* group_id,int *appword_id,cwsl_word_type_t *wordtype )
{
    *group_id = sg_prev_group_id;
    *cmd_id = sg_prev_cmd_id ;
    *appword_id = sg_prev_appword_id;
    *wordtype = sg_prev_wordtype;
}

//重新学习前 删除上一次模板
int ci_arduino_cwsl_learn_word(uint32_t command_id,
                               uint16_t group_id,
                               uint8_t word_type)
{
    cwsl_arduino_operation_t learning;
    TaskHandle_t sys_task_handle;

    if ((word_type != (uint8_t)CMD_WORD && word_type != (uint8_t)WAKEUP_WORD) ||
        command_id > UINT16_MAX ||
        group_id > UINT8_MAX ||
        (command_id >= CWSL_REGISTRATION_NEXT &&
         command_id <= CWSL_DELETE_ALL) ||
        !cwsl_arduino_command_matches_type(command_id,
                                           (cwsl_word_type_t)word_type) ||
        ciss_get(CI_SS_START_SLEEP_PROCESS) != 0 ||
        cwsl_get_status() == CWSL_STA_REG_TEMPLATE ||
        cwsl_get_status() == CWSL_STA_DEL_TEMPLATE ||
        cwsl_tm_get_left_tpl_number() <= 0 ||
        ((cwsl_word_type_t)word_type == CMD_WORD &&
         cwsl_arduino_template_exists(command_id,
                                      group_id,
                                      CMD_WORD)) ||
        ((cwsl_word_type_t)word_type == WAKEUP_WORD &&
         cwsl_tm_get_reg_tpl_number(WAKEUP_WORD) >= CWSL_WAKEUP_NUMBER))
    {
        return -1;
    }

    if (!cwsl_arduino_claim_operation(&sg_arduino_learning,
                                      CWSL_APP_REG,
                                      command_id,
                                      group_id,
                                      (cwsl_word_type_t)word_type,
                                      &learning))
    {
        return -1;
    }

    sys_task_handle = cwsl_arduino_suspend_system_task();

    /* Revalidate every mutable predicate while this claim owns admission.
     * This closes validate/preempt/claim races with a just-finished operation. */
    if (ciss_get(CI_SS_START_SLEEP_PROCESS) != 0 ||
        cwsl_get_status() == CWSL_STA_REG_TEMPLATE ||
        cwsl_get_status() == CWSL_STA_DEL_TEMPLATE ||
        cwsl_tm_get_left_tpl_number() <= 0 ||
        ((cwsl_word_type_t)word_type == CMD_WORD &&
         cwsl_arduino_template_exists(command_id, group_id, CMD_WORD)) ||
        ((cwsl_word_type_t)word_type == WAKEUP_WORD &&
         cwsl_tm_get_reg_tpl_number(WAKEUP_WORD) >= CWSL_WAKEUP_NUMBER))
    {
        cwsl_arduino_abort_submission_before_vendor(&sg_arduino_learning,
                                                     learning.generation);
        cwsl_arduino_resume_task(sys_task_handle);
        return -1;
    }

    if (!cwsl_arduino_mark_submitted(&sg_arduino_learning,
                                     learning.generation))
    {
        cwsl_arduino_abort_submission_before_vendor(&sg_arduino_learning,
                                                     learning.generation);
        cwsl_arduino_resume_task(sys_task_handle);
        return -1;
    }

    cwsl_clear_prev_info();
    cwsl_app.word_id = -1;
    cwsl_app.word_type = (cwsl_word_type_t)word_type;
    cwsl_app.continus_flag = 0;

    #if USE_AEC_MODULE
    ciss_set(CI_SS_CWSL_AEC_MUTE_STATE, CI_SS_CWSL_AEC_MUTE_ON);
    #endif
    set_state_enter_wakeup(EXIT_WAKEUP_TIME);
    sys_ignore_exit_msg_in_queue();

    /* No wake-state writes are allowed after this linearization point.  A
     * concurrent sleep/reset can then only enqueue the vendor reset after us. */
    if (!cwsl_arduino_prepare_vendor_call(&sg_arduino_learning,
                                           learning.generation))
    {
        cwsl_arduino_abort_submission_before_vendor(&sg_arduino_learning,
                                                     learning.generation);
        cwsl_arduino_resume_task(sys_task_handle);
        return -1;
    }

    int ret = cwsl_reg_word(command_id,
                            group_id,
                            (cwsl_word_type_t)word_type);
    cwsl_arduino_finish_submission(&sg_arduino_learning,
                                   learning.generation,
                                   (ret == 0) ? 1 : 0);
    cwsl_arduino_resume_task(sys_task_handle);
    return ret;
}

int ci_arduino_cwsl_cancel_learning(void)
{
    int can_cancel = 0;
    uint32_t generation = 0;

    taskENTER_CRITICAL();
    if (sg_arduino_learning.active &&
        sg_arduino_learning.submitted &&
        !sg_arduino_submission_in_progress &&
        !sg_arduino_official_dispatch &&
        !sg_arduino_progress_in_progress &&
        !sg_arduino_transition &&
        !sg_arduino_reset_pending &&
        !sg_arduino_learning.started &&
        !sg_arduino_learning.cancel_requested &&
        cwsl_app.app_mode == CWSL_APP_REG)
    {
        sg_arduino_learning.cancel_requested = 1;
        sg_arduino_submission_in_progress = 1;
        generation = sg_arduino_learning.generation;
        can_cancel = 1;
    }
    taskEXIT_CRITICAL();

    if (!can_cancel)
    {
        return -1;
    }

    /* Before recording starts, the guaranteed type-1 callback is the FIFO
     * fence.  Keep this generation active until that callback cancels it. */
    int ret = cwsl_exit_reg_word();
    if (ret != 0)
    {
        taskENTER_CRITICAL();
        if (sg_arduino_learning.active &&
            sg_arduino_learning.generation == generation)
        {
            sg_arduino_learning.cancel_requested = 0;
        }
        taskEXIT_CRITICAL();
    }
    cwsl_arduino_finish_submission(&sg_arduino_learning,
                                    generation,
                                    -1);
    return ret == 0 ? 0 : -1;
}

int ci_arduino_cwsl_delete_word(uint32_t command_id,
                                uint16_t group_id,
                                uint8_t word_type)
{
    cwsl_arduino_operation_t delete_operation;
    TaskHandle_t sys_task_handle;

    if ((word_type != (uint8_t)CMD_WORD &&
         word_type != (uint8_t)WAKEUP_WORD &&
         word_type != (uint8_t)ALL_WORD) ||
        (command_id != UINT32_MAX && command_id > UINT16_MAX) ||
        (group_id != UINT16_MAX && group_id > UINT8_MAX) ||
        !cwsl_arduino_delete_target_valid(command_id,
                                           group_id,
                                           (cwsl_word_type_t)word_type) ||
        ciss_get(CI_SS_START_SLEEP_PROCESS) != 0 ||
        cwsl_get_status() == CWSL_STA_REG_TEMPLATE ||
        cwsl_get_status() == CWSL_STA_DEL_TEMPLATE)
    {
        return -1;
    }

    if (!cwsl_arduino_claim_operation(&sg_arduino_delete,
                                      CWSL_APP_DEL,
                                      command_id,
                                      group_id,
                                      (cwsl_word_type_t)word_type,
                                      &delete_operation))
    {
        return -1;
    }

    sys_task_handle = cwsl_arduino_suspend_system_task();

    if (ciss_get(CI_SS_START_SLEEP_PROCESS) != 0 ||
        cwsl_get_status() == CWSL_STA_REG_TEMPLATE ||
        cwsl_get_status() == CWSL_STA_DEL_TEMPLATE ||
        !cwsl_arduino_delete_target_valid(command_id,
                                           group_id,
                                           (cwsl_word_type_t)word_type))
    {
        cwsl_arduino_abort_submission_before_vendor(&sg_arduino_delete,
                                                     delete_operation.generation);
        cwsl_arduino_resume_task(sys_task_handle);
        return -1;
    }

    if (!cwsl_arduino_mark_submitted(&sg_arduino_delete,
                                     delete_operation.generation))
    {
        cwsl_arduino_abort_submission_before_vendor(&sg_arduino_delete,
                                                     delete_operation.generation);
        cwsl_arduino_resume_task(sys_task_handle);
        return -1;
    }

    set_state_enter_wakeup(EXIT_WAKEUP_TIME);
    sys_ignore_exit_msg_in_queue();

    if (!cwsl_arduino_prepare_vendor_call(&sg_arduino_delete,
                                           delete_operation.generation))
    {
        cwsl_arduino_abort_submission_before_vendor(&sg_arduino_delete,
                                                     delete_operation.generation);
        cwsl_arduino_resume_task(sys_task_handle);
        return -1;
    }

    int ret = cwsl_delete_word(command_id,
                               group_id,
                               (cwsl_word_type_t)word_type);
    cwsl_arduino_finish_submission(&sg_arduino_delete,
                                   delete_operation.generation,
                                   (ret == 0) ? 1 : 0);
    cwsl_arduino_resume_task(sys_task_handle);
    return ret;
}

int ci_arduino_cwsl_get_status(void)
{
    return (int)cwsl_get_status();
}

int ci_arduino_cwsl_get_template_count(uint8_t word_type)
{
    if (word_type == (uint8_t)ALL_WORD)
    {
        return cwsl_tm_get_reg_tpl_number(CMD_WORD) +
               cwsl_tm_get_reg_tpl_number(WAKEUP_WORD);
    }
    if (word_type != (uint8_t)CMD_WORD && word_type != (uint8_t)WAKEUP_WORD)
    {
        return -1;
    }
    return cwsl_tm_get_reg_tpl_number((cwsl_word_type_t)word_type);
}

int ci_arduino_cwsl_get_remaining_templates(void)
{
    return cwsl_tm_get_left_tpl_number();
}

int ci_arduino_cwsl_get_max_templates(void)
{
    return cwsl_tm_get_max_tpl_number();
}

void cwsl_app_delete_prev_word(void)
{
    if(1 == CWSL_REG_TIMES)
    {// 满足前提，先删除 上一次的 模板
        uint32_t cmd_id;
        uint16_t group_id;
        cwsl_word_type_t wordtype;
        int app_wordid ;
        //获取上一次模板的信息
        cwsl_get_prev_info(&cmd_id,&group_id,&app_wordid,&wordtype);
        
        if( (cmd_id != (uint32_t)-1) && (group_id != (uint16_t)-1) )
        {//删除模板
            // 在学习状态下 发送 删除指定模板 消息
            cwsl_delete_word_when_reg(cmd_id, group_id,wordtype);
            //更新 重新 学习 信息和 播报提示信息
            cwsl_set_wordinfo(cmd_id,group_id,wordtype);

            if(app_wordid != (int)-1)
            {
                cwsl_app.word_id = app_wordid;
            }
        }
    }
}


////cwsl 事件响应函数////////////////////////////////////////////////

// cwsl 模块初始化事件响应
// 必须返回可学习的模板数量 函数内部不能阻塞，延时
int on_cwsl_init(cwsl_init_parameter_t *cwsl_init_parameter)
{   
    memset(&sg_arduino_learning, 0, sizeof(sg_arduino_learning));
    memset(&sg_arduino_delete, 0, sizeof(sg_arduino_delete));
    memset(&sg_official, 0, sizeof(sg_official));
    sg_arduino_next_generation = 0;
    sg_arduino_submission_in_progress = 0;
    sg_arduino_official_dispatch = 0;
    sg_arduino_progress_in_progress = 0;
    sg_arduino_transition = 0;
    sg_arduino_reset_pending = 0;
    sg_arduino_learning_rearm_blocked = 0;
    sg_arduino_record_end_in_progress = 0;
    sg_arduino_manual_end_quarantined = 0;
    sg_arduino_system_task_suspend_count = 0;
    memset(&sg_arduino_deferred_terminal,
           0,
           sizeof(sg_arduino_deferred_terminal));
    cwsl_set_reserved_num(CICWSL_TOTAL_TEMPLATE);
    cwsl_app.app_mode = CWSL_APP_REC;
    cwsl_app.word_id = -1;
    CI_ASSERT(CICWSL_TOTAL_TEMPLATE >= (CWSL_WAKEUP_NUMBER + CWSL_CMD_NUMBER), "not enough template space\n");
    CI_ASSERT(CICWSL_TOTAL_TEMPLATE <= CWSL_MAX_TPL_NUM, "too many CWSL templates\n");
    cwsl_init_parameter->wait_time = 0;                                 // 使用默认值(20ms)
    cwsl_init_parameter->sg_reg_times = CWSL_REG_TIMES ;                
    cwsl_init_parameter->tpl_min_word_length = CWSL_TPL_MINWORD ;       
    cwsl_init_parameter->wakeup_threshold = CWSL_WAKEUP_THRESHOLD ;     
    cwsl_init_parameter->cmdword_threshold = CWSL_CMD_THRESHOLD ;       
    cwsl_init_parameter->reg_2times_flow_v2 = FOR_REG_2TIMES_FLOW_V2 ;

	#if FOR_REG_2TIMES_FLOW_V2
		// if(CWSL_REG_TIMES < 2)
		// {
		// 	cwsl_init_parameter->sg_reg_times = 2;
		// 	mprintf("CWSL_REG_TIMES %d must be 2 or 3\n",CWSL_REG_TIMES);
		// }
		// else if(CWSL_REG_TIMES > 3)
		// {
		// 	cwsl_init_parameter->sg_reg_times = 3;
		// 	mprintf("CWSL_REG_TIMES %d must be 2 or 3\n",CWSL_REG_TIMES);
		// }
        #if (CWSL_REG_TIMES < 2)
        #error "CWSL_REG_TIMES error,CWSL_REG_TIMES must be 2 or 3。\n"
        #endif
	#endif
    #if (CWSL_REG_TIMES > 3)
    #error "CWSL_REG_TIMES error,学习时 每个词最大支持重复说 3 遍。\n"
    #endif
    cwsl_init_parameter->reg_vad_level = CWSL_REG_VAD_LEVEL ;
    return CICWSL_TOTAL_TEMPLATE;
}

// 播放回调处理
static void cwsl_play_done_callback_default(cmd_handle_t cmd_handle)
{
}

// 播放回调处理, 开始录制模板
static void cwsl_play_done_callback_with_start_record(cmd_handle_t cmd_handle)
{
    int reservation = cwsl_official_take_continuation(
                            CWSL_OFFICIAL_CONTINUATION_START_RECORD);
    if (reservation > 0)
    {
        if (cwsl_official_start_recording_reserved() != 0)
        {
            cwsl_app_reset();
        }
        if (reservation == 1)
        {
            cwsl_arduino_finish_official_dispatch();
        }
    }
}

// 学习开始事件响应 函数内部不能阻塞，延时
int on_cwsl_reg_start(uint32_t cmd_id, uint16_t group_id, cwsl_word_type_t word_type)
{    
    cwsl_arduino_operation_t learning;
    int arduino_callback;
    TaskHandle_t sys_task_handle = cwsl_arduino_suspend_system_task();

    arduino_callback = cwsl_arduino_mark_learning_started(cmd_id,
                                                          group_id,
                                                          word_type,
                                                          &learning);
    if (arduino_callback == 2)
    {
        cwsl_arduino_terminal_t terminal;
        int terminal_status = cwsl_arduino_begin_terminal(
                                &sg_arduino_learning,
                                learning.generation,
                                CI_ARDUINO_CWSL_EVENT_LEARNING_CANCELLED,
                                0,
                                (uint8_t)CWSL_REG_ABORT,
                                1,
                                &terminal);
        if (terminal_status == 1)
        {
            cwsl_arduino_run_terminal(&terminal);
        }
        cwsl_arduino_resume_task(sys_task_handle);
        return 0;
    }
    if (arduino_callback < 0)
    {
        cwsl_arduino_resume_task(sys_task_handle);
        return 0;
    }

    if (arduino_callback == 0)
    {
        int official_start = cwsl_official_mark_learning_started(cmd_id,
                                                                  group_id,
                                                                  word_type);
        if (official_start == 2)
        {
            cwsl_arduino_lowlevel_reset();
            cwsl_arduino_finish_transition(1);
            cwsl_arduino_resume_task(sys_task_handle);
            return 0;
        }
        if (official_start < 0)
        {
            cwsl_arduino_resume_task(sys_task_handle);
            return 0;
        }
    }

    #if VOICE_UPLOAD_BY_UART
    voice_upload_enable();
    #endif
    set_state_enter_wakeup(EXIT_WAKEUP_TIME); // 更新退出唤醒时间
    ci_logdebug(LOG_CWSL, "==on_cwsl_reg_start\n");
    cwsl_reg_record_stop(); // 在提示音播放期间，关闭模板录制功能

    if (arduino_callback > 0)
    {
        cwsl_save_prev_info(cmd_id, group_id, word_type);
        cwsl_arduino_notify(&learning,
                            CI_ARDUINO_CWSL_EVENT_LEARNING_STARTED,
                            0,
                            0,
                            0);
        /* A synchronous user callback may cancel/reset this generation. */
        if (ciss_get(CI_SS_START_SLEEP_PROCESS) == 0 &&
            cwsl_arduino_learning_can_continue(learning.generation))
        {
            int ret = cwsl_arduino_start_direct_recording(
                                                    learning.generation);
            cwsl_arduino_finish_progress();
            cwsl_arduino_resume_task(sys_task_handle);
            return ret;
        }
        cwsl_arduino_finish_progress();
        cwsl_arduino_resume_task(sys_task_handle);
        return 0;
    }
    cwsl_save_prev_info(cmd_id,group_id,word_type);// 记录 学习的命令词 cmd_id 、 group_id;

    if (word_type == WAKEUP_WORD)
    {
        // 播放提示音 "开始学习唤醒词"
        if (cwsl_official_prompt_then_record(CWSL_REGISTRATION_WAKE,
                                              -1,
                                              false) != 0)
        {
            cwsl_app_reset();
        }
    }
    else
    {
        if (!cwsl_app.continus_flag) // 如果是连续学习，就不播报“开始学习”
        {
            prompt_play_by_cmd_id(CWSL_REGISTRATION_CMD, -1, cwsl_play_done_callback_default, false);
        }

        if (cwsl_official_prompt_then_record(
                        reg_cmd_list[cwsl_app.word_id].reg_play_id,
                        -1,
                        false) != 0)
        {
            cwsl_app_reset();
        }
    }
    cwsl_arduino_resume_task(sys_task_handle);
    return 0;
}

// 学习停止事件响应 函数内部不能阻塞，延时
int on_cwsl_reg_abort()
{
    cwsl_arduino_operation_t learning;
    cwsl_arduino_terminal_t terminal;
    TaskHandle_t sys_task_handle = cwsl_arduino_suspend_system_task();
    int terminal_status = -1;
    int official_abort = 0;
    int reset_performed = 0;

    ci_logdebug(LOG_CWSL, "==on_cwsl_reg_abort\n");
    if (cwsl_arduino_get_operation(&sg_arduino_learning, &learning) &&
        learning.cancel_requested)
    {
        taskENTER_CRITICAL();
        if (sg_arduino_learning.active &&
            sg_arduino_learning.generation == learning.generation &&
            sg_arduino_learning.record_requested)
        {
            sg_arduino_learning_rearm_blocked = 1;
        }
        taskEXIT_CRITICAL();
        terminal_status = cwsl_arduino_begin_terminal(
                            &sg_arduino_learning,
                            learning.generation,
                            CI_ARDUINO_CWSL_EVENT_LEARNING_CANCELLED,
                            0,
                            (uint8_t)CWSL_REG_ABORT,
                            1,
                            &terminal);
    }
    if (terminal_status > 0)
    {
        if (terminal_status == 1)
        {
            cwsl_arduino_run_terminal(&terminal);
        }
        cwsl_arduino_resume_task(sys_task_handle);
        return 0;
    }
    taskENTER_CRITICAL();
    if (!sg_arduino_learning.active &&
        sg_official.learning_active &&
        sg_official.started &&
        !sg_official.start_pending)
    {
        if (sg_official.record_requested)
        {
            sg_arduino_learning_rearm_blocked = 1;
        }
        cwsl_official_cancel_learning_locked();
        cwsl_app.app_mode = CWSL_APP_REC;
        sg_arduino_transition = 1;
        official_abort = 1;
    }
    taskEXIT_CRITICAL();
    if (!official_abort)
    {
        cwsl_arduino_resume_task(sys_task_handle);
        return 0;
    }
    reset_performed = cwsl_arduino_finish_transition(0);
    if (!reset_performed)
    {
        prompt_play_by_cmd_id(CWSL_EXIT_REGISTRATION,
                              -1,
                              cwsl_play_done_callback_default,
                              true);
    }
    cwsl_arduino_resume_task(sys_task_handle);
    return 0;
}

// 录制开始事件响应 函数内部不能阻塞，延时
int on_cwsl_record_start()
{
    /* Kept for vendor ABI compatibility.  This SDK does not reference the
     * callback; Arduino emits RecordingStarted at the successful queue point. */
    ci_logdebug(LOG_CWSL, "==on_cwsl_record_start\n");
    return 0;
}

static int cwsl_arduino_finish_learning(uint8_t event_type,
                                        uint8_t attempt,
                                        cwsl_reg_result_t result,
                                        uint32_t expected_generation,
                                        int notify_attempt)
{
    cwsl_arduino_terminal_t terminal;
    int terminal_status;

    terminal_status = cwsl_arduino_begin_terminal(
                            &sg_arduino_learning,
                            expected_generation,
                            event_type,
                            attempt,
                            (uint8_t)result,
                            1,
                            &terminal);
    if (terminal_status <= 0)
    {
        return 0;
    }

    /* Final vendor results are reserved before ATTEMPT_RESULT.  A synchronous
     * callback therefore cannot cancel an already persisted success. */
    if (notify_attempt)
    {
        cwsl_arduino_notify(&terminal.operation,
                            CI_ARDUINO_CWSL_EVENT_ATTEMPT_RESULT,
                            attempt,
                            (uint8_t)result,
                            0);
    }
    if (terminal_status == 1)
    {
        cwsl_arduino_run_terminal(&terminal);
    }
    return 0;
}

static void cwsl_arduino_notify_attempt(
                        const cwsl_arduino_operation_t *learning,
                        uint8_t attempt,
                        cwsl_reg_result_t result)
{
    cwsl_arduino_notify(learning,
                        CI_ARDUINO_CWSL_EVENT_ATTEMPT_RESULT,
                        attempt,
                        (uint8_t)result,
                        0);
}

static int cwsl_arduino_handle_record_end(int times,
                                          cwsl_reg_result_t result,
                                          int allow_retry,
                                          uint32_t expected_generation)
{
    cwsl_arduino_operation_t learning;

    if (!cwsl_arduino_get_operation(&sg_arduino_learning, &learning) ||
        !learning.started ||
        learning.generation != expected_generation)
    {
        return 0;
    }

    if (learning.cancel_requested)
    {
        return cwsl_arduino_finish_learning(
                    CI_ARDUINO_CWSL_EVENT_LEARNING_CANCELLED,
                    (uint8_t)times,
                    CWSL_REG_ABORT,
                    learning.generation,
                    0);
    }

    if (cwsl_arduino_manual_nn_end_is_quarantined())
    {
        return cwsl_arduino_finish_learning(
                    CI_ARDUINO_CWSL_EVENT_LEARNING_FAILED,
                    (uint8_t)times,
                    result,
                    learning.generation,
                    1);
    }

    if (result == CWSL_REG_FINISHED)
    {
        return cwsl_arduino_finish_learning(
                    CI_ARDUINO_CWSL_EVENT_LEARNING_SUCCEEDED,
                    (uint8_t)times,
                    result,
                    learning.generation,
                    1);
    }
    if (result == CWSL_REG_ABORT)
    {
        return cwsl_arduino_finish_learning(
                    CI_ARDUINO_CWSL_EVENT_LEARNING_CANCELLED,
                    (uint8_t)times,
                    result,
                    learning.generation,
                    1);
    }
    if (allow_retry &&
        (result == CWSL_REG_INVALID_DATA || MAX_LEARN_REPEAT_NUMBER > times))
    {
        if (!cwsl_arduino_begin_learning_progress(learning.generation,
                                                   1,
                                                   &learning))
        {
            return 0;
        }
        cwsl_arduino_notify_attempt(&learning, (uint8_t)times, result);
        /* User callbacks run synchronously above and may cancel/reset this
         * generation. Re-read it after notification before queuing a retry. */
        if (ciss_get(CI_SS_START_SLEEP_PROCESS) == 0 &&
            cwsl_arduino_learning_can_continue(learning.generation))
        {
            int ret = cwsl_arduino_start_direct_recording(
                                                    learning.generation);
            cwsl_arduino_finish_progress();
            return ret;
        }
        cwsl_arduino_finish_progress();
        return 0;
    }

    return cwsl_arduino_finish_learning(
                CI_ARDUINO_CWSL_EVENT_LEARNING_FAILED,
                (uint8_t)times,
                result,
                learning.generation,
                1);
}

// 学习阶段，获取被学习识别到的命令词 ID ,一般特殊场景用: 在同一个语言模型中 有A,B,C三个唤醒词，仅一个唤醒词有效，且ID 不一样；
// 例如 A是有效，B,C是无效（可以识别，但不播放，不发协议），在学习唤醒词 说了B或C唤醒词，返回值表示B或C的命令词ID 
// 应用获取命令词ID后，需自行实现逻辑，把B或C唤醒词 激活 当ID=0xFFFFFFFF,表示没有识别到默认词，是无有效ID
extern uint32_t get_othercmd_id(void);

// 录制结束事件响应 函数内部不能阻塞，延时 
int on_cwsl_record_end(int times, cwsl_reg_result_t result)
{
    // 学习阶段，获取被学习识别到的命令词 ID ,一般特殊场景用: 在同一个语言模型中 有A,B,C三个唤醒词，仅一个唤醒词有效，且ID 不一样；
    // 例如 A是有效，B,C是无效（可以识别，但不播放，不发协议），在学习唤醒词 说了B或C唤醒词，返回值表示B或C的命令词ID
    // 应用获取命令词ID后，需自行实现逻辑管理，把B或C唤醒词 激活  
    // 该接口只能在当前函数调用 ,当ID=0xFFFFFFFF,表示没有识别到默认词，是无有效ID
    uint32_t other_cmd_id = get_othercmd_id();
    cwsl_arduino_operation_t direct_learning;

    ci_logdebug(LOG_CWSL, "==on_cwsl_record_end %d,%d\n", times, result);
    TaskHandle_t sys_task_handle = cwsl_arduino_suspend_system_task();
    int record_owner = cwsl_arduino_take_record_end(&direct_learning);
    if (record_owner <= 0)
    {
        /* -1 is a callback quarantined after a forced reset.  Learning stays
         * fail-closed for this boot because the vendor callback has no epoch. */
        cwsl_arduino_resume_task(sys_task_handle);
        return 0;
    }
    #if VOICE_UPLOAD_BY_UART
    voice_upload_disable();
    #endif
    if (ciss_get(CI_SS_START_SLEEP_PROCESS) == 0)
    {
        if (record_owner == 1)
        {
            int direct_result;
            set_state_enter_wakeup(EXIT_WAKEUP_TIME);
            sys_ignore_exit_msg_in_queue();
            cwsl_update_prev_info();
            /* Keep the sleep-transition task suspended until the direct
             * operation either queues a verified retry or reaches a
             * terminal state. */
            direct_result = cwsl_arduino_handle_record_end(
                                                times,
                                                result,
                                                1,
                                                direct_learning.generation);
            cwsl_arduino_finish_record_end();
            cwsl_arduino_resume_task(sys_task_handle);
            return direct_result;
        }

        if (record_owner == 2)
        {
            if (cwsl_arduino_manual_nn_end_is_quarantined())
            {
                /* The application-generated end and the producer's natural
                 * end cannot be ordered.  Terminate this official session
                 * without starting another untagged recording. */
                cwsl_app_reset();
                cwsl_arduino_finish_record_end();
                cwsl_arduino_resume_task(sys_task_handle);
                return 0;
            }
            if (!cwsl_arduino_begin_official_dispatch())
            {
                /* Another owner may have been preempted after queueing this
                 * callback.  Preserve safety and let that owner run reset. */
                cwsl_app_reset();
                cwsl_arduino_finish_record_end();
                cwsl_arduino_resume_task(sys_task_handle);
                return 0;
            }
            taskENTER_CRITICAL();
            int official_current = sg_official.learning_active &&
                                   sg_official.started &&
                                   !sg_official.start_pending &&
                                   cwsl_app.app_mode == CWSL_APP_REG;
            taskEXIT_CRITICAL();
            if (!official_current)
            {
                cwsl_arduino_finish_official_dispatch();
                cwsl_arduino_finish_record_end();
                cwsl_arduino_resume_task(sys_task_handle);
                return 0;
            }
            set_state_enter_wakeup(EXIT_WAKEUP_TIME);
            sys_ignore_exit_msg_in_queue();
            cwsl_update_prev_info(); //本次记录学习的 cmd_id 、 group_id 已处理完（学习完成或失败);

            if (CWSL_RECORD_SUCCESSED == result)
            {
                if (MAX_LEARN_REPEAT_NUMBER > times)
                {
                    // if (times == 1)
                    {
                        if (cwsl_official_prompt_then_record(CWSL_SPEAK_AGAIN,
                                                             -1,
                                                             true) != 0)
                        {
                            cwsl_app_reset();
                        }
                    }
                    // else
                    // {
                    //     prompt_play_by_cmd_id(CWSL_DATA_ENTERY_SUCCESSFUL, -1, cwsl_play_done_callback_with_start_record, true);
                    // }
                }
                else
                {
                    // 学习次数超过上限，自动退出
                    prompt_play_by_cmd_id(CWSL_REG_FAILED, -1, cwsl_play_done_callback_default, true);
                    // 转回识别模式
                    cwsl_app_reset();
                }
            }
            else if ((CWSL_RECORD_FAILED == result ) || ( CWSL_RECORD_FAILED_BY_DEFAULTCMD == result ))
            {
                cicwsl_func_index  cmd_id_for_play = CWSL_DATA_ENTERY_FAILED;
                if(CWSL_RECORD_FAILED_BY_DEFAULTCMD == result)
                {
                    cmd_id_for_play = CWSL_REG_FAILED_DEFAULT_CMD_CONFLICT;
                }
                if((2 == CWSL_REG_TIMES)||(3 == CWSL_REG_TIMES))
                {// 
                    #if FOR_REG_2TIMES_FLOW_V2
                    if (MAX_LEARN_REPEAT_NUMBER > times)
                    {   // 播报 "学习失败，请再说一次" 或者 "与默认指令冲突，请换种说法"
                        //新流程 2遍情况下  和第一次不一致，学习失败
                        if (cwsl_official_prompt_then_record(cmd_id_for_play,
                                                             -1,
                                                             true) != 0)
                        {
                            cwsl_app_reset();
                        }
                    }
                    else
                    {
                        // 学习次数超过上限，自动退出
                        prompt_play_by_cmd_id(CWSL_REG_FAILED, -1, cwsl_play_done_callback_default, true);
                        // 转回识别模式
                        cwsl_app_reset();
                    }
                    #else
                    if(2 == CWSL_REG_TIMES)
                    {
                        //2遍情况下，学习失败，提示重新学习该词
                        if(CWSL_RECORD_FAILED_BY_DEFAULTCMD == result)
                        {
                            prompt_play_by_cmd_id(CWSL_REG_FAILED_DEFAULT_CMD_CONFLICT, 1, cwsl_play_done_callback_default, false);
                        }
                        else
                        {
                            //播报 "学习失败，请重新学习 XX 指令"
                            if (cwsl_official_prompt_then_record(
                                                        CWSL_REG_FAILED,
                                                        1,
                                                        false) != 0)
                            {
                                cwsl_app_reset();
                            }
                        }
                        //重新学习
                        extern void cwsl_app_reg_word_restart();
                        taskENTER_CRITICAL();
                        int restart_current = sg_official.learning_active &&
                                              sg_official.started &&
                                              !sg_arduino_reset_pending;
                        taskEXIT_CRITICAL();
                        if (restart_current)
                        {
                            cwsl_app_reg_word_restart();
                        }
                    }
                    else
                    {//not this func
                    }
                    #endif 
                }
                else
                {
                    if (MAX_LEARN_REPEAT_NUMBER > times)
                    {   // 播报 "学习失败，请再说一次" 或者 "与默认指令冲突，请换种说法"
                        if (cwsl_official_prompt_then_record(cmd_id_for_play,
                                                             -1,
                                                             true) != 0)
                        {
                            cwsl_app_reset();
                        }
                    }
                    else
                    {
                        // 学习次数超过上限，自动退出
                        prompt_play_by_cmd_id(CWSL_REG_FAILED, -1, cwsl_play_done_callback_default, true);
                        // 转回识别模式
                        cwsl_app_reset();
                    }
                }
            }
            else if (CWSL_REG_FINISHED == result)
            {
                prompt_play_by_cmd_id(CWSL_REGISTRATION_SUCCESSFUL, -1, cwsl_play_done_callback_default, true);

                int next_cmd_index = get_next_reg_cmd_word_index(cwsl_app.word_id);

                if (CMD_WORD == cwsl_app.word_type && CWSL_CMD_NUMBER > cwsl_tm_get_reg_tpl_number(CMD_WORD) && (cwsl_app.word_id < (CWSL_CMD_NUMBER - 1)) && (next_cmd_index != -1))
                {
                    //为了配合学习下一个的逻辑，加了判断条件(cwsl_app.word_id < (CWSL_CMD_NUMBER - 1))且(next_cmd_index != -1)
                    cwsl_set_prev_appwordid(cwsl_app.word_id);
                    cwsl_app.word_id = next_cmd_index;
                    cwsl_app.continus_flag = 1;   
                
                    // 指定是连续学习，用于简化播报提示音
                    if (cwsl_official_submit_learning_reserved(
                                reg_cmd_list[next_cmd_index].reg_cmd_id,
                                0,
                                CMD_WORD) != 0)
                    {
                        cwsl_app_reset();
                    }
                }
                else
                {
                    // 转回识别模式
                    cwsl_app_reset();
                }
            }
            else if (CWSL_NOT_ENOUGH_FRAME == result)
            {
                if (MAX_LEARN_REPEAT_NUMBER > times)
                {
                    if (cwsl_official_prompt_then_record(CWSL_TOO_SHORT,
                                                         -1,
                                                         true) != 0)
                    {
                        cwsl_app_reset();
                    }
                }
                else
                {
                    // 学习次数超过上限，自动退出
                    prompt_play_by_cmd_id(CWSL_REG_FAILED, -1, cwsl_play_done_callback_default, true);
                    // 转回识别模式
                    cwsl_app_reset();
                }
            }
            else if (CWSL_REG_INVALID_DATA == result)
            {
                if (cwsl_official_start_recording_reserved() != 0)
                {
                    cwsl_app_reset();
                }
            }
            cwsl_arduino_finish_official_dispatch();
            cwsl_arduino_finish_record_end();
            cwsl_arduino_resume_task(sys_task_handle);
        }
        else
        {
            cwsl_arduino_finish_record_end();
            cwsl_arduino_resume_task(sys_task_handle);
        }
    }
    else
    {
        /* The sleep owner will reset after this callback.  Preserve evidence
         * that the untagged producer may still emit another type-6 first. */
        taskENTER_CRITICAL();
        sg_arduino_learning_rearm_blocked = 1;
        taskEXIT_CRITICAL();
        if (record_owner == 1)
        {
            int direct_result;
            /* Never start another recording once the sleep transition began. */
            cwsl_update_prev_info();
            direct_result = cwsl_arduino_handle_record_end(
                                                times,
                                                result,
                                                0,
                                                direct_learning.generation);
            cwsl_arduino_finish_record_end();
            cwsl_arduino_resume_task(sys_task_handle);
            return direct_result;
        }
        if (!cwsl_arduino_begin_official_dispatch())
        {
            cwsl_app_reset();
            cwsl_arduino_finish_record_end();
            cwsl_arduino_resume_task(sys_task_handle);
            return 0;
        }
        taskENTER_CRITICAL();
        int official_current = sg_official.learning_active &&
                               sg_official.started &&
                               !sg_official.start_pending;
        taskEXIT_CRITICAL();
        if (!official_current)
        {
            cwsl_arduino_finish_official_dispatch();
            cwsl_arduino_finish_record_end();
            cwsl_arduino_resume_task(sys_task_handle);
            return 0;
        }
        // 系统已进入唤醒词监听状态
        // 转回识别模式
        cwsl_app_reset();
        cwsl_arduino_finish_official_dispatch();
        cwsl_arduino_finish_record_end();
        cwsl_arduino_resume_task(sys_task_handle);
    }
    return 0;
}

// 删除模板成功事件响应 函数内部不能阻塞，延时
int on_cwsl_delete_successed()
{
    cwsl_arduino_operation_t delete_operation;
    cwsl_arduino_terminal_t terminal;
    TaskHandle_t sys_task_handle = cwsl_arduino_suspend_system_task();
    int terminal_status = 0;
    int official_success = 0;
    int reset_requested = 0;
    int reset_performed = 0;

    if (cwsl_arduino_get_operation(&sg_arduino_delete,
                                    &delete_operation))
    {
        terminal_status = cwsl_arduino_begin_terminal(
                            &sg_arduino_delete,
                            delete_operation.generation,
                            CI_ARDUINO_CWSL_EVENT_DELETE_SUCCEEDED,
                            0,
                            0,
                            0,
                            &terminal);
    }
    if (terminal_status > 0)
    {
        if (terminal_status == 1)
        {
            cwsl_arduino_run_terminal(&terminal);
        }
        cwsl_arduino_resume_task(sys_task_handle);
        return 0;
    }
    if (terminal_status < 0)
    {
        cwsl_arduino_resume_task(sys_task_handle);
        return 0;
    }

    taskENTER_CRITICAL();
    if (!sg_arduino_delete.active && sg_official.delete_pending)
    {
        sg_official.delete_pending = 0;
        sg_official.delete_mode_active = 0;
        cwsl_app.app_mode = CWSL_APP_REC;
        reset_requested = sg_arduino_reset_pending;
        sg_arduino_reset_pending = 0;
        sg_arduino_transition = 1;
        official_success = 1;
    }
    taskEXIT_CRITICAL();
    if (!official_success)
    {
        cwsl_arduino_resume_task(sys_task_handle);
        return 0;
    }
    if (reset_requested)
    {
        cwsl_arduino_lowlevel_reset();
    }
    reset_performed = cwsl_arduino_finish_transition(reset_requested);
    if (!reset_performed)
    {
        prompt_play_by_cmd_id(CWSL_DELETE_SUCCESSFUL,
                              -1,
                              cwsl_play_done_callback_default,
                              true);
    }
    cwsl_arduino_resume_task(sys_task_handle);
    return 0;
}

// 识别成功事件响应 函数内部不能阻塞，延时
int on_cwsl_rgz_successed(uint16_t cmd_id, uint32_t distance)
{
    cmd_handle_t cmd_handle = cmd_info_find_command_by_id(cmd_id);
    cwsl_arduino_operation_t recognition = {
        .active = 1,
        .cancel_requested = 0,
        .command_id = cmd_id,
        .group_id = UINT16_MAX,
        .word_type = CMD_WORD,
    };

    if (cmd_handle != (cmd_handle_t)INVALID_HANDLE && cmd_handle != NULL)
    {
        recognition.word_type = cmd_info_is_wakeup_word(cmd_handle)
                                    ? WAKEUP_WORD
                                    : CMD_WORD;
    }
    else if (cwsl_arduino_template_exists(cmd_id,
                                           UINT16_MAX,
                                           WAKEUP_WORD))
    {
        recognition.word_type = WAKEUP_WORD;
    }

    cwsl_arduino_notify(&recognition,
                        CI_ARDUINO_CWSL_EVENT_RECOGNIZED,
                        0,
                        0,
                        distance);

    if (cmd_handle == (cmd_handle_t)INVALID_HANDLE || cmd_handle == NULL)
    {
        ci_logerr(LOG_CWSL, "CWSL result has no cmd_info entry: %d\n", cmd_id);
        return -1;
    }

    /* CWSL has no ASR PCM/score/frame values; publish explicit neutral values. */
    sys_msg_t send_msg = {0};
    send_msg.msg_type = SYS_MSG_TYPE_ASR;
    send_msg.msg_data.asr_data.asr_status = MSG_CWSL_STATUS_GOOD_RESULT;
    send_msg.msg_data.asr_data.asr_cmd_handle = (uint32_t)cmd_handle;
    send_msg.msg_data.asr_data.asr_pcm_base_addr = 0;
    send_msg.msg_data.asr_data.asr_score = 0;
    send_msg.msg_data.asr_data.asr_frames = 0;
    send_msg_to_sys_task(&send_msg, NULL);
    ci_logdebug(LOG_CWSL, "cwsl result: %d, %d\n", cmd_id, distance);
    return 0;
}

// 查找下一个需要学习的命令词索引，用于实现“从上次中断处开始学习”.
static int get_next_reg_cmd_word_index(int word_id)
{
    int ret = -1;

    // 查找学习的命令词
    uint8_t cmd_word_tm_index[CWSL_CMD_NUMBER];
    int cmd_tpl_count = cwsl_tm_get_words_index(cmd_word_tm_index, CWSL_CMD_NUMBER, -1, CMD_WORD);
    
    //为了配合学习下一个的逻辑，加了条件int i = word_id
    for (int i = word_id; i < CWSL_CMD_NUMBER; i++)
    {
        int find_flag = 0;
        for (int j = 0; j < cmd_tpl_count; j++)
        {
            if (cwsl_tm_get_tpl_cmd_id_by_index(cmd_word_tm_index[j]) == reg_cmd_list[i].reg_cmd_id)
            {
                find_flag = 1;
                break;
            }
        }
        if (!find_flag)
        {
            ret = i;
            return ret;
        }
    }
    return ret;
}



////cwsl API///////////////////////////////////////////////

// 学习唤醒词
void cwsl_app_reg_word(cwsl_word_type_t word_type)
{
    if (word_type != WAKEUP_WORD && word_type != CMD_WORD)
    {
        return;
    }
    if (cwsl_app_claim_official_mode(CWSL_APP_REG))
    {
        // 清除 前一次学习的信息
        cwsl_clear_prev_info();
        if (WAKEUP_WORD == word_type)
        {
            if ( ( CWSL_WAKEUP_NUMBER > cwsl_tm_get_reg_tpl_number(WAKEUP_WORD) ) && (cwsl_tm_get_left_tpl_number() > 0))
            {
                cwsl_app.word_type = WAKEUP_WORD;
                if (cwsl_official_submit_learning_reserved(WAKE_UP_ID,
                                                            0,
                                                            WAKEUP_WORD) != 0)
                {
                    cwsl_app_release_official_mode();
                }
            }
            else
            {
                cwsl_app_release_official_mode();
                // 唤醒词已经学习满了
                prompt_play_by_cmd_id(CWSL_TEMPLATE_FULL, -1, cwsl_play_done_callback_default, true);
            }
        }
        else if (CMD_WORD == word_type)
        {
            if ((CWSL_CMD_NUMBER > cwsl_tm_get_reg_tpl_number(CMD_WORD)) && (cwsl_tm_get_left_tpl_number() > 0) )
            {
                int next_cmd_index = get_next_reg_cmd_word_index(0);
                if (next_cmd_index < 0)
                {
                    cwsl_app_release_official_mode();
                    return;
                }
                cwsl_app.word_id = next_cmd_index;
                cwsl_app.word_type = CMD_WORD;
                if (cwsl_official_submit_learning_reserved(
                                reg_cmd_list[next_cmd_index].reg_cmd_id,
                                0,
                                CMD_WORD) != 0)
                {
                    cwsl_app_release_official_mode();
                    return;
                }
                cwsl_app.continus_flag = 0;
            }
            else
            {
                cwsl_app_release_official_mode();
                // 命令词已经学习满了
                prompt_play_by_cmd_id(CWSL_TEMPLATE_FULL, -1, cwsl_play_done_callback_default, true);
            }
        }
    }
}

void cwsl_app_reg_next_callback()
{
    int reservation = cwsl_official_take_continuation(
                            CWSL_OFFICIAL_CONTINUATION_NEXT_WORD);
    if (reservation > 0)
    {
        cwsl_app.word_type = CMD_WORD;
        cwsl_app.continus_flag = 1;
        if (cwsl_official_submit_learning_reserved(
                            reg_cmd_list[cwsl_app.word_id].reg_cmd_id,
                            0,
                            CMD_WORD) != 0)
        {
            cwsl_app_reset();
        }
        if (reservation == 1)
        {
            cwsl_arduino_finish_official_dispatch();
        }
    }
}

void cwsl_app_reg_next(void)
{
    taskENTER_CRITICAL();
    int can_advance = cwsl_app.app_mode == CWSL_APP_REG &&
                      cwsl_app.word_type == CMD_WORD &&
                      sg_official.learning_active &&
                      sg_official.started &&
                      !sg_official.record_requested &&
                      !sg_official.start_pending &&
                      !cwsl_official_has_continuation_locked() &&
                      !sg_arduino_learning_rearm_blocked &&
                      !sg_arduino_reset_pending;
    taskEXIT_CRITICAL();
    if (can_advance)
    {
        cwsl_exit_reg_word();
        int next_cmd_index = (cwsl_app.word_id < (CWSL_CMD_NUMBER - 1)) ? get_next_reg_cmd_word_index(cwsl_app.word_id + 1) : -1;
        if(-1 == next_cmd_index)
        {   
            //未找到下一个，所以播报 “学习完成”
            prompt_play_by_cmd_id(CWSL_REGISTRATION_ALL, -1, cwsl_play_done_callback_default, true);
            // 转回识别模式
			cwsl_app_reset();
        }
        else
        {
            cwsl_app.word_id = next_cmd_index;
            if (cwsl_official_arm_continuation(
                            CWSL_OFFICIAL_CONTINUATION_NEXT_WORD))
            {
                uint32_t ret = prompt_play_by_cmd_id(CWSL_REGISTRATION_NEXT,
                                                      -1,
                                                      cwsl_app_reg_next_callback,
                                                      true);
                if (ret != 0)
                {
                    taskENTER_CRITICAL();
                    sg_official.continuation =
                                        CWSL_OFFICIAL_CONTINUATION_NONE;
                    sg_official.continuation_cancelled = 0;
                    taskEXIT_CRITICAL();
                    cwsl_app_reset();
                }
            }
            else
            {
                cwsl_app_reset();
            }
        }
    }
}

// 重新学习
void cwsl_app_reg_word_restart()
{
    taskENTER_CRITICAL();
    int can_restart = cwsl_app.app_mode == CWSL_APP_REG &&
                      sg_official.learning_active &&
                      sg_official.started &&
                      !sg_official.record_requested &&
                      !sg_official.start_pending &&
                      !sg_arduino_learning_rearm_blocked &&
                      !sg_arduino_reset_pending;
    taskEXIT_CRITICAL();
    if (can_restart)
    {
        cwsl_set_reg_restart_flag();
        cwsl_app_delete_prev_word();
        cwsl_reg_restart();
    }
}

// 播放回调处理, 退出学习
static void cwsl_play_done_callback_with_exit_reg(cmd_handle_t cmd_handle)
{
    // cwsl_exit_reg_word();
}

// 退出学习
void cwsl_app_exit_reg()
{
    taskENTER_CRITICAL();
    int can_restart = cwsl_app.app_mode == CWSL_APP_REG &&
                      sg_official.learning_active &&
                      sg_official.started &&
                      !sg_official.start_pending &&
                      !sg_arduino_reset_pending;
    taskEXIT_CRITICAL();
    if (can_restart)
    {
        // cwsl_reg_record_stop();
        cwsl_exit_reg_word();
        cwsl_app_reset();
        prompt_play_by_cmd_id(CWSL_EXIT_REGISTRATION, -1, cwsl_play_done_callback_with_exit_reg, true);
    }
}

// 进入删除模式
void cwsl_app_enter_delete_mode()
{
    if (cwsl_app_claim_official_mode(CWSL_APP_DEL))
    {
        prompt_play_by_cmd_id(CWSL_DELETE_FUNC, -1, cwsl_play_done_callback_default, true);
    }
}

// 退出删除模式
void cwsl_app_exit_delete_mode()
{
    if (cwsl_app.app_mode == CWSL_APP_DEL)
    {
        cwsl_app_reset();
        prompt_play_by_cmd_id(CWSL_EXIT_DELETE, -1, cwsl_play_done_callback_default, true);
    }
}

// 删除指定类型模板
// cmd_id: 指定要删除的命令词ID, 传入-1为通配符，忽略此项
// group_id: 指定要删除的命令词分组号, 传入-1为通配符，忽略此项
// word_type: 指定要删除的命令词类型，传入-1为通配符，忽略此项
void cwsl_app_delete_word(uint32_t cmd_id, uint16_t group_id, cwsl_word_type_t word_type)
{
    if (cwsl_app.app_mode == CWSL_APP_DEL)
    {
        cwsl_official_submit_delete_reserved(cmd_id, group_id, word_type);
    }
}

// cwsl_manage模块复位，用于系统退出唤醒状态时调用
int cwsl_app_reset()
{
    cwsl_arduino_reset_context_t reset_context;
    int run_reset = 0;

    taskENTER_CRITICAL();
    /* Linearize the reset request with record ownership.  record_requested is
     * consumed at callback entry, so the in-progress bit is the only durable
     * evidence while a synchronous user callback is running. */
    if (sg_arduino_record_end_in_progress ||
        (sg_arduino_learning.active &&
         sg_arduino_learning.record_requested) ||
        (sg_official.learning_active && sg_official.record_requested))
    {
        sg_arduino_learning_rearm_blocked = 1;
    }
    if (sg_arduino_submission_in_progress ||
        sg_arduino_official_dispatch ||
        sg_arduino_progress_in_progress ||
        sg_arduino_transition)
    {
        /* The owner will hand this reservation directly to reset, without an
         * interval in which another direct or official operation can enter. */
        sg_arduino_reset_pending = 1;
    }
    else
    {
        run_reset = cwsl_arduino_begin_reset_locked(&reset_context);
    }
    taskEXIT_CRITICAL();

    if (run_reset)
    {
        cwsl_arduino_run_reset(&reset_context);
    }
    return 0;
}



////cwsl process ASR message///////////////////////////////////////////////
/**
 * @brief 命令词自学习消息处理函数
 * 
 * @param asr_msg ASR识别结果消息
 * @param cmd_handle 命令词handle
 * @param cmd_id 命令词ID
 * @retval 1 数据有效,消息已处理
 * @retval 0 数据无效,消息未处理
 */

uint32_t cwsl_app_process_asr_msg(sys_msg_asr_data_t *asr_msg, cmd_handle_t *cmd_handle, uint16_t cmd_id)
{
    uint32_t ret = 0;

    /* Atomically reserve the entire official dispatch.  Without this handoff a
     * direct task could claim REG/DEL after the busy check but before switch. */
    if (!cwsl_arduino_begin_official_dispatch())
    {
        /* This result has no producer epoch and may belong to the previous
         * attempt.  A reserved operation owns the boundary, so consume it
         * without mutating the next attempt or adding a second type-6. */
        return 2;
    }
   
    switch(cmd_id)
    {
    case CWSL_REGISTRATION_WAKE://删除和学习不能连续调用
        if (ciss_get(CI_SS_START_SLEEP_PROCESS) == 0)
        {
            #if USE_AEC_MODULE
            ciss_set(CI_SS_CWSL_AEC_MUTE_STATE,CI_SS_CWSL_AEC_MUTE_ON);
            #endif
            set_state_enter_wakeup(EXIT_WAKEUP_TIME); // 更新退出唤醒时间
            sys_ignore_exit_msg_in_queue();
			cwsl_app_reg_word(WAKEUP_WORD);
            cwsl_reg_check_other_asrcmd(cmd_id,1);//记录默认命令词，检测是否与学习的词条
            
        }
        ret = 2;
        break;
    case CWSL_REGISTRATION_CMD://删除和学习不能连续调用
        if (ciss_get(CI_SS_START_SLEEP_PROCESS) == 0)
        {
            #if USE_AEC_MODULE
            ciss_set(CI_SS_CWSL_AEC_MUTE_STATE,CI_SS_CWSL_AEC_MUTE_ON);
            #endif
            set_state_enter_wakeup(EXIT_WAKEUP_TIME); // 更新退出唤醒时间
            sys_ignore_exit_msg_in_queue();
			cwsl_app_reg_word(CMD_WORD);
            cwsl_reg_check_other_asrcmd(cmd_id,1);//记录默认命令词，检测是否与学习的词条
            
        }
        ret = 2;
        break;
    case CWSL_REGISTRATION_NEXT:
        if (ciss_get(CI_SS_START_SLEEP_PROCESS) == 0)
        {
            set_state_enter_wakeup(EXIT_WAKEUP_TIME); // 更新退出唤醒时间
            sys_ignore_exit_msg_in_queue();
			cwsl_app_reg_next();
            cwsl_reg_check_other_asrcmd(cmd_id,1);//记录默认命令词，检测是否与学习的词条
            
        }
        ret = 2;
        break;
    case CWSL_REGISTER_AGAIN:
        if (ciss_get(CI_SS_START_SLEEP_PROCESS) == 0)
        {
            set_state_enter_wakeup(EXIT_WAKEUP_TIME); // 更新退出唤醒时间
            sys_ignore_exit_msg_in_queue();
            cwsl_app_reg_word_restart();
        }
        ret = 2;
        break;
    case CWSL_EXIT_REGISTRATION:
        if (ciss_get(CI_SS_START_SLEEP_PROCESS) == 0)
        {
            #if USE_AEC_MODULE
            ciss_set(CI_SS_CWSL_AEC_MUTE_STATE,CI_SS_CWSL_AEC_MUTE_OFF);
            #endif
            set_state_enter_wakeup(EXIT_WAKEUP_TIME); // 更新退出唤醒时间
            sys_ignore_exit_msg_in_queue();
            cwsl_app_exit_reg();
        }
        ret = 2;
        break;
    case CWSL_DELETE_FUNC:
        if (ciss_get(CI_SS_START_SLEEP_PROCESS) == 0)
        {
            set_state_enter_wakeup(EXIT_WAKEUP_TIME); // 更新退出唤醒时间
            sys_ignore_exit_msg_in_queue();
            cwsl_app_enter_delete_mode();
            cwsl_reg_check_other_asrcmd(cmd_id,1);//记录默认命令词，检测是否与学习的词条
        }
        ret = 2;
        break;
    case CWSL_EXIT_DELETE:
        if (ciss_get(CI_SS_START_SLEEP_PROCESS) == 0)
        {
            set_state_enter_wakeup(EXIT_WAKEUP_TIME); // 更新退出唤醒时间
            sys_ignore_exit_msg_in_queue();
            cwsl_app_exit_delete_mode();
            cwsl_reg_check_other_asrcmd(cmd_id,1);//记录默认命令词，检测是否与学习的词条
        }
        ret = 2;
        break;
    case CWSL_DELETE_WAKE://删除和学习不能连续调用
        if (ciss_get(CI_SS_START_SLEEP_PROCESS) == 0)
        {
            set_state_enter_wakeup(EXIT_WAKEUP_TIME); // 更新退出唤醒时间
            sys_ignore_exit_msg_in_queue();
            cwsl_app_delete_word((uint32_t)-1, (uint16_t)-1, WAKEUP_WORD);
            cwsl_reg_check_other_asrcmd(cmd_id,1);//记录默认命令词，检测是否与学习的词条
        }
        ret = 2;
        break;
    case CWSL_DELETE_CMD://删除和学习不能连续调用
        if (ciss_get(CI_SS_START_SLEEP_PROCESS) == 0)
        {
            set_state_enter_wakeup(EXIT_WAKEUP_TIME); // 更新退出唤醒时间
            sys_ignore_exit_msg_in_queue();
            cwsl_app_delete_word((uint32_t)-1, (uint16_t)-1, CMD_WORD);
            cwsl_reg_check_other_asrcmd(cmd_id,1);//记录默认命令词，检测是否与学习的词条
        }
        ret = 2;
        break;
    case CWSL_DELETE_ALL://删除和学习不能连续调用
        if (ciss_get(CI_SS_START_SLEEP_PROCESS) == 0)
        {
            set_state_enter_wakeup(EXIT_WAKEUP_TIME); // 更新退出唤醒时间
            sys_ignore_exit_msg_in_queue();
            cwsl_app_delete_word((uint32_t)-1, (uint16_t)-1, ALL_WORD);
            cwsl_reg_check_other_asrcmd(cmd_id,1);//记录默认命令词，检测是否与学习的词条
        }
        ret = 2;
        break;
    default:
        {
            int manual_end_action = cwsl_arduino_prepare_manual_nn_end();
            if (manual_end_action == 1)
            {
                /* Mutate the vendor conflict state only while this exact
                 * recording still owns the untagged manual end. */
                cwsl_reg_check_other_asrcmd(cmd_id,1);
                send_nn_end_msg_to_cwsl(NULL, 0);
            }
            if (manual_end_action > 0)
            {
                ret = 2;
            }
        }
        break;
    }
   
    cwsl_arduino_finish_official_dispatch();
    return ret;
}




#endif
