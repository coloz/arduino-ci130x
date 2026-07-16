#ifndef __CIAS_RECORD_DEMO_H__
#define __CIAS_RECORD_DEMO_H__
#include "user_config.h"
#include <stdbool.h>
#include "FreeRTOS.h"
#include "task.h"
#include "queue.h"
#include "semphr.h"
#include "stream_buffer.h"
#include "codec_manager.h"
#include "ci130x_gpio.h"
#include "ci130x_dpmu.h"
#include "ci130x_iis.h"
#include "ci130x_scu.h"
//录音+flash存储+播放demo
typedef struct 
{
    StreamBufferHandle_t record_stream_buffer;    //pcm压缩数据buf
    uint8_t  record_work_state;        //1-开始录音 2-录音中
    uint8_t  play_work_state;          //1-开始播放 2-播放中  
    uint32_t record_cur_offset;        //录音偏移
    uint32_t  play_cur_offset;          //播放偏移
}RecordPlayTestTypedef;
typedef enum
{
    RECORD_STATE_IDLE,
    RECORD_STATE_START,
    RECORD_STATE_RUNING,
    PLAY_STATE_IDLE,
    PLAY_STATE_START,
    PLAY_STATE_RUNING
}RecordPlayState_t;

void play_from_flash_task(void);
void record_to_flash_task(void);
bool record_play_test_init(void);
#endif  //__CIAS_RECORD_DEMO_H__