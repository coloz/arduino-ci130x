#ifndef CHIPINTELLI_EASYVOICE_1306_DEV_USER_CONFIG_H
#define CHIPINTELLI_EASYVOICE_1306_DEV_USER_CONFIG_H

#ifndef CI_CHIP_CI1306
#define CI_CHIP_CI1306 1
#endif

#ifndef CI_BOARD_EASYVOICE_1306_DEV
#define CI_BOARD_EASYVOICE_1306_DEV 1
#endif

#if defined(AUDIO_IN_FROM_DMIC) && AUDIO_IN_FROM_DMIC && \
    defined(USE_AEC_MODULE) && USE_AEC_MODULE
#error "easyVoice 1306 dev PDM input has no playback-reference channel; select a non-AEC Algorithm profile"
#endif

#include "../../tools/sdk/include/user_config.h"

#endif
