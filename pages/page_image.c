#include "page_image.h"

static double scale_default;
static bool touching;
static bool moved;
static bool ui_hidden;
static lv_obj_t * img;
static lv_obj_t * btn_back;
static lv_obj_t * slider_scale;
static lv_obj_t * touch_area;
static lv_coord_t touch_last_x;
static lv_coord_t touch_last_y;
static lv_img_header_t header;
static lv_coord_t img_scaled_w;
static lv_coord_t img_scaled_h;

static void back_click(lv_event_t * e);
static void slider_scale_changed(lv_event_t * e);
static void touch_pressing(lv_event_t * e);

lv_obj_t * page_image(char * src)
{
    lv_obj_t * screen = lv_obj_create(lv_scr_act());
    lv_obj_remove_style_all(screen);
    lv_obj_set_size(screen, lv_pct(100), lv_pct(100));
    lv_obj_clear_flag(screen, LV_OBJ_FLAG_SCROLLABLE);

    touching = false;
    ui_hidden = false;

    ffmpeg_get_img_header(src, &header);

    img = lv_img_create(screen);
    lv_obj_set_size(img, header.w, header.h);
    lv_obj_center(img);
    if(header.w != 0 && header.h != 0){
        lv_img_set_src(img, src);
        scale_default = fmin((double)LV_SCR_WIDTH / header.w, (double)LV_SCR_HEIGHT / header.h);
        img_scaled_w  = (lv_coord_t)(header.w * scale_default);
        img_scaled_h  = (lv_coord_t)(header.h * scale_default);
        LV_LOG_USER("[page_image]%dx%d, scale=%.8g\n", header.w, header.h, scale_default);
        lv_obj_set_style_transform_zoom(img, (lv_coord_t)(256 * scale_default), 0);
    }

    touch_area = lv_obj_create(screen);
    lv_obj_set_size(touch_area, LV_SCR_WIDTH, LV_SCR_HEIGHT);
    lv_obj_set_pos(touch_area, 0, 0);
    lv_obj_set_style_bg_opa(touch_area, LV_OPA_0, 0);
    lv_obj_set_style_border_opa(touch_area, LV_OPA_0, 0);
    lv_obj_set_style_outline_opa(touch_area, LV_OPA_0, 0);
    lv_obj_set_style_shadow_opa(touch_area, LV_OPA_0, 0);
    lv_obj_add_event_cb(touch_area, touch_pressing, LV_EVENT_PRESSED, NULL);
    lv_obj_add_event_cb(touch_area, touch_pressing, LV_EVENT_PRESSING, NULL);
    lv_obj_add_event_cb(touch_area, touch_pressing, LV_EVENT_RELEASED, NULL);
    lv_obj_add_event_cb(touch_area, touch_pressing, LV_EVENT_CLICKED, NULL);

    slider_scale = lv_slider_create(screen);
    lv_obj_set_size(slider_scale, lv_pct(10), lv_pct(80));
    lv_obj_align(slider_scale, LV_ALIGN_LEFT_MID, lv_pct(85), 0);
    lv_slider_set_range(slider_scale, 1, 10);
    lv_slider_set_value(slider_scale, 1, LV_ANIM_OFF);
    lv_obj_add_event_cb(slider_scale, slider_scale_changed, LV_EVENT_VALUE_CHANGED, NULL);


    btn_back = lv_btn_create(screen);
    lv_obj_set_size(btn_back, lv_pct(25), lv_pct(12));
    lv_obj_align(btn_back, LV_ALIGN_TOP_LEFT, 0, 0);
    lv_obj_t * btn_back_label = lv_label_create(btn_back);
    lv_label_set_text(btn_back_label, CUSTOM_SYMBOL_BACK "");
    lv_obj_center(btn_back_label);
    lv_obj_add_event_cb(btn_back, back_click, LV_EVENT_CLICKED, NULL);

    return screen;
}

static void back_click(lv_event_t * e)
{
    page_back();
}

static void slider_scale_changed(lv_event_t * e)
{
    lv_obj_t * slider = lv_event_get_target(e);
    int value         = lv_slider_get_value(slider);
    double scale_new  = value * scale_default;
    img_scaled_w      = (lv_coord_t)(header.w * scale_new);
    img_scaled_h      = (lv_coord_t)(header.h * scale_new);
    lv_obj_set_style_transform_zoom(img, (int)(256 * scale_new), 0);
}

static void touch_pressing(lv_event_t * e)
{
    lv_point_t point;
    lv_indev_get_point(lv_indev_get_act(), &point);

    switch(e->code) {
        case LV_EVENT_PRESSED:
            touching     = true;
            moved        = false;
            touch_last_x = point.x;
            touch_last_y = point.y;
            break;

        case LV_EVENT_PRESSING: 
            if(touching) {
                lv_coord_t dx = point.x - touch_last_x;
                lv_coord_t dy = point.y - touch_last_y;
                if(abs(dx) > 0 || abs(dy) > 0) {
                    moved = true;
                    lv_obj_set_pos(img, lv_obj_get_x_aligned(img) + dx, lv_obj_get_y_aligned(img) + dy);
                }
                touch_last_x = point.x;
                touch_last_y = point.y;
            }
            break;
        

        case LV_EVENT_RELEASED: 
            if(!moved) {
                ui_hidden = !ui_hidden;
                if(ui_hidden) {
                    lv_obj_add_flag(btn_back, LV_OBJ_FLAG_HIDDEN);
                    lv_obj_add_flag(slider_scale, LV_OBJ_FLAG_HIDDEN);
                }
                else {    
                    lv_obj_clear_flag(btn_back, LV_OBJ_FLAG_HIDDEN);
                    lv_obj_clear_flag(slider_scale, LV_OBJ_FLAG_HIDDEN);
                }
            }
            touching = false; 
            break;

        default: break;
    }
}