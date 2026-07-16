#include "user_config.h"
#if	SIMPLE_AUDIO_PLAYER_ENABLE
#pragma once

#include "FreeRTOS.h"
#include <stdbool.h>
#include "ci130x_system.h"

typedef enum
{
    SAP_DATA_SRC_FLASH,         /*!< 从FLASH读取源数据 */
    SAP_DATA_SRC_STREAM,        /*!< 从其他模块获取流数据 */
}sap_src_type_t;


typedef enum {
    SAP_STATE_STARTING,         /*!< 播放器处于启动播放的状态 */
    SAP_STATE_PLAYING,          /*!< 播放器处于正在播放的状态 */
    SAP_STATE_STOPPING,         /*!< 播放器处于正在停止播放的状态 */
    SAP_STATE_IDLE,             /*!< 播放器处于空闲状态 */
}sap_status_t;

/**
 * @brief audio play process 任务状态
 * 
 */
 //兼容老播放器状态
typedef enum
{
    AUDIO_PLAY_STATE_IDLE,     /*!< 播放空闲       */
    AUDIO_PLAY_STATE_START,    /*!< 播放开始       */
    AUDIO_PLAY_STATE_PLAYING,  /*!< 播放中         */
    AUDIO_PLAY_STATE_STOP,     /*!< 播放停止       */
} audio_play_state_t;   
/**
 * @brief 音频信息数据结构
 * 
 */
typedef struct
{
    uint32_t samprate;              /*!< 采样率                */
    // int32_t total_sample_num;    /*!< 总采样点数            */
    int32_t src_data_size;          /*!< 源数据大小            */
    // int32_t pcm_data_size;       /*!< pcm数据大小       */   
    int32_t samples_per_frame;      /*!< 每帧的采样点数        */
    uint8_t channels;               /*!< 通道数               */
    uint8_t bits_per_sample;        /*!< 每个采样点的位数      */
}audio_format_info_t;

typedef void(*SAP_PLAY_END_CALLBACK)(int32_t arg);                  // 播放结束回调函数类型
typedef void(*SAP_PLAY_DATA_REQUEST_CALLBACK)(int32_t data_size);   // 请求数据回调函数类型


struct audio_decoder_interface_st;
typedef struct audio_decoder_interface_st audio_decoder_interface_t; 
/**
 * @brief 音频解码器接口
 * 
 */
struct audio_decoder_interface_st
{
    const char* name;                       /*!< 解码器名称 */
    void* (*init)(void);                    /*!< 解码器初始化接口指针 */  
    void (*deinit)(void* decoder_haldle);   /*!< 解码器反初始化接口指针 */
    int (*get_info)(void* decoder_haldle, uint8_t *data, int *data_size, uint8_t *pcm_buf, audio_format_info_t *format_info);   /*! 获取音频信息的接口指针 */
    int (*decode_one_frame)(void* decoder_haldle, uint8_t *data, int *data_size, uint8_t *pcm_buf, uint32_t *pcm_data_size);    /*! 解码一帧音频的接口指针 */
    audio_decoder_interface_t *next;        /*!< 链表指针，用于指向下一个解码器 */
};

/**
 * @brief 播放器初始化接口.
 * 
 * @return int 1: 成功; not 1: 失败.
 */
int sap_init(void);

/**
 * @brief 启动播放，用于播放本地音频文件（从FLASH读取数据）。
 * @param data_addr uint32_t 用于指定音频数据在FLASH中的地址。
 * @param play_end_callback 播放结束的回调函数指针,因为此接口为异步接口,需要调用模块提供一个接口用于通知播放结束事件。
 * @return int 1: 成功; not 1: 失败.
t */
int sap_play(uint32_t data_addr, void* play_end_callback);

/**
 * @brief 启动播放, 用于播放其他通信模块提供的流式音频数据.
 * @param audio_fmt const char* 音频格式,例如: "MP3", "PCM"等.
 * @param fetcher_buffer_size uint32_t 用于指定数据获取模块的缓冲区大小.
 * @param request_block_size uint32_t 用于指定数据获取模块每次请求数据的大小.
 * @param play_end_callback 播放结束的回调函数指针,因为此接口为异步接口,需要调用模块提供一个接口用于通知播放结束事件。
 * @param data_request_callback 播放器需要数据时,会调用此回调函数,用于数据获取模块向通信模块发起数据请求。
 * @return int 1: 成功; not 1: 失败.
t */
int sap_play_stream(const char* audio_fmt, uint32_t fetcher_buffer_size, uint32_t request_block_size, void* play_end_callback, SAP_PLAY_DATA_REQUEST_CALLBACK data_request_callback);

/**
 * @brief 获取播放器当前的工作状态.
 */
sap_status_t sap_get_state(void);

/**
 * @breif 停止播放, 同步接口,会阻塞调用线程,直到播放停止完成。
 */
void sap_stop();

/**
 * @brief 注册解码器接口
 * 
 * @param decoder audio_decoder_interface_t* 解码器接口指针
 * @return int 1: 成功; not 1: 失败.
 */
int sap_register_decoder(audio_decoder_interface_t *decoder);

/**
 * @brief 向播放器推送数据
 * 
 * @param data uint8_t* 数据指针
 * @param data_size uint32_t 数据大小
 * @param xTicksToWait TickType_t 等待时间
 * @return int32_t 推送数据的大小
 */
int32_t pdf_push_data(uint8_t *data, uint32_t data_size, TickType_t xTicksToWait);

/**
 * @breif 控制PA功放
 * @param cmd FunctionalState 控制命令
 * @param is_control_pa bool 是否控制PA功放
 */
void audio_play_hw_pa_da_ctl(FunctionalState cmd,bool is_control_pa);

/**
 * @brief 设置音量增益
 * @param gain int32_t 增益值
 */
void audio_play_set_vol_gain(int32_t gain);

/**
 * @brief 注册音频解码器宏
 */
#define register_audio_decoder(fmt, _init, _deinit, _get_info, _decode_one_frame) __attribute__((constructor))\
static void __register_##fmt##_decoder(void)\
{\
    static audio_decoder_interface_t adi;\
    adi.name = #fmt;\
    adi.init = _init;\
    adi.deinit = _deinit;\
    adi.get_info = _get_info;\
    adi.decode_one_frame = _decode_one_frame;\
    adi.next = NULL;\
    sap_register_decoder(&adi);\
}

#endif


