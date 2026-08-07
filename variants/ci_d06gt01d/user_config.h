#ifndef CHIPINTELLI_CI_D06GT01D_USER_CONFIG_H
#define CHIPINTELLI_CI_D06GT01D_USER_CONFIG_H

#ifndef CI_CHIP_CI1306
#define CI_CHIP_CI1306 1
#endif

#ifndef CI_BOARD_CI_D06GT01D
#define CI_BOARD_CI_D06GT01D 1
#endif

#if defined(AUDIO_IN_FROM_DMIC) && AUDIO_IN_FROM_DMIC && \
    defined(USE_AEC_MODULE) && USE_AEC_MODULE
#error "CI-D06GT01D PDM microphone input has no playback-reference channel; select a non-AEC Algorithm profile"
#endif

#include "../../tools/sdk/include/user_config.h"

#endif
