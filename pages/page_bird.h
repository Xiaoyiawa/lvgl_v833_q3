#ifndef PROJ_PAGE_BIRD_H
#define PROJ_PAGE_BIRD_H

#ifdef __cplusplus
extern "C" {
#endif

/*********************
 *      INCLUDES
 *********************/
#include "../lvgl/lvgl.h"
#include "lv_drv_conf.h"
#include "../lv_lib_100ask/lv_lib_100ask.h"
#include <stdio.h>
#include <stdlib.h>
#include <time.h>
#include "page_manager.h"

/*********************
 *      DEFINES
 *********************/

/**********************
 *      TYPEDEFS
 **********************/

/**********************
 * GLOBAL PROTOTYPES
 **********************/
BasePage * page_bird_create();

/**********************
 *      MACROS
 **********************/

#ifdef __cplusplus
} /* extern "C" */
#endif

#endif
