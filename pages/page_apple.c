#include "page_apple.h"

static bool ui_hidden;
static lv_obj_t * btn_control;
static lv_obj_t * btn_control_label;
static ff_player_t * player;
static lv_obj_t * slider_volume;
static lv_obj_t * slider_progress;
static lv_obj_t * btn_back;
static lv_timer_t * timer;

static void back_click(lv_event_t * e);
static void slider_progress_released(lv_event_t * e);
static void slider_volume_changed(lv_event_t * e);
static void touch_clicked(lv_event_t * e);
static void control_click(lv_event_t * e);
static void timer_tick(lv_event_t * e);
static void player_finished(ff_player_t p);

lv_obj_t * page_apple(char * filename)
{
    lv_obj_t * screen = lv_obj_create(lv_scr_act());
    lv_obj_remove_style_all(screen);
    lv_obj_set_size(screen, lv_pct(100), lv_pct(100));

    setDontDeepSleep(true);
    audio_enable();

    lv_img_header_t header;
    ffmpeg_get_img_header(filename, &header);
    lv_obj_t * img = lv_img_create(screen);
    double scale_default  = fmin((double)LV_SCR_WIDTH / header.w, (double)LV_SCR_HEIGHT / header.h);
    lv_coord_t img_scaled_w   = (lv_coord_t)(header.w * scale_default);
    lv_coord_t img_scaled_h   = (lv_coord_t)(header.h * scale_default);
    lv_obj_set_size(img, img_scaled_w, img_scaled_h);
    lv_obj_center(img);

    player = player_create();
    if(player_open(player, filename) == 0 
        && player_init_audio(player) == 0 && player_init_video(player, img) == 0) {
        player_resume(player);
        player_set_finish_callback(player, player_finished);
    } 
    else
        player = NULL;

    lv_obj_t * touch_area = lv_obj_create(screen);
    lv_obj_set_size(touch_area, LV_SCR_WIDTH, LV_SCR_HEIGHT);
    lv_obj_set_pos(touch_area, 0, 0);
    lv_obj_set_style_bg_opa(touch_area, LV_OPA_0, 0);
    lv_obj_set_style_border_opa(touch_area, LV_OPA_0, 0);
    lv_obj_set_style_outline_opa(touch_area, LV_OPA_0, 0);
    lv_obj_set_style_shadow_opa(touch_area, LV_OPA_0, 0);
    lv_obj_add_event_cb(touch_area, touch_clicked, LV_EVENT_CLICKED, NULL);

    slider_progress = lv_slider_create(screen);
    lv_obj_set_size(slider_progress, lv_pct(90), lv_pct(10));
    lv_obj_align(slider_progress, LV_ALIGN_TOP_MID, 0, lv_pct(90));
    lv_slider_set_range(slider_progress, 0, 100);
    lv_obj_add_event_cb(slider_progress, slider_progress_released, LV_EVENT_RELEASED, NULL);

    slider_volume = lv_slider_create(screen);
    lv_obj_set_size(slider_volume, lv_pct(10), lv_pct(60));
    lv_obj_align(slider_volume, LV_ALIGN_LEFT_MID, lv_pct(90), 0);
    lv_slider_set_range(slider_volume, 0, 100);
    lv_slider_set_value(slider_volume, audio_volume_get(), LV_ANIM_OFF);
    lv_obj_add_event_cb(slider_volume, slider_volume_changed, LV_EVENT_VALUE_CHANGED, NULL);

    btn_control = lv_btn_create(screen);
    lv_obj_set_size(btn_control, lv_pct(25), lv_pct(25));
    lv_obj_align(btn_control, LV_ALIGN_CENTER, 0, 0);
    lv_obj_set_style_bg_opa(btn_control, LV_OPA_50, 0);
    lv_obj_set_style_border_opa(btn_control, LV_OPA_50, 0);
    lv_obj_set_style_outline_opa(btn_control, LV_OPA_50, 0);
    lv_obj_set_style_shadow_opa(btn_control, LV_OPA_50, 0);
    btn_control_label = lv_label_create(btn_control);
    lv_label_set_text(btn_control_label, LV_SYMBOL_PAUSE "");
    lv_obj_center(btn_control_label);
    lv_obj_add_event_cb(btn_control, control_click, LV_EVENT_CLICKED, player);

    btn_back = lv_btn_create(screen);
    lv_obj_set_size(btn_back, lv_pct(25), lv_pct(12));
    lv_obj_align(btn_back, LV_ALIGN_TOP_LEFT, 0, 0);
    lv_obj_t * btn_back_label = lv_label_create(btn_back);
    lv_label_set_text(btn_back_label, CUSTOM_SYMBOL_BACK "");
    lv_obj_center(btn_back_label);
    lv_obj_add_event_cb(btn_back, back_click, LV_EVENT_CLICKED, NULL);
        
    timer = lv_timer_create(timer_tick, 250, NULL);

    return screen;
}

static void control_click(lv_event_t * e)
{
    if(!player) return;
    player_state_t state = player_get_state(player);
    if(state == PLAYER_PAUSED) {
        audio_enable();
        player_resume(player);
        lv_label_set_text(btn_control_label, LV_SYMBOL_PAUSE "");
    }
    if(state == PLAYER_PLAYING) {
        player_pause(player);
        lv_label_set_text(btn_control_label, LV_SYMBOL_PLAY "");
        audio_disable();
    }
}

static void player_finished(ff_player_t p)
{
    lv_label_set_text(btn_control_label, LV_SYMBOL_PLAY "");
    audio_disable();
}

static void slider_volume_changed(lv_event_t * e)
{
    lv_obj_t * slider = lv_event_get_target(e);
    int value         = lv_slider_get_value(slider);
    
    audio_volume_set(value);
}

static void slider_progress_released(lv_event_t * e)
{
    if(!player) return;
    player_state_t state = player_get_state(player);
    if(state == PLAYER_PLAYING || state == PLAYER_PAUSED) {
        lv_obj_t * slider = lv_event_get_target(e);
        int value         = lv_slider_get_value(slider);
        player_seek_pct(player, value);
    }
}

static void touch_clicked(lv_event_t * e) 
{
    ui_hidden = !ui_hidden;
    if(ui_hidden) {
        lv_obj_add_flag(slider_volume, LV_OBJ_FLAG_HIDDEN);
        lv_obj_add_flag(slider_progress, LV_OBJ_FLAG_HIDDEN);
        lv_obj_add_flag(btn_control, LV_OBJ_FLAG_HIDDEN);
        lv_obj_add_flag(btn_back, LV_OBJ_FLAG_HIDDEN);
    } else {
        lv_obj_clear_flag(slider_volume, LV_OBJ_FLAG_HIDDEN);
        lv_obj_clear_flag(slider_progress, LV_OBJ_FLAG_HIDDEN);
        lv_obj_clear_flag(btn_control, LV_OBJ_FLAG_HIDDEN);
        lv_obj_clear_flag(btn_back, LV_OBJ_FLAG_HIDDEN);
    }
}

static void timer_tick(lv_event_t * e)
{
    if(!player) return;
    lv_slider_set_value(slider_progress, player_get_position_pct(player), LV_ANIM_OFF);
}

static void back_click(lv_event_t * e)
{
    if(timer) lv_timer_del(timer);
    if(player) player_destroy(player);
    player = NULL;
    audio_disable();
    setDontDeepSleep(false);
    page_back();
}