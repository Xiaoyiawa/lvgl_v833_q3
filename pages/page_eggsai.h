#ifndef PROJ_PAGE_EGGSAI_H
#define PROJ_PAGE_EGGSAI_H

#ifdef __cplusplus
extern "C" {
#endif

/*********************
 *      INCLUDES
 *********************/
#include "../lvgl/lvgl.h"
#include "../lv_lib_100ask/lv_lib_100ask.h"
#include "../platform/eggsai_provider.h"
#include "../cJSON/cJSON.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "page_manager.h"

/*********************
 *      DEFINES
 *********************/

#define SCREEN_W 240
#define SCREEN_H 240

#define TOP_BAR_H 30
#define BOTTOM_BAR_H 34
#define GAP 2
#define KB_H 110

/* Normal (keyboard hidden) positions */
#define CHAT_Y (TOP_BAR_H + GAP)                       /* 32 */
#define BOTTOM_Y_NORMAL (SCREEN_H - BOTTOM_BAR_H)      /* 206 */
#define CHAT_H_NORMAL (BOTTOM_Y_NORMAL - GAP - CHAT_Y) /* 172 */

/* Keyboard-shown positions */
#define KB_Y (SCREEN_H - KB_H)                 /* 130 */
#define BOTTOM_Y_KB (KB_Y - BOTTOM_BAR_H)      /* 96 */
#define CHAT_H_KB (BOTTOM_Y_KB - GAP - CHAT_Y) /* 62 */

#define BACK_BTN_W 30
#define SEND_BTN_W 50

/**********************
 *      TYPEDEFS
 **********************/

/**********************
 * GLOBAL PROTOTYPES
 **********************/
lv_obj_t * page_eggsai(void);

/**********************
 *      MACROS
 **********************/

#ifdef __cplusplus
} /* extern "C" */
#endif

#endif /* PROJ_PAGE_EGGSAI_H */
