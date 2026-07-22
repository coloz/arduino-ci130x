#include "user_config.h"
#if SIMPLE_AUDIO_PLAYER_ENABLE
#include <string.h>
#include "user_config.h"
#include "simple_audio_player.h"
#include "FreeRTOS.h"
#include "queue.h"
#include "stream_buffer.h"
#include "romlib_runtime.h"
#include "codec_manager.h"
#include "ci_log_config.h"
#include "ci_log.h"
#include "board.h"
#include "flash_rw_process.h"
#include "player_data_fetcher.h"
#include "status_share.h"

#define SAP_SOURCE_DATA_BUF_SIZE (512)

#define SAP_MAX_PCM_FRAME_SIZE (2048)
#define SAP_MSG_QUEUE_LENGTH 10
#define SAP_PCM_BUF_COUNT 4

typedef enum
{
    SAP_MSG_START_PLAY = 0, // Start playing. This message is always valid. If the sap is not in the SAP_STATE_IDLE state,the last play work will be stoped,and then begin a play work for this message.
    SAP_MSG_STOP_PLAY,      // Force stop play. This message is valid only when the sap is in the "SAP_STATE_START" or "SAP_STATE_PLAY" state.
} sap_msg_id_t;

typedef struct
{
    sap_msg_id_t msg_id;
    uint32_t data_addr;
    uint32_t data_size;
    uint32_t request_block_size;
    uint32_t fetcher_buffer_size;
    sap_src_type_t src_type;
    SAP_PLAY_END_CALLBACK play_end_callback;
    SAP_PLAY_DATA_REQUEST_CALLBACK data_request_callback;
    char *fmt;
} sap_msg_t;

typedef struct
{
    QueueHandle_t msg_queue;
    sap_status_t state;
    // uint32_t data_addr;
    SAP_PLAY_END_CALLBACK play_end_callback;
    void *decoder_handle;
    audio_decoder_interface_t *decoder_chain;
    audio_decoder_interface_t *cur_decoder;
    uint32_t sample_rate;
    uint32_t output_samples;
    uint32_t pcm_buffer_size;
    short *pcm_buffer;
    // uint32_t source_file_bytes_left;
    uint32_t source_buf_bytes_left;
    uint32_t source_buf_offset;
    uint32_t decode_frame_count;
    // uint32_t source_frame_size;
    uint32_t frame_pcm_size;
    // uint32_t total_pcm_size;
    uint8_t source_data_buf[SAP_SOURCE_DATA_BUF_SIZE];
    uint8_t channels;
} sap_info_t;

sap_info_t sap_info = {
    .msg_queue = NULL,
    .state = SAP_STATE_IDLE,
    .pcm_buffer = NULL,
    // .source_file_bytes_left = 0,
    .source_buf_bytes_left = 0,
    .decoder_chain = NULL,
};

sap_src_type_t src_type; // 数据源类型
static audio_play_state_t audio_play_state = AUDIO_PLAY_STATE_IDLE;
static void task_simple_audio_player(void *pvParameters);
static void sap_send_msg(sap_msg_t *msg, BaseType_t *xHigherPriorityTaskWoken);

/**
 * @brief Initialize the simple audio player module.
 *
 * @return int 1: successed; not 1: failed.
 */
int sap_init(void)
{
    return (int)xTaskCreate(task_simple_audio_player, "simple-audio-player", 512, 0, 4, NULL);
}

/**
 * @brief Start playing.
 *
 * @param data_addr uint32_t It's used to specifiy the address of the audio data.
 * @param play_end_callback A pointer to a function that will be called when play to end.
 * @return int 1: successed; not 1: failed.
t */
int sap_play(uint32_t data_addr, void *play_end_callback)
{

    sap_msg_t msg =
        {
            .msg_id = SAP_MSG_START_PLAY,
            .data_addr = data_addr,
            .data_size = 0,
            .fetcher_buffer_size = SAP_SOURCE_DATA_BUF_SIZE,
            .src_type = SAP_DATA_SRC_FLASH,
            .play_end_callback = play_end_callback,
            .fmt = "MP3",
        };
    sap_send_msg(&msg, NULL);
}

int sap_play_stream(const char *audio_fmt, uint32_t fetcher_buffer_size, uint32_t request_block_size, void *play_end_callback, SAP_PLAY_DATA_REQUEST_CALLBACK data_request_callback)
{
    sap_msg_t msg =
        {
            .msg_id = SAP_MSG_START_PLAY,
            .data_addr = 0,
            .data_size = 0,
            .request_block_size = request_block_size,
            .fetcher_buffer_size = fetcher_buffer_size,
            .src_type = SAP_DATA_SRC_STREAM,
            .play_end_callback = play_end_callback,
            .fmt = audio_fmt,
            .data_request_callback = (SAP_PLAY_DATA_REQUEST_CALLBACK)data_request_callback,
        };
    sap_send_msg(&msg, NULL);
}

sap_status_t sap_get_state(void)
{
    return sap_info.state;
}

// 停止接口
void sap_stop()
{
    if (sap_info.state == SAP_STATE_IDLE)
    {
        return;
    }

    sap_msg_t msg;
    msg.msg_id = SAP_MSG_STOP_PLAY;
    while (sap_info.decode_frame_count < 8 && sap_info.state != SAP_STATE_IDLE)
    {
        vTaskDelay(1);
    }
    sap_send_msg(&msg, NULL);
    vTaskDelay(2);

    // while(sap_info.state != SAP_STATE_IDLE)
    // {
    //     mprintf("zt test3\r\n");
    //     vTaskDelay(100);
    // }
}

/**
 * @brief 调节播放音量
 *
 * @param gain 音量(0--100)
 */
void audio_play_set_vol_gain(int32_t gain)
{
    cm_set_codec_dac_gain(PLAY_CODEC_ID, 0, gain);
}

/**
 * @brief pa、da控制，可选择是否控制功放
 *
 * @param cmd pa使能或失能
 *
 */
void audio_play_hw_pa_da_ctl(FunctionalState cmd, bool is_control_pa)
{
    // PA的操作需要在DAC关闭的情况下进行，不然会影响采音
    if (ENABLE == cmd)
    {
        icodec_start(CODEC_OUTPUT);
        if (is_control_pa)
        {
            power_amplifier_on();
        }
    }
    else
    {
        if (is_control_pa)
        {
            power_amplifier_off();
        }
        icodec_stop(CODEC_OUTPUT);
    }
}

static void sap_send_msg(sap_msg_t *msg, BaseType_t *xHigherPriorityTaskWoken)
{
    if (0 != check_curr_trap())
    {
        xQueueSendFromISR(sap_info.msg_queue, msg, xHigherPriorityTaskWoken);
    }
    else
    {
        xQueueSend(sap_info.msg_queue, msg, portMAX_DELAY);
    }
}

/**
 * @brief Inner start playing function.
 *
 * @param msg A pointer to a sap_msg_t structure used to input data address and callback function.
 * @return int 0:successed, not 0:failed.
 */
static int sap_start_play_inner(sap_msg_t *msg)
{
    int ret = 0;
    uint8_t *tmp_buf;
    do
    {
        // 搜索解码器
        sap_info.cur_decoder = NULL;
        for (audio_decoder_interface_t *decoder = sap_info.decoder_chain; decoder; decoder = decoder->next)
        {
            if (0 == strcmp(decoder->name, msg->fmt))
            {
                sap_info.cur_decoder = decoder;
                break;
            }
        }
        if (NULL == sap_info.cur_decoder)
        {
            ci_logerr(CI_LOG_ERROR, "no decoder found\n");
            ret = -1;
            break;
        }
        // sap_info.data_addr = msg->data_addr;
        sap_info.play_end_callback = msg->play_end_callback;
        sap_info.decoder_handle = sap_info.cur_decoder->init();

        src_data_info_t src_data_info = {
            .src_type = msg->src_type,
            .data_addr = msg->data_addr,
            .data_size = msg->data_size,
            .data_offset = 0,
            .buffer_size = msg->fetcher_buffer_size,
            .request_block_size = msg->request_block_size,
            .data_request_callback = msg->data_request_callback,
        };
        src_type = msg->src_type,
        pdf_init(&src_data_info);

        // malloc a buffer to recevie output data from decoder.
        tmp_buf = pvPortMalloc(SAP_MAX_PCM_FRAME_SIZE);
        if (!tmp_buf)
        {
            ci_logerr(CI_LOG_ERROR, "not enough memory\n");
            ret = -1;
            break;
        }
        int32_t read_size = pdf_fetch_data(sap_info.source_data_buf, SAP_SOURCE_DATA_BUF_SIZE, pdMS_TO_TICKS(2000));
        //ci_logdebug(LOG_AUDIO_PLAY, "sap read %d bytes\n", read_size);
        if (read_size <= 0)
        {
            ci_logerr(CI_LOG_ERROR, "read data failed\n");
            ret = -2;
            break;
        }

        uint32_t pcm_data_size;
        audio_format_info_t format_info = {0};
        int bytes_left = read_size;
        if (sap_info.cur_decoder->get_info(sap_info.decoder_handle, sap_info.source_data_buf, &bytes_left, tmp_buf, &format_info) < 0)
        {
            ci_logerr(CI_LOG_ERROR, "get format info failed\n");
            ret = -3;
            break;
        }
#if (PLAYER_CONTROL_PA)
        audio_play_hw_pa_da_ctl(ENABLE, true);
        vTaskDelay(pdMS_TO_TICKS(100));
#endif
        if (format_info.src_data_size > 0)
        {
            pdf_set_total_data_size(format_info.src_data_size);
        }
        // sap_info.total_pcm_size = format_info.pcm_data_size;
        sap_info.source_buf_bytes_left = bytes_left;
        sap_info.source_buf_offset = read_size - bytes_left;
        sap_info.output_samples = format_info.samples_per_frame;
        sap_info.decode_frame_count = 0;
        sap_info.frame_pcm_size = sap_info.output_samples * sizeof(short);
        if (sap_info.frame_pcm_size > SAP_MAX_PCM_FRAME_SIZE)
        {
            ci_logerr(LOG_AUDIO_PLAY, "pcm frame size %d too large!\n", sap_info.frame_pcm_size); 
        }

        cm_pcm_buffer_info_t pcm_buffer_info;
        pcm_buffer_info.play_buffer_info.block_num = 2;
        pcm_buffer_info.play_buffer_info.buffer_num = SAP_PCM_BUF_COUNT;
        pcm_buffer_info.play_buffer_info.buffer_size = sap_info.frame_pcm_size;
        pcm_buffer_info.play_buffer_info.block_size = pcm_buffer_info.play_buffer_info.buffer_size / pcm_buffer_info.play_buffer_info.block_num;
        int pcm_buffer_total_size = pcm_buffer_info.play_buffer_info.buffer_size * pcm_buffer_info.play_buffer_info.buffer_num;
        if (sap_info.pcm_buffer_size < pcm_buffer_total_size)
        {
            if (sap_info.pcm_buffer != NULL)
            {
                vPortFree(sap_info.pcm_buffer);
                sap_info.pcm_buffer = NULL;
            }
        }

        if (sap_info.pcm_buffer == NULL)
        {
            sap_info.pcm_buffer = pvPortMalloc(pcm_buffer_total_size);
            if (!sap_info.pcm_buffer)
            {
                ci_logerr(CI_LOG_ERROR, "not enough memory\n");
                ret = -5;
                break;
            }
            sap_info.pcm_buffer_size = pcm_buffer_total_size;
        }
        pcm_buffer_info.play_buffer_info.pcm_buffer = sap_info.pcm_buffer;
        cm_config_pcm_buffer(PLAY_CODEC_ID, CODEC_OUTPUT, &pcm_buffer_info);
        uint32_t ret_buf;
        // cm_get_pcm_buffer(PLAY_CODEC_ID,&ret_buf,portMAX_DELAY);
        // memcpy((void*)ret_buf, tmp_buf, mp3FrameInfo.outputSamps*sizeof(short)*mp3FrameInfo.nChans);
        // cm_write_codec(PLAY_CODEC_ID, (void*)ret_buf,portMAX_DELAY);
        static uint32_t pre_sample_rate = 0;
        static uint32_t pre_nChans = 0;
        // if((sap_info.sample_rate != format_info.samprate) || (sap_info.channels != format_info.channels))
        {
            sap_info.sample_rate = format_info.samprate;
            sap_info.channels = format_info.channels;

            cm_sound_info_t sound_info;
            sound_info.sample_rate = format_info.samprate;
            sound_info.sample_depth = IIS_DW_16BIT;
            sound_info.channel_flag = (format_info.channels == 2) ? 3 : 1;
            cm_config_codec(PLAY_CODEC_ID, CODEC_OUTPUT, &sound_info);
        }
    } while (0);

    if (tmp_buf)
    {
        vPortFree(tmp_buf);
    }
    ci_loginfo(LOG_AUDIO_PLAY, "Play start\r\n");
    return ret;
}

static void wait_codec_play_finish()
{
    while (cm_get_codec_empty_buffer_number(PLAY_CODEC_ID, CODEC_OUTPUT) < SAP_PCM_BUF_COUNT - 1)
    {
        vTaskDelay(1);
    }
}

/**
 * @brief Decode one frame.
 *
 * @return int  0: Decode failed;
 *              1: Decode successed,and the PCM buffer of codec manager is full, you can do some delay before the next call to this function to free up CPU for other tasks;
 *              2: Decode successed,and the PCM buffer of codec manager is not full, you'd better call this function again immediately.
 *              3: End of file, there's no source data any more, end of play.
 */
static int sap_decode_one_frame(void)
{
    int ret = 0;
    uint32_t pcm_buf;
    int bytes_left = sap_info.source_buf_bytes_left;
    int32_t read_size;
    //mprintf("source_buf_bytes_left = %d\r\n", sap_info.source_buf_bytes_left);
    read_size = SAP_SOURCE_DATA_BUF_SIZE - sap_info.source_buf_bytes_left;
    if (read_size > 0)
    {
        memmove(sap_info.source_data_buf, &sap_info.source_data_buf[sap_info.source_buf_offset], bytes_left);
        sap_info.source_buf_offset = 0;
        // read_size = pdf_fetch_data(&sap_info.source_data_buf[sap_info.source_buf_bytes_left], read_size, pdMS_TO_TICKS(200));
        read_size = pdf_fetch_data(&sap_info.source_data_buf[sap_info.source_buf_bytes_left], read_size, pdMS_TO_TICKS(32));
        //ci_logwarn(LOG_AUDIO_PLAY, "sap read %d bytes\n", read_size);
        if (read_size > 0)
        {
            // sap_info.source_file_bytes_left -= read_size;
            bytes_left += read_size;
            sap_info.source_buf_bytes_left = bytes_left;
        }
        else if (read_size == -1 && sap_info.source_buf_bytes_left == 0) // 缓存内无数据，并且请求数据任务删除
        {
            wait_codec_play_finish();
            ret = 3;
            return ret;
        }
    }

    cm_get_pcm_buffer(PLAY_CODEC_ID, &pcm_buf, portMAX_DELAY);
    int32_t err = sap_info.cur_decoder->decode_one_frame(sap_info.decoder_handle, &sap_info.source_data_buf[sap_info.source_buf_offset], &bytes_left, (void *)pcm_buf, 0); // Decode a frame.
    //ci_logwarn(LOG_AUDIO_PLAY, "audio decorde err %d,bytes_left %d\n", err, bytes_left);
    if (0 == err)
    {
        if (bytes_left >= 0)
        {
            cm_write_codec(PLAY_CODEC_ID, (void *)pcm_buf, portMAX_DELAY);
        }
        else
        {
            cm_release_pcm_buffer(PLAY_CODEC_ID, CODEC_OUTPUT, (void *)pcm_buf);
            bytes_left = 0;
        }
        int bytes_used = sap_info.source_buf_bytes_left - bytes_left;
        sap_info.source_buf_bytes_left = bytes_left;
        sap_info.source_buf_offset += bytes_used;
        sap_info.decode_frame_count += 1;
        if (sap_info.decode_frame_count == 3)
        {
            cm_start_codec(PLAY_CODEC_ID, CODEC_OUTPUT);
            cm_set_codec_mute(PLAY_CODEC_ID, CODEC_OUTPUT, 3, DISABLE);
        }
        ret = 1;
    }
    else
    {

        cm_release_pcm_buffer(PLAY_CODEC_ID, CODEC_OUTPUT, (void *)pcm_buf);
        wait_codec_play_finish();
        if(err != ERR_MP3_MAINDATA_UNDERFLOW)
            ci_logwarn(LOG_AUDIO_PLAY, "audio decorde err %d,bad frame!\n", err);
    }
    return ret;
}

static int sap_stop_play_inner(int32_t play_cb_state)
{
    int ret = 0;

    cm_set_codec_mute(PLAY_CODEC_ID, CODEC_OUTPUT, 3, ENABLE);
    cm_stop_codec(PLAY_CODEC_ID, CODEC_OUTPUT);
    sap_info.cur_decoder->deinit(sap_info.decoder_handle);
    sap_info.decoder_handle = NULL;

#if (PLAYER_CONTROL_PA)
    audio_play_hw_pa_da_ctl(DISABLE, true);
#endif
    ciss_set(CI_SS_PLAY_STATE, CI_SS_PLAY_STATE_IDLE);
    ci_loginfo(LOG_AUDIO_PLAY,"Play end\n");
    if (sap_info.play_end_callback)
    {
        sap_info.play_end_callback(play_cb_state);
    }
    return ret;
}

/**
 * @brief Task function of player
 *
 * @param pvParameters Task parameter.
 */
static void task_simple_audio_player(void *pvParameters)
{
    sap_info.state = SAP_STATE_IDLE;    //播放状态初始化
    audio_play_state = AUDIO_PLAY_STATE_IDLE;
    TickType_t wait_time = portMAX_DELAY;
    sap_info.msg_queue = xQueueCreate(SAP_MSG_QUEUE_LENGTH, sizeof(sap_msg_t));
    while (1)
    {
        sap_msg_t msg;
        BaseType_t rst = xQueueReceive(sap_info.msg_queue, &msg, wait_time);
        if (rst == pdTRUE) // If received a message from the message queue.
        {
            // Received a message.
            if (sap_info.state != SAP_STATE_IDLE)
            {
                // stop last play.
                sap_info.state = SAP_STATE_STOPPING;
                sap_stop_play_inner(-1);
                pdf_deinit();
                //mprintf("SAP_STATE_IDLE2\r\n");
                sap_info.state = SAP_STATE_IDLE;
                audio_play_state = AUDIO_PLAY_STATE_IDLE;
            }
            wait_time = 0;
            if (msg.msg_id == SAP_MSG_START_PLAY)
            {
                // start current play request.
                sap_info.state = SAP_STATE_STARTING;
                audio_play_state = AUDIO_PLAY_STATE_START;
                int rst = sap_start_play_inner(&msg);
                if (rst != 0)
                {
                    // ci_logerr(LOG_AUDIO_PLAY, "start play failed,err %d!\n", rst);
                    sap_info.state = SAP_STATE_IDLE;
                    audio_play_state = AUDIO_PLAY_STATE_IDLE;
                    wait_time = portMAX_DELAY;
                }
                else
                {
                    ciss_set(CI_SS_PLAY_STATE, CI_SS_PLAY_STATE_PLAYING);
                    sap_info.state = SAP_STATE_PLAYING;
                    audio_play_state = AUDIO_PLAY_STATE_PLAYING;
                }
            }
        }
        else
        {
            // No message
            if (sap_info.state == SAP_STATE_PLAYING)
            {
                // decode one frame
                int rst = sap_decode_one_frame();
                if (rst == 3 || rst == 0)
                {
                    ci_logdebug(LOG_AUDIO_PLAY, "end err %d\n", rst);
                    sap_info.state = SAP_STATE_STOPPING;
                    sap_stop_play_inner(0);
                    //mprintf("SAP_STATE_IDLE3\r\n");
                    sap_info.state = SAP_STATE_IDLE;
                    audio_play_state = AUDIO_PLAY_STATE_IDLE;
                    wait_time = portMAX_DELAY;
                }
                else
                {
                    wait_time = rst ? 0 : 2;
                }
            }
            else if (sap_info.state == SAP_STATE_IDLE)
            {
                wait_time = portMAX_DELAY;
            }
            else
            {
                wait_time = 0;
            }
        }
    }
}

int sap_register_decoder(audio_decoder_interface_t *decoder)
{
    if (decoder == NULL)
    {
        return -1;
    }
    decoder->next = sap_info.decoder_chain;
    sap_info.decoder_chain = decoder;
    return 0;
}

/**
 * @brief 返回播放器task状态
 * 
 * @return uint32_t 播放处理任务状态-兼容自学习中播报状态
 */
uint32_t get_audio_play_state(void)
{
    return audio_play_state;
}
#endif
