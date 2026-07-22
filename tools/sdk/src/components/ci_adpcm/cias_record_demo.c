#include "cias_record_demo.h"
#include "status_share.h"
#if RECORD_PLAY_BY_FLASH_ENABLE

RecordPlayTestTypedef gRecordPlayTest =
    {
        .record_stream_buffer = NULL,
        .record_work_state = RECORD_STATE_IDLE,
        .play_work_state = PLAY_STATE_IDLE,
        .record_cur_offset = 0,
        .play_cur_offset = 0,
};
void record_to_flash_task(void)
{
    uint8_t adpcm_dst_data_buf[128] = {0};
    while (1)
    {
        int stream_aviable_len = xStreamBufferBytesAvailable(gRecordPlayTest.record_stream_buffer);
        if (stream_aviable_len > 128)
        {
            //mprintf("stream_aviable_len = %d\r\n", stream_aviable_len);
            int rx_size = xStreamBufferReceive(gRecordPlayTest.record_stream_buffer, adpcm_dst_data_buf, 128, portMAX_DELAY);
            if (rx_size != 128)
            {
                mprintf("record_stream_buffer rcv error\r\n");
            }
            else
            {
                if (gRecordPlayTest.record_cur_offset <= RECORD_SAVE_FLASH_SIZE)
                {
                   // pcm_convert_to_adpcm(upload_src_data_buf, 512/2,  adpcm_dst_data_buf);
                    mprintf(".");
                   // mprintf("gRecordPlayTest.record_cur_offset = %x\r\n", gRecordPlayTest.record_cur_offset);
                    if (RETURN_OK != post_write_flash((uint8_t *)adpcm_dst_data_buf, RECORD_SAVE_FLASH_START_ADDR + gRecordPlayTest.record_cur_offset, 128))
                    {
                        mprintf("record: write flash error\r\n");
                    }
                    else
                    {
                        gRecordPlayTest.record_cur_offset += 128;
                    }
                }
                else
                {
                    mprintf("record over, record_cur_offset = %d\r\n", gRecordPlayTest.record_cur_offset);
                    gRecordPlayTest.record_work_state == RECORD_STATE_IDLE;
                }
            }
        }
        else
        {
            vTaskDelay(pdMS_TO_TICKS(2));
        }
    }
}

void play_from_flash_task(void)
{
    post_erase_flash(RECORD_SAVE_FLASH_START_ADDR, RECORD_SAVE_FLASH_SIZE);
    uint8_t pcm_play_buf[512] = {0};
    uint8_t adpcm_play_buf[128] = {0};
    uint32_t write_pcm_addr_cpy = 0;
    while (1)
    {
        switch (gRecordPlayTest.play_work_state)
        {
        case PLAY_STATE_START:
            gRecordPlayTest.play_work_state = PLAY_STATE_RUNING;
            gRecordPlayTest.record_work_state = RECORD_STATE_IDLE;
            cm_stop_codec(PLAY_CODEC_ID, CODEC_OUTPUT);    //清中断
            audio_pre_rslt_out_play_card_init(); // 重新初始化声卡
            cm_start_codec(PLAY_CODEC_ID, CODEC_OUTPUT);
            cm_set_codec_mute(PLAY_CODEC_ID, CODEC_OUTPUT, 3, DISABLE);
            gRecordPlayTest.play_cur_offset = 0;
            break;
        case PLAY_STATE_RUNING:
            if ((gRecordPlayTest.play_cur_offset <= gRecordPlayTest.record_cur_offset) && gRecordPlayTest.play_cur_offset <= RECORD_SAVE_FLASH_SIZE)
            {
                cm_get_pcm_buffer(PLAY_CODEC_ID, &write_pcm_addr_cpy, pdMS_TO_TICKS(10)); // TODO HSL
                if (write_pcm_addr_cpy)
                {
                    memset(pcm_play_buf, 0, 512);
                    memset(adpcm_play_buf, 0, 512/4);
                    mprintf("=");
                    //mprintf("gRecordPlayTest.play_cur_offset = %x\r\n", gRecordPlayTest.play_cur_offset);
                    post_read_flash((int8_t *)adpcm_play_buf, RECORD_SAVE_FLASH_START_ADDR + gRecordPlayTest.play_cur_offset, 512/4);
                    adpcm_convert_to_pcm(adpcm_play_buf, 512/4, (short *)pcm_play_buf);
                    int8_t *pcm_data_p_cpy = (int8_t *)write_pcm_addr_cpy;
                    for (int i = 0; i < 512; i++)   // 数据复制一分进行播放，不复制会出现音频播放加快
                    {
                        pcm_data_p_cpy[2 * i] = pcm_play_buf[i];
                        pcm_data_p_cpy[2 * i + 1] = pcm_play_buf[i];
                    }
                    cm_write_codec(PLAY_CODEC_ID, (void *)write_pcm_addr_cpy, pdMS_TO_TICKS(10));
                    gRecordPlayTest.play_cur_offset += 512/4;
                }
                else
                {
                    mprintf("get pcm buf error\r\n");
                }
            }
            else
            {
                mprintf("play over, play_cur_offset = %d\r\n", gRecordPlayTest.play_cur_offset);
                gRecordPlayTest.play_work_state = PLAY_STATE_IDLE;
            }
            break;
        }
        vTaskDelay(pdMS_TO_TICKS(10));
    }
}
// 录音播放初始化
bool record_play_test_init(void)
{
    bool ret = false;
    gRecordPlayTest.record_stream_buffer = xStreamBufferCreate(128 * 50, 128);
    if (gRecordPlayTest.record_stream_buffer == NULL)
    {
        mprintf("error %s  %d\n", __func__, __LINE__);
        // 处理错误情况
        return false;
    }
    xTaskCreate(play_from_flash_task, "play_from_flash_task", 512, NULL, 4, NULL);
    xTaskCreate(record_to_flash_task, "record_to_flash_task", 512, NULL, 4, NULL);
    return true;
}
//调用该函数开始测试录音
void record_play_test_start(void)
{
    static uint8_t test_count = 1;
    extern RecordPlayTestTypedef gRecordPlayTest;
    if (test_count == 1)
    {
        while(1)
        {
            if(ciss_get(CI_SS_PLAY_STATE) == CI_SS_PLAY_STATE_PLAYING)
            {
                mprintf("wait paly over\r\n");
                vTaskDelay(500);
            }
            else
            {
                break;
            }
        }
        mprintf("===start record\r\n");
        gRecordPlayTest.record_work_state = RECORD_STATE_START;
        vTaskDelay(10000); // 录音10S后播放
        test_count++;
    }
    if(test_count == 2)
    {
        mprintf("===start play\r\n");
        gRecordPlayTest.play_work_state = PLAY_STATE_START;
        test_count++;
    }
}
#endif