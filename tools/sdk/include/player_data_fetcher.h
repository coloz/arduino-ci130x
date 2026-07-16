#include "user_config.h"
#if	SIMPLE_AUDIO_PLAYER_ENABLE
#pragma once
#include "simple_audio_player.h"

#define PDF_MAX_DATA_SIZE           ((~(uint32_t)0)>>1)     // 数据最大大小,在不知道数据大小的情况下，可以设置成此值，然后在数据结束时调用pd_set_eos函数设置数据结束标志。


typedef struct {
    sap_src_type_t src_type;            // 数据源类型
    uint32_t data_addr;                 // 数据起始地址
    uint32_t data_offset;               // 数据读取指针地址
    uint32_t data_size;                 // 数据大小
    uint32_t buffer_size;               // 缓冲区大小
    uint32_t request_block_size;        // 请求数据块大小    
    SAP_PLAY_DATA_REQUEST_CALLBACK data_request_callback;   // 数据请求回调函数, 当数据缓存有空闲空间时，会调用此函数请求数据，函数由通信模块实现。
}src_data_info_t;


/**
 * @breif 初始化数据获取器
 * @param src_data_info 数据源信息
 */
extern void pdf_init(src_data_info_t *src_data_info);

/**
 * @breif 用于从数据拉取模块中读取数据
 * @param buffer 用于存放读取到的数据
 * @param buffer_size 存放读取数据的缓冲区大小
 * @param xTicksToWait 超时时间，单位为系统节拍数
 * @return 读取到的数据大小，如果返回-1，表示数据读取结束。
 */
extern int32_t pdf_fetch_data(void *buffer, uint32_t buffer_size, TickType_t xTicksToWait);

/**
 * @breif 设置数据总大小
 * @param data_size 数据总大小
 * @return 0: 固定返回0。
 */
 extern int32_t pdf_set_total_data_size(uint32_t data_size);

/**
 * @breif 设置数据结束标志
 * @return 0: 固定返回0。
 */
extern int32_t pdf_set_eos();

/**
 * @breif 反初始化数据获取器, 用于释放数据获取器占用的资源
 */
extern void pdf_deinit(void);


#endif