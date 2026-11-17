/* SPDX-License-Identifier: GPL-3.0-only */
/* Kayak's FM-1 flash chips (build without BOARD_SMK37; hal/fm1_flash_map.h): the app's storage and the update loader
 * accept the Puya P25Q80 (0x856014) and the UC25HQ80 of newer FM-1s (0xB36014), nothing else */
#include <stdint.h>
#include <stdio.h>
#include "fm1_cc.h"
#if BOARD_SMK37
#error "build without BOARD_SMK37"
#endif
#include "../firmware/src/store.h"

int main(void)
{
    int ok = FL_JEDEC_OK(0x856014u) && FL_JEDEC_OK(0xB36014u) && !FL_JEDEC_OK(0xEB6014u) && !FL_JEDEC_OK(0u) &&
             !FL_JEDEC_OK(0xFFFFFFu) && !FL_JEDEC_OK(0xB36015u);
    printf("fm1 flash: FL_JEDEC_OK: the Puya 0x856014 and the UC25HQ80 0xB36014 only%28s %s
", "", ok ? "ok" : "FAIL");
    return !ok;
}
