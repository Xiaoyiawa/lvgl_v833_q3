#include "page_demo.h"

static void slider1_changed(lv_event_t * e);
static void back_click(lv_event_t * e);

lv_obj_t * page_demo() {
    lv_obj_t * screen = lv_obj_create(lv_scr_act());
    //lv_obj_remove_style_all(screen);
    lv_obj_set_size(screen, lv_pct(100), lv_pct(100));

    lv_obj_set_flex_flow(screen, LV_FLEX_FLOW_COLUMN);
	lv_obj_set_scroll_dir(screen, LV_DIR_VER);
	
	lv_obj_t * label1 = lv_label_create(screen);
	lv_label_set_text(label1, "Hello World!");
	lv_obj_align(label1, LV_FLEX_ALIGN_CENTER, 0, 0);

	lv_obj_t * slider1 = lv_slider_create(screen);
	lv_obj_set_size(slider1, lv_pct(80), lv_pct(10));
	lv_obj_align(slider1, LV_FLEX_ALIGN_CENTER, 0, 0);
	lv_slider_set_range(slider1, 1, 255);
	lv_obj_add_event_cb(slider1, slider1_changed, LV_EVENT_VALUE_CHANGED, NULL);
	
	lv_obj_t * img1 = lv_img_create(screen);
	lv_obj_set_size(img1, 128, 128);
	lv_img_set_src(img1, "/mnt/UDISK/lvgl/res/avatar.png");

    lv_obj_t * btn_back = lv_btn_create(screen);
    lv_obj_set_size(btn_back, lv_pct(25), lv_pct(12));
    lv_obj_align(btn_back, LV_ALIGN_TOP_LEFT, 0, 0);
    lv_obj_t * btn_back_label = lv_label_create(btn_back);
    lv_label_set_text(btn_back_label, CUSTOM_SYMBOL_BACK "");
    lv_obj_center(btn_back_label);
    lv_obj_add_event_cb(btn_back, back_click, LV_EVENT_CLICKED, NULL);

    return screen;
}

static void slider1_changed(lv_event_t * e) {
    lv_obj_t * slider = lv_event_get_target(e);
    int value = lv_slider_get_value(slider);
    lcdBrightness(value);
}

static void back_click(lv_event_t * e) {
    page_back();
}
