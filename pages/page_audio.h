#ifndef PROJ_PAGE_AUDIO_H
#define PROJ_PAGE_AUDIO_H

#ifdef __cplusplus
extern "C" {
#endif

/*********************
 *      INCLUDES
 *********************/
#include "../lvgl/lvgl.h"
#include "../lv_lib_100ask/lv_lib_100ask.h"
#include "page_manager.h"
#include "platform/audio_player.h"
#include <unistd.h>
#include <stdio.h>
#include <stdbool.h>

/*********************
 *      DEFINES
 *********************/

/**********************
 *      TYPEDEFS
 **********************/

/**********************
 * GLOBAL PROTOTYPES
 **********************/
lv_obj_t * page_audio();

/**********************
 *      MACROS
 **********************/

#ifdef __cplusplus
} /* extern "C" */
#endif

#endif
