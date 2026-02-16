#include "page_apple.h"

static void back_click(lv_event_t * e);

static ff_player_t * ffplayer;

lv_obj_t * page_apple(char * filename)
{
    lv_obj_t * screen = lv_obj_create(lv_scr_act());
    lv_obj_remove_style_all(screen);
    lv_obj_set_size(screen, lv_pct(100), lv_pct(100));

    setDontDeepSleep(true);
    system("echo 1 > /dev/spk_crtl");

    lv_img_header_t header;
    ffmpeg_get_img_header(filename, &header);
    lv_obj_t * img = lv_img_create(screen);
    double scale_default  = fmin((double)LV_SCR_WIDTH / header.w, (double)LV_SCR_HEIGHT / header.h);
    lv_coord_t img_scaled_w   = (lv_coord_t)(header.w * scale_default);
    lv_coord_t img_scaled_h   = (lv_coord_t)(header.h * scale_default);
    lv_obj_set_size(img, img_scaled_w, img_scaled_h);
    lv_obj_center(img);

    ffplayer = player_create();
    if(player_open(ffplayer, filename) == 0 
        && player_init_audio(ffplayer) == 0 && player_init_video(ffplayer, img) == 0) {
        player_resume(ffplayer);
    } else
        ffplayer = NULL;

    lv_obj_t * btn_back = lv_btn_create(screen);
    lv_obj_set_size(btn_back, lv_pct(25), lv_pct(12));
    lv_obj_align(btn_back, LV_ALIGN_BOTTOM_LEFT, 0, 0);
    lv_obj_t * btn_back_label = lv_label_create(btn_back);
    lv_label_set_text(btn_back_label, "back");
    lv_obj_center(btn_back_label);
    lv_obj_add_event_cb(btn_back, back_click, LV_EVENT_CLICKED, NULL);

    return screen;
}

static void back_click(lv_event_t * e)
{
    system("echo 0 > /dev/spk_crtl");
    setDontDeepSleep(false);
    if(ffplayer) player_destroy(ffplayer);
    page_back();
}