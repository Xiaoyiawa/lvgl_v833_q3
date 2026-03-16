#ifndef PROJ_PAGE_RECORDER_H
#define PROJ_PAGE_RECORDER_H

#ifdef __cplusplus
extern "C" {
#endif

#include "../lvgl/lvgl.h"
#include "../lv_lib_100ask/lv_lib_100ask.h"
#include "page_manager.h"
#include "platform/alsa_rec.h"

BasePage * recorder_page_create(void);

#ifdef __cplusplus
}
#endif

#endif