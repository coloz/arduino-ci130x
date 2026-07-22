#include "user_config.h"
#if	SIMPLE_AUDIO_PLAYER_ENABLE
#include "ci_log.h"
#include "romlib_runtime.h"
#include "simple_audio_player.h"


#pragma push
#pragma pack(1)
typedef struct
{
    char ID3[3];               //"ID3"
    char ver;                  //3
    char revision;             //0
    char flag;                 //0
    uint32_t total_frame_size; //标签帧大小
    char frame_ID[4];          //"PRIV"
    uint32_t frame_size;       //PRIV大小
    uint16_t frame_flag;       //0
    char CI[2];                //"CI"
    uint32_t file_size;        //文件大小
    uint32_t pcm_size;         //PCM大小
}ci_mp3_header_t;
#pragma pop




static void *mp3_decoder_init(void)
{
    // return (void *)MP3InitDecoder();
    return MASK_ROM_LIB_FUNC->mp3func.MP3InitDecoder_p();	
}

static void mp3_docoder_deinit(void* decoder_haldle)
{
    MASK_ROM_LIB_FUNC->mp3func.MP3FreeDecoder_p(decoder_haldle);
}


static int get_info(void* decoder_handle, uint8_t *data, int *bytes_left, uint8_t *pcm_buf, audio_format_info_t *format_info)
{
    int ret = 0;

    do {
        format_info->src_data_size = ((ci_mp3_header_t*)data)->file_size;

        uint32_t tmp = ((ci_mp3_header_t*)data)->total_frame_size;
        int id3v2_size = ((tmp & 0xFF) << 21) | ((tmp & 0xFF00) << 6) | ((tmp & 0xFF0000) >> 9) | ((tmp & 0xFF000000) >> 24);
        *bytes_left -= id3v2_size;
        ci_logdebug(LOG_AUDIO_PLAY,"id3v2 size:%x\n", id3v2_size);

        int32_t mp3_sync_offset;
        mp3_sync_offset = MASK_ROM_LIB_FUNC->mp3func.MP3FindSyncWord_p(data + id3v2_size, *bytes_left);   // Find the head flag of the frist frame.
        if (mp3_sync_offset == -1)
        {
            ret = -2;
            break;
        }
        *bytes_left -= mp3_sync_offset;
        // Decode a frame to get some information about the audio, such as sample rate, channels, output samples per frame.
        uint8_t * in_data_ptr = data + id3v2_size + mp3_sync_offset;
        int32_t err = MASK_ROM_LIB_FUNC->mp3func.MP3Decode_p(decoder_handle, &in_data_ptr, bytes_left, (short*)pcm_buf, 0);  // Decode a frame. 
        MP3FrameInfo mp3FrameInfo;
        MASK_ROM_LIB_FUNC->mp3func.MP3GetLastFrameInfo_p(decoder_handle, &mp3FrameInfo);       // Get information about the frame that just decoded.
        if(ERR_MP3_NONE != err )
        {
            ci_logwarn(LOG_AUDIO_PLAY,"mp3_decorde err %d,bad frame!\n",err);
            ret = -3;
            break;
        }

        // format_info->pcm_data_size = ((ci_mp3_header_t*)data)->pcm_size*mp3FrameInfo.bitsPerSample/8;
        format_info->channels = mp3FrameInfo.nChans;
        format_info->samprate = mp3FrameInfo.samprate;
        format_info->bits_per_sample = mp3FrameInfo.bitsPerSample;
        format_info->samples_per_frame = mp3FrameInfo.outputSamps;
    }while(0);

    return ret;
}

static int decode_one_frame(void* decoder_handle, uint8_t *data, int *bytes_left, uint8_t *pcm_buf, uint32_t *pcm_data_size)
{
    int ret = 0;
    int32_t mp3_sync_offset = MASK_ROM_LIB_FUNC->mp3func.MP3FindSyncWord_p(data, *bytes_left);   // Find the head flag of the frist frame.
    if (mp3_sync_offset == -1)
    {
        ret = 2;
        return ret;
    }
    *bytes_left -= mp3_sync_offset;
    uint8_t * in_data_ptr = data + mp3_sync_offset;
    ret = MASK_ROM_LIB_FUNC->mp3func.MP3Decode_p(decoder_handle, &in_data_ptr, bytes_left, (void*)pcm_buf, 0);  // Decode a frame. 

    return ret;
}

#if AUDIO_PLAYER_ENABLE
register_audio_decoder(MP3, mp3_decoder_init, mp3_docoder_deinit, get_info, decode_one_frame); 
#endif
#endif
