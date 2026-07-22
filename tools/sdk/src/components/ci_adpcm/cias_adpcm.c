#include "cias_adpcm.h"

/* Quantizer step size lookup table */
static const uint16_t StepSizeTable[89] = {7, 8, 9, 10, 11, 12, 13, 14, 16, 17,
                                    19, 21, 23, 25, 28, 31, 34, 37, 41, 45,
                                    50, 55, 60, 66, 73, 80, 88, 97, 107, 118,
                                    130, 143, 157, 173, 190, 209, 230, 253, 279, 307,
                                    337, 371, 408, 449, 494, 544, 598, 658, 724, 796,
                                    876, 963, 1060, 1166, 1282, 1411, 1552, 1707, 1878, 2066,
                                    2272, 2499, 2749, 3024, 3327, 3660, 4026, 4428, 4871, 5358,
                                    5894, 6484, 7132, 7845, 8630, 9493, 10442, 11487, 12635, 13899,
                                    15289, 16818, 18500, 20350, 22385, 24623, 27086, 29794, 32767};

static int8_t IndexTable[16] = {0xff, 0xff, 0xff, 0xff, 2, 4, 6, 8, 0xff, 0xff, 0xff, 0xff, 2, 4, 6, 8};

int16_t Encodeindex = 0;
int32_t Encodepredsample = 0;
int16_t Decodeindex = 0;
int32_t Decodepredsample = 0;

void adpcm_init(void)
{
    Encodeindex = 0;
    Encodepredsample = 0;
    Decodeindex = 0;
    Decodepredsample = 0;
}
/**
 * @brief  cias_adpcm_encode.
 * @param sample: a 16-bit PCM sample
 * @retval : a 4-bit ADPCM sample
 */

uint8_t cias_adpcm_encode(int32_t sample)
{
    // static int16_t  Encodeindex = 0;
    // static int32_t Encodepredsample = 0;
    uint8_t code = 0;
    uint16_t tmpstep = 0;
    int32_t diff = 0;
    int32_t diffq = 0;
    uint16_t step = 0;

    step = StepSizeTable[Encodeindex];

    /* 2. compute diff and record sign and absolut value */
    diff = sample - Encodepredsample;
    if (diff < 0)
    {
        code = 8;
        diff = -diff;
    }

    /* 3. quantize the diff into ADPCM code */
    /* 4. inverse quantize the code into a predicted diff */
    tmpstep = step;
    diffq = (step >> 3);

    if (diff >= tmpstep)
    {
        code |= 0x04;
        diff -= tmpstep;
        diffq += step;
    }

    tmpstep = tmpstep >> 1;

    if (diff >= tmpstep)
    {
        code |= 0x02;
        diff -= tmpstep;
        diffq += (step >> 1);
    }

    tmpstep = tmpstep >> 1;

    if (diff >= tmpstep)
    {
        code |= 0x01;
        diffq += (step >> 2);
    }

    /* 5. fixed predictor to get new predicted sample*/
    if (code & 8)
    {
        Encodepredsample -= diffq;
    }
    else
    {
        Encodepredsample += diffq;
    }

    /* check for overflow*/
    if (Encodepredsample > 32767)
    {
        Encodepredsample = 32767;
    }
    else if (Encodepredsample < -32768)
    {
        Encodepredsample = -32768;
    }

    /* 6. find new stepsize Encodeindex */
    Encodeindex += IndexTable[code];
    /* check for overflow*/
    if (Encodeindex < 0)
    {
        Encodeindex = 0;
    }
    else if (Encodeindex > 88)
    {
        Encodeindex = 88;
    }

    /* 8. return new ADPCM code*/
    return (code & 0x0f);
}

/**
 * @brief  cias_adpcm_decode.
 * @param code: a byte containing a 4-bit ADPCM sample.
 * @retval : 16-bit ADPCM sample
 */

int16_t cias_adpcm_decode(uint8_t code)
{

    uint16_t step = 0;
    int32_t diffq = 0;

    step = StepSizeTable[Decodeindex];

    /* 2. inverse code into diff */
    diffq = step >> 3;
    if (code & 4)
    {
        diffq += step;
    }

    if (code & 2)
    {
        diffq += step >> 1;
    }

    if (code & 1)
    {
        diffq += step >> 2;
    }

    /* 3. add diff to predicted sample*/
    if (code & 8)
    {
        Decodepredsample -= diffq;
    }
    else
    {
        Decodepredsample += diffq;
    }

    /* check for overflow*/
    if (Decodepredsample > 32767)
    {
        Decodepredsample = 32767;
    }
    else if (Decodepredsample < -32768)
    {
        Decodepredsample = -32768;
    }

    /* 4. find new quantizer step size */
    Decodeindex += IndexTable[code];
    /* check for overflow*/
    if (Decodeindex < 0)
    {
        Decodeindex = 0;
    }
    if (Decodeindex > 88)
    {
        Decodeindex = 88;
    }

    /* 5. save predict sample and Decodeindex for next iteration */
    /* done! static variables */

    /* 6. return new speech sample*/
    return ((int16_t)Decodepredsample);
}
int pcm_convert_to_adpcm(short *src, int size, unsigned char *dst)
{
    unsigned char code = 0;
    unsigned char adpcm = 0;
    int count = 0;
    for(int i = 0, j = 0; i < size; i+=2, j++)
    {
        code = cias_adpcm_encode(src[i]);
        adpcm = code & 0xF;
        code = cias_adpcm_encode(src[i+1]);
        adpcm |= (code << 4);
        dst[j] = adpcm;
        count = j;
    }
    return 0;
}
// adpcm转pcm裸数据
int adpcm_convert_to_pcm(unsigned char *src, int size, short *dst)
{
    // 定义两个short类型的变量，用于存储解码后的样本值
    short sample_tmp, sample_tmp2;
    // 定义一个int类型的变量，用于存储解码后的样本值
    int a = 0;
    // 定义一个int类型的变量，用于存储解码后的样本值
    int count = 0;
    // 遍历源数据，解码每个样本值，并存储到目标数据中
    for(int i = 0, j = 0; i < size; i++, j+=2)
    {
        // 解码第一个样本值
        sample_tmp = cias_adpcm_decode(src[i] & 0xF);
        // 将解码后的样本值存储到目标数据中
        dst[j] = sample_tmp;
        // 解码第二个样本值
        sample_tmp2 = cias_adpcm_decode((src[i] >> 4) & 0xF);
        // 将解码后的样本值存储到目标数据中
        dst[j + 1] = sample_tmp2;
        // 更新计数器
        count = j;
    }
    // 返回0，表示解码成功
    return 0;
}