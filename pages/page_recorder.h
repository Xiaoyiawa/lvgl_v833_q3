#ifndef PROJ_PAGE_RECORDER_H
#define PROJ_PAGE_RECORDER_H

#ifdef __cplusplus
extern "C" {
#endif

/*********************
 *      INCLUDES
 *********************/
#include "../lvgl/lvgl.h"
#include "../lv_lib_100ask/lv_lib_100ask.h"
#include "page_manager.h"
#include "../platform/alsa_rec.h"

/**********************
 * GLOBAL PROTOTYPES
 **********************/
BasePage * recorder_page_create(void);

/**********************
 *      MACROS
 **********************/

#ifdef __cplusplus
} /* extern "C" */
#endif

#endif