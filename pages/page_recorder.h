#ifndef PROJ_PAGE_RECORDER_H
#define PROJ_PAGE_RECORDER_H

#ifdef __cplusplus
extern "C" {
#endif

#include "../lvgl/lvgl.h"
#include "../lv_lib_100ask/lv_lib_100ask.h"
#include "page_manager.h"
#include "platform/alsa_rec.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <time.h>
#include <sys/stat.h>
#include <sys/statfs.h>
#include <errno.h>

BasePage * recorder_page_create(void);

#ifdef __cplusplus
}
#endif

#endif