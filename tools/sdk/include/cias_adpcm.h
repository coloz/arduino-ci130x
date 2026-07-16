#ifndef __CIAS_ADPCM_H__
#define __CIAS_ADPCM_H__

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "ci_log.h"
#include <string.h>
#include "sdk_default_config.h"

void adpcm_init(void);
uint8_t cias_adpcm_encode(int32_t sample);
int16_t cias_adpcm_decode(uint8_t code);
int pcm_convert_to_adpcm(short *src, int size, unsigned char *dst);   //pcm裸数据转adpcm
int adpcm_convert_to_pcm(unsigned char *src, int size, short *dst);   //adpcm转pcm裸数据
#endif   //__CIAS_ADPCM_H__