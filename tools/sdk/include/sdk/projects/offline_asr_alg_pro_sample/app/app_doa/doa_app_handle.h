#ifndef __DOA_APP_HANDLE_H__
#define __DOA_APP_HANDLE_H__

#include "ci_log.h"
#include <stdint.h>
#include <stdbool.h>
#include "user_config.h"
#include "prompt_player.h"
#if !SIMPLE_AUDIO_PLAYER_ENABLE
#include "audio_play_process.h"
#else
#include "simple_audio_player.h"
#endif
#include "ci130x_gpio.h"
#include "FreeRTOS.h"
#include "system_msg_deal.h"
#include "ci130x_dpmu.h"
#include "ci_nvdata_manage.h"
#include "remote_api_for_host.h"
void doa_out_result_hand_task(void *p);
#endif