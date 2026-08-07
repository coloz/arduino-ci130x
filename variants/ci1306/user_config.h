#ifndef CHIPINTELLI_CI1306_USER_CONFIG_H
#define CHIPINTELLI_CI1306_USER_CONFIG_H

#ifndef CI_CHIP_CI1306
#define CI_CHIP_CI1306 1
#endif

#if defined(AUDIO_IN_FROM_DMIC) && AUDIO_IN_FROM_DMIC && \
    defined(USE_AEC_MODULE) && USE_AEC_MODULE
#error "CI1306 PDM microphone input has no playback-reference channel; select a non-AEC Algorithm profile"
#endif

#include "../../tools/sdk/include/user_config.h"

#endif
