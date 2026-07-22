#include <stdint.h>
#include <stdbool.h>
#include <ci130x_system.h>
#include <string.h>
#include <stdlib.h>
#include "ci_flash_data_info.h"
#include "flash_manage_outside_port.h"
#include "user_config.h"
#include "ci130x_spiflash.h"
#include "ci_log.h"
#include "sdk_default_config.h"
uint32_t ota_aes_info_addr_func(void)
{
#if CI_OTA_ENABLE
    return 0x7000;
#else
    return 0x3000;
#endif
}
