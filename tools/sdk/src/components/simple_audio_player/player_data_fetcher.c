#include "user_config.h"
#if	SIMPLE_AUDIO_PLAYER_ENABLE
#include "FreeRTOS.h"
#include "task.h"
#include "stream_buffer.h"
#include "player_data_fetcher.h"
#include "ci_log.h"
#include "flash_rw_process.h"
#include "status_share.h"

typedef struct
{
    src_data_info_t src_data_info;
    StreamBufferHandle_t data_buffer;
    int32_t data_offset;
    int32_t eos;            // End of data stream.
    int32_t request_wait_count;
    TaskHandle_t task_handle;
}data_fetch_task_info_t;

static void task_data_fetch(void *pvParameters);

data_fetch_task_info_t data_fetch_task_info = {0};

void pdf_init(src_data_info_t *src_data_info)
{
    pdf_deinit();

    // 将传入的参数赋值给data_fetch_task_info结构体
    data_fetch_task_info.src_data_info = *src_data_info;

    if (src_data_info->data_size <= 0)
    {
        data_fetch_task_info.src_data_info.data_size = PDF_MAX_DATA_SIZE;
    }

    // 创建一个流缓冲区，用于存储数据
    data_fetch_task_info.data_buffer = xStreamBufferCreate(src_data_info->buffer_size, 1);
    // 设置数据偏移量
    data_fetch_task_info.data_offset = src_data_info->data_offset;
    data_fetch_task_info.eos = 0;
    xTaskCreate(task_data_fetch, "data_fetch", 256, src_data_info, 3, &data_fetch_task_info.task_handle);
}

void pdf_deinit(void)
{
    while(data_fetch_task_info.task_handle != NULL)
    {
        //强制结束上一个任务
        taskENTER_CRITICAL();
        data_fetch_task_info.src_data_info.data_size = 0;
        data_fetch_task_info.data_offset = 0;
        xStreamBufferReset(data_fetch_task_info.data_buffer);
        taskEXIT_CRITICAL();
        vTaskDelay(1);
    }
}

void task_data_fetch(void *pvParameters)
{
    bool ret = false;
    // 设置结束标志位，本地使用有效left_size长度、云端使用gCiasAiotRunParam.request_play_data_flag结束请求任务
    data_fetch_task_info.eos = 0;
    // 计算剩余数据大小
    int left_size = data_fetch_task_info.src_data_info.data_size - data_fetch_task_info.data_offset;
    // 当剩余数据大小大于0且流缓冲区有可用空间时，循环读取数据
    while((left_size > 0) || (xStreamBufferBytesAvailable(data_fetch_task_info.data_buffer) > 0) )
    {
        // 计算流缓冲区可用空间大小
        size_t space_size = xStreamBufferSpacesAvailable(data_fetch_task_info.data_buffer);
        // 如果流缓冲区有可用空间
        if ((space_size > data_fetch_task_info.src_data_info.request_block_size) && ((left_size > 0)))
        {
            if (data_fetch_task_info.src_data_info.src_type == SAP_DATA_SRC_FLASH)
            {
                // 定义一个临时缓冲区
                uint8_t tmp_buffer[256];
                // 计算读取大小
                size_t read_size = (space_size > sizeof(tmp_buffer)) ? sizeof(tmp_buffer) : space_size;
                read_size = (read_size > left_size) ? left_size : read_size;
                // 从flash中读取数据
                post_read_flash(tmp_buffer, data_fetch_task_info.src_data_info.data_addr + data_fetch_task_info.data_offset, read_size);
                // 将数据发送到流缓冲区
                xStreamBufferSend(data_fetch_task_info.data_buffer, tmp_buffer, read_size, portMAX_DELAY);
                data_fetch_task_info.data_offset += read_size;
            }
            else
            {
                size_t read_size = (space_size >= data_fetch_task_info.src_data_info.request_block_size) ? data_fetch_task_info.src_data_info.request_block_size : space_size;
                // mprintf("read_size = %d,request_play_data_flag = %d,\r\n",read_size,gCiasAiotRunParam.request_play_data_flag);
                if (read_size > 0 && data_fetch_task_info.src_data_info.data_request_callback)
                {

                    taskENTER_CRITICAL();
                    if (data_fetch_task_info.request_wait_count == 0)
                    {
                        // 发送数据请求
                        data_fetch_task_info.src_data_info.data_request_callback(read_size);
                        data_fetch_task_info.request_wait_count = 20;
                    }
                    else
                    {
                        // 如果请求等待次数大于0，则延迟32个tick
                        data_fetch_task_info.request_wait_count--;
                    }
                    taskEXIT_CRITICAL();
                    if (data_fetch_task_info.request_wait_count != 0)
                    {
                        vTaskDelay(pdMS_TO_TICKS(20));
                    }
                }

            
            }

            // 更新剩余数据大小
            left_size = data_fetch_task_info.src_data_info.data_size - data_fetch_task_info.data_offset;
            // ci_logdebug(LOG_AUDIO_PLAY, "pdf left %d bytes\n", left_size);
        }
        else
        {
            // 如果流缓冲区没有可用空间，则延迟2个tick
            vTaskDelay(2);
        }
    }
    //mprintf("==task_data_fetch vTaskDelete\r\n");
    // 设置结束标志位
    data_fetch_task_info.eos = 1;
    // 删除流缓冲区
    taskENTER_CRITICAL();
    vStreamBufferDelete(data_fetch_task_info.data_buffer);
    // 将流缓冲区指针置为NULL
    data_fetch_task_info.data_buffer = NULL;
    taskEXIT_CRITICAL();
    // 删除任务
    data_fetch_task_info.task_handle = NULL;
    vTaskDelete(NULL);
}

int32_t pdf_push_data(uint8_t *data, uint32_t data_size, TickType_t xTicksToWait)
{
    size_t write_bytes = xStreamBufferSend(data_fetch_task_info.data_buffer, data, data_size, xTicksToWait);
    if (write_bytes > 0)
    {
        data_fetch_task_info.data_offset += write_bytes;
        data_fetch_task_info.request_wait_count = 0;
    }
    return write_bytes;
}
// uint16_t pdf_get_vaild_size(void)
// {
//     return xStreamBufferBytesAvailable(data_fetch_task_info.data_buffer);
// }
int32_t pdf_fetch_data(void *buffer, uint32_t buffer_size, TickType_t xTicksToWait )
{
    // 定义变量bytes_read，用于存储读取的字节数
    int32_t bytes_read = 0;
    do 
    {
        //请求任务删除
        if (data_fetch_task_info.eos)
        {
            bytes_read = -1;
            break;
        }
        taskENTER_CRITICAL();
        if (data_fetch_task_info.data_buffer)
        {
            // 从data_buffer中读取数据到buffer中，读取的字节数存储在bytes_read中
            size_t available = xStreamBufferBytesAvailable(data_fetch_task_info.data_buffer);
            if (available >= buffer_size)
            {     
                bytes_read = xStreamBufferReceive(data_fetch_task_info.data_buffer, buffer, buffer_size, 0);
                taskEXIT_CRITICAL();
            }
            else
            {
                xTicksToWait = xTicksToWait > 0 ? xTicksToWait - 1 : 0;
                if (xTicksToWait == 0 && available > 0)
                {
                    bytes_read = xStreamBufferReceive(data_fetch_task_info.data_buffer, buffer, buffer_size, 0);
                    taskEXIT_CRITICAL();
                }
                else
                {
                    taskEXIT_CRITICAL();
                    vTaskDelay(1);
                }
            }
        }
        else
        {
            taskEXIT_CRITICAL();
        }
    }while(bytes_read == 0 && xTicksToWait > 0);

    return bytes_read;
}

int32_t pdf_set_total_data_size(uint32_t data_size)
{
    data_fetch_task_info.src_data_info.data_size = data_size;
    return 0;
}

int32_t pdf_set_eos()
{
    data_fetch_task_info.src_data_info.data_size = data_fetch_task_info.src_data_info.data_offset;
    return 0;
}
#endif
