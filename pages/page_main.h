#ifndef PROJ_PAGE_MAIN_H
#define PROJ_PAGE_MAIN_H

#ifdef __cplusplus
extern "C" {
#endif

/*********************
 *      INCLUDES
 *********************/
#include "../lvgl/lvgl.h"
#include "../lv_lib_100ask/lv_lib_100ask.h"
#include "page_manager.h"
#include <sys/time.h>
#include <time.h>
#include <unistd.h>
#include <stdio.h>
#include <stdlib.h>

/*********************
 *      DEFINES
 *********************/

/**********************
 *      TYPEDEFS
 **********************/
typedef struct
{
    BasePage base;   // 基类对象
    lv_timer_t * timer_time;
    lv_timer_t * timer_battery;
    lv_obj_t * label_time;
    lv_obj_t * label_battery;

} MainPage;

/**********************
 * GLOBAL PROTOTYPES
 **********************/
lv_obj_t * page_main();

/**********************
 *      MACROS
 **********************/

#ifdef __cplusplus
} /* extern "C" */
#endif

#endif
