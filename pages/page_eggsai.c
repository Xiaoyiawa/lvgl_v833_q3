/**
 * EggAI — AI Chat Page
 *
 * Layout (240x240):
 *   Top bar   (y=0,   h=30):  [<] [model dropdown          ]
 *   Chat area (y=32,  h=var): textarea (read-only, scrollable)
 *   Bottom    (y=var, h=34):  [input box        ] [Send]
 *   Keyboard  (bottom, h=110): lv_keyboard, shown on input focus
 *
 * When keyboard is hidden: bottom bar sits at y=206, chat fills the rest.
 * When keyboard is shown:  keyboard at y=130, bottom bar at y=96, chat shrinks.
 * An invisible overlay covers the rest of the screen to catch outside clicks.
 */

#include "page_eggsai.h"
#include "../lvgl/lvgl.h"
#include "../eggsai/eggsai_provider.h"

/*-----------------------------------------------------
 *  Layout constants (pixel values for 240×240)
 *----------------------------------------------------*/
#define SCREEN_W          240
#define SCREEN_H          240

#define TOP_BAR_H         30
#define BOTTOM_BAR_H      34
#define GAP               2
#define KB_H              110

/* Normal (keyboard hidden) positions */
#define CHAT_Y            (TOP_BAR_H + GAP)                                   /* 32 */
#define BOTTOM_Y_NORMAL   (SCREEN_H - BOTTOM_BAR_H)                          /* 206 */
#define CHAT_H_NORMAL     (BOTTOM_Y_NORMAL - GAP - CHAT_Y)                   /* 172 */

/* Keyboard-shown positions */
#define KB_Y              (SCREEN_H - KB_H)                                   /* 130 */
#define BOTTOM_Y_KB       (KB_Y - BOTTOM_BAR_H)                              /* 96 */
#define CHAT_H_KB         (BOTTOM_Y_KB - GAP - CHAT_Y)                       /* 62 */

#define BACK_BTN_W        30
#define SEND_BTN_W        50

/*-----------------------------------------------------
 *  Static state
 *----------------------------------------------------*/
static EggsaiProvider * s_provider  = NULL;
static int              s_model_idx = 0;

static lv_obj_t * s_screen     = NULL;
static lv_obj_t * s_chat_ta    = NULL;   /* chat display textarea */
static lv_obj_t * s_input_ta   = NULL;   /* user input textarea */
static lv_obj_t * s_send_btn   = NULL;
static lv_obj_t * s_dropdown   = NULL;
static lv_obj_t * s_keyboard   = NULL;
static lv_obj_t * s_kb_overlay = NULL;   /* transparent overlay to catch clicks outside kb */
static bool       s_waiting    = false;  /* true while waiting for API response */
static bool       s_kb_shown   = false;  /* true while keyboard is visible */

/*-----------------------------------------------------
 *  Forward declarations
 *----------------------------------------------------*/
static void back_btn_cb(lv_event_t * e);
static void send_btn_cb(lv_event_t * e);
static void dropdown_cb(lv_event_t * e);
static void input_ta_event_cb(lv_event_t * e);
static void overlay_click_cb(lv_event_t * e);
static void on_ai_response(const char * response, bool success, void * user_data);

static void show_keyboard(void);
static void hide_keyboard(void);

/*-----------------------------------------------------
 *  Append text to chat area
 *----------------------------------------------------*/
static void chat_append(const char * text)
{
    if(!s_chat_ta) return;

    const char * old = lv_textarea_get_text(s_chat_ta);
    size_t old_len = strlen(old);
    size_t add_len = strlen(text);
    size_t new_len = old_len + add_len + 2;

    char * buf = malloc(new_len);
    if(!buf) return;

    if(old_len > 0)
        snprintf(buf, new_len, "%s\n%s", old, text);
    else
        snprintf(buf, new_len, "%s", text);

    lv_textarea_set_text(s_chat_ta, buf);
    free(buf);

    /* Scroll to bottom */
    lv_coord_t sb = lv_obj_get_scroll_bottom(s_chat_ta);
    if(sb > 0) lv_obj_scroll_to_y(s_chat_ta, sb, LV_ANIM_OFF);
}

/*-----------------------------------------------------
 *  Keyboard show / hide
 *----------------------------------------------------*/
static void show_keyboard(void)
{
    if(s_kb_shown) return;
    s_kb_shown = true;

    /* Show keyboard & overlay */
    lv_obj_clear_flag(s_keyboard, LV_OBJ_FLAG_HIDDEN);
    lv_obj_clear_flag(s_kb_overlay, LV_OBJ_FLAG_HIDDEN);
    
    /* Move overlay to foreground, then input box, then keyboard.
     * This leaves the Send button and Chat area behind the overlay.
     * So clicking Send will hit the overlay and hide the keyboard. */
    lv_obj_move_foreground(s_kb_overlay);
    lv_obj_move_foreground(s_input_ta);
    lv_obj_move_foreground(s_keyboard);

    /* Move bottom bar up */
    lv_obj_set_pos(s_input_ta, 0, BOTTOM_Y_KB);
    lv_obj_set_pos(s_send_btn, SCREEN_W - SEND_BTN_W, BOTTOM_Y_KB);

    /* Shrink chat area */
    lv_obj_set_size(s_chat_ta, SCREEN_W, CHAT_H_KB);
    
    /* Focus the text area */
    lv_obj_add_state(s_input_ta, LV_STATE_FOCUSED);
}

static void hide_keyboard(void)
{
    if(!s_kb_shown) return;
    s_kb_shown = false;

    /* Hide keyboard & overlay */
    lv_obj_add_flag(s_keyboard, LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_flag(s_kb_overlay, LV_OBJ_FLAG_HIDDEN);

    /* Move bottom bar back down */
    lv_obj_set_pos(s_input_ta, 0, BOTTOM_Y_NORMAL);
    lv_obj_set_pos(s_send_btn, SCREEN_W - SEND_BTN_W, BOTTOM_Y_NORMAL);

    /* Restore chat area */
    lv_obj_set_size(s_chat_ta, SCREEN_W, CHAT_H_NORMAL);

    /* Remove focus from input */
    lv_obj_clear_state(s_input_ta, LV_STATE_FOCUSED);

    /* Scroll chat to bottom */
    lv_coord_t sb = lv_obj_get_scroll_bottom(s_chat_ta);
    if(sb > 0) lv_obj_scroll_to_y(s_chat_ta, sb, LV_ANIM_OFF);
}

/*-----------------------------------------------------
 *  Page constructor
 *----------------------------------------------------*/
static lv_obj_t * page_eggsai_obj(void)
{
    /* ---- Root container (full screen) ---- */
    lv_obj_t * screen = lv_obj_create(lv_scr_act());
    s_screen = screen;
    lv_obj_set_size(screen, SCREEN_W, SCREEN_H);
    lv_obj_set_pos(screen, 0, 0);
    lv_obj_set_style_pad_all(screen, 0, 0);
    lv_obj_set_style_border_width(screen, 0, 0);
    lv_obj_set_style_radius(screen, 0, 0);
    lv_obj_clear_flag(screen, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_bg_opa(screen, LV_OPA_COVER, 0);
    lv_obj_set_style_bg_color(screen, lv_color_hex(0xFFFFFF), 0);

    /* ---- Initialize provider ---- */
    s_provider = eggsai_provider_get_default();
    s_provider->load_configs(s_provider);
    s_model_idx = 0;
    s_waiting = false;
    s_kb_shown = false;

    /* ============================================================
     *  TOP BAR  (y=0, h=TOP_BAR_H)
     * ============================================================ */

    /* Back button */
    lv_obj_t * btn_back = lv_btn_create(screen);
    lv_obj_set_size(btn_back, BACK_BTN_W, TOP_BAR_H);
    lv_obj_set_pos(btn_back, 0, 0);
    lv_obj_set_style_radius(btn_back, 4, 0);
    lv_obj_set_style_pad_all(btn_back, 0, 0);
    lv_obj_t * lbl_back = lv_label_create(btn_back);
    lv_label_set_text(lbl_back, LV_SYMBOL_LEFT);
    lv_obj_center(lbl_back);
    lv_obj_add_event_cb(btn_back, back_btn_cb, LV_EVENT_CLICKED, NULL);

    /* Model dropdown */
    s_dropdown = lv_dropdown_create(screen);
    lv_obj_set_pos(s_dropdown, BACK_BTN_W + GAP, 0);
    lv_obj_set_size(s_dropdown, SCREEN_W - BACK_BTN_W - GAP, TOP_BAR_H);
    
    /* Ensure border covers the dropdown clearly and uniformly */
    lv_obj_set_style_border_width(s_dropdown, 1, 0);
    lv_obj_set_style_border_color(s_dropdown, lv_color_hex(0x888888), 0);
    lv_obj_set_style_radius(s_dropdown, 4, 0);
    /* Set inner padding so text isn't cramped on the border */
    lv_obj_set_style_pad_all(s_dropdown, 4, 0);

    {
        char opts[256] = "";
        for(int i = 0; i < s_provider->model_count; i++) {
            if(i > 0) strcat(opts, "\n");
            strcat(opts, s_provider->models[i].name);
        }
        lv_dropdown_set_options(s_dropdown, opts);
    }
    lv_dropdown_set_selected(s_dropdown, 0);
    lv_obj_add_event_cb(s_dropdown, dropdown_cb, LV_EVENT_VALUE_CHANGED, NULL);

    /* ============================================================
     *  CHAT DISPLAY  (y=CHAT_Y, h=CHAT_H_NORMAL)
     * ============================================================ */
    s_chat_ta = lv_textarea_create(screen);
    lv_obj_set_pos(s_chat_ta, 0, CHAT_Y);
    lv_obj_set_size(s_chat_ta, SCREEN_W, CHAT_H_NORMAL);
    lv_textarea_set_text(s_chat_ta, "");
    lv_textarea_set_cursor_click_pos(s_chat_ta, false);

    /* Hide cursor completely */
    lv_obj_set_style_anim_time(s_chat_ta, 0, LV_PART_CURSOR);
    lv_obj_set_style_border_width(s_chat_ta, 0, LV_PART_CURSOR);
    lv_obj_set_style_bg_opa(s_chat_ta, LV_OPA_TRANSP, LV_PART_CURSOR);
    lv_obj_set_style_opa(s_chat_ta, LV_OPA_TRANSP, LV_PART_CURSOR);
    lv_obj_set_style_width(s_chat_ta, 0, LV_PART_CURSOR);

    lv_obj_clear_flag(s_chat_ta, LV_OBJ_FLAG_CLICK_FOCUSABLE);

    /* Border and styling */
    lv_obj_set_style_border_width(s_chat_ta, 1, 0);
    lv_obj_set_style_border_color(s_chat_ta, lv_color_hex(0x888888), 0);
    lv_obj_set_style_radius(s_chat_ta, 4, 0);
    lv_obj_set_style_pad_all(s_chat_ta, 4, 0);

    /* ============================================================
     *  KEYBOARD OVERLAY  (hidden initially)
     * ============================================================ */
    /* This catches clicks outside the keyboard/input when active */
    s_kb_overlay = lv_obj_create(screen);
    lv_obj_set_size(s_kb_overlay, SCREEN_W, SCREEN_H);
    lv_obj_set_pos(s_kb_overlay, 0, 0);
    
    /* Transparent overaly per user request */
    lv_obj_set_style_bg_opa(s_kb_overlay, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(s_kb_overlay, 0, 0);
    lv_obj_set_style_radius(s_kb_overlay, 0, 0);
    lv_obj_clear_flag(s_kb_overlay, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(s_kb_overlay, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_flag(s_kb_overlay, LV_OBJ_FLAG_HIDDEN);
    
    /* Hide keyboard when overlay clicked */
    lv_obj_add_event_cb(s_kb_overlay, overlay_click_cb, LV_EVENT_CLICKED, NULL);

    /* ============================================================
     *  BOTTOM BAR  (y=BOTTOM_Y_NORMAL, h=BOTTOM_BAR_H)
     * ============================================================ */

    /* Send button */
    /* We create Send btn BEFORE input so it sits behind the overlay */
    s_send_btn = lv_btn_create(screen);
    lv_obj_set_pos(s_send_btn, SCREEN_W - SEND_BTN_W, BOTTOM_Y_NORMAL);
    lv_obj_set_size(s_send_btn, SEND_BTN_W, BOTTOM_BAR_H);
    lv_obj_set_style_radius(s_send_btn, 4, 0);
    lv_obj_set_style_pad_all(s_send_btn, 0, 0);
    lv_obj_t * lbl_send = lv_label_create(s_send_btn);
    lv_label_set_text(lbl_send, "Send");
    lv_obj_center(lbl_send);
    lv_obj_add_event_cb(s_send_btn, send_btn_cb, LV_EVENT_CLICKED, NULL);

    /* Input textarea */
    s_input_ta = lv_textarea_create(screen);
    lv_obj_set_pos(s_input_ta, 0, BOTTOM_Y_NORMAL);
    lv_obj_set_size(s_input_ta, SCREEN_W - SEND_BTN_W - GAP, BOTTOM_BAR_H);
    lv_textarea_set_text(s_input_ta, "");
    lv_textarea_set_one_line(s_input_ta, true);
    lv_textarea_set_placeholder_text(s_input_ta, "Ask something...");
    
    lv_obj_set_style_border_width(s_input_ta, 1, 0);
    lv_obj_set_style_border_color(s_input_ta, lv_color_hex(0x888888), 0);
    lv_obj_set_style_radius(s_input_ta, 4, 0);
    lv_obj_set_style_pad_all(s_input_ta, 4, 0);

    /* Only show keyboard on EXPLICIT clicks to avoid LV_EVENT_ALL side-effects */
    lv_obj_add_event_cb(s_input_ta, input_ta_event_cb, LV_EVENT_CLICKED, NULL);
    lv_obj_add_event_cb(s_input_ta, input_ta_event_cb, LV_EVENT_FOCUSED, NULL);

    /* ============================================================
     *  KEYBOARD  (hidden initially)
     * ============================================================ */
    s_keyboard = lv_keyboard_create(screen);
    lv_obj_set_size(s_keyboard, SCREEN_W, KB_H);
    /* Do NOT set pos here. lv_keyboard_create already calls lv_obj_align(..., LV_ALIGN_BOTTOM_MID)
       so setting pos will push it off-screen! */
    lv_keyboard_set_textarea(s_keyboard, s_input_ta);
    lv_obj_add_flag(s_keyboard, LV_OBJ_FLAG_HIDDEN);

    return screen;
}

BasePage * eggsai_page_create(void)
{
    return base_page_create(page_eggsai_obj(), NULL, NULL);
}

/*-----------------------------------------------------
 *  Callbacks
 *----------------------------------------------------*/

static void back_btn_cb(lv_event_t * e)
{
    (void)e;
    s_screen     = NULL;
    s_chat_ta    = NULL;
    s_input_ta   = NULL;
    s_send_btn   = NULL;
    s_dropdown   = NULL;
    s_keyboard   = NULL;
    s_kb_overlay = NULL;
    s_kb_shown   = false;
    page_back();
}

static void dropdown_cb(lv_event_t * e)
{
    (void)e;
    s_model_idx = lv_dropdown_get_selected(s_dropdown);
}

static void input_ta_event_cb(lv_event_t * e)
{
    (void)e;
    show_keyboard();
}

static void overlay_click_cb(lv_event_t * e)
{
    (void)e;
    hide_keyboard();
}

static void send_btn_cb(lv_event_t * e)
{
    (void)e;

    /* If keyboard is somehow visible and send is clicked (should be blocked by overlay,
       but just in case), hide it. */
    if(s_kb_shown) {
        hide_keyboard();
        return;
    }

    if(s_waiting) return;
    if(!s_input_ta || !s_chat_ta) return;

    const char * text = lv_textarea_get_text(s_input_ta);
    if(!text || strlen(text) == 0) return;

    char user_buf[512];
    snprintf(user_buf, sizeof(user_buf), "You: %s", text);
    chat_append(user_buf);

    char * msg_copy = strdup(text);
    lv_textarea_set_text(s_input_ta, "");

    chat_append("AI: ...");
    s_waiting = true;

    if(!s_provider || s_provider->send_message(s_provider, s_model_idx, msg_copy,
                                               on_ai_response, NULL) != 0) {
        chat_append("Error: Failed to send message");
        s_waiting = false;
    }

    free(msg_copy);
}

static void on_ai_response(const char * response, bool success, void * user_data)
{
    (void)user_data;
    (void)success;
    s_waiting = false;

    if(!s_chat_ta) return;

    if(response && strlen(response) > 0) {
        char ai_buf[EGGSAI_RESPONSE_MAX_LEN + 8];
        snprintf(ai_buf, sizeof(ai_buf), "AI: %s", response);

        const char * old = lv_textarea_get_text(s_chat_ta);
        const char * placeholder = "AI: ...";
        const char * last_ph = NULL;

        const char * p = old;
        while((p = strstr(p, placeholder)) != NULL) {
            last_ph = p;
            p += strlen(placeholder);
        }

        if(last_ph) {
            size_t prefix_len = last_ph - old;
            size_t reply_len = strlen(ai_buf);
            size_t suffix_len = strlen(last_ph + strlen(placeholder));
            size_t total = prefix_len + reply_len + suffix_len + 1;

            char * buf = malloc(total);
            if(buf) {
                memcpy(buf, old, prefix_len);
                memcpy(buf + prefix_len, ai_buf, reply_len);
                memcpy(buf + prefix_len + reply_len,
                       last_ph + strlen(placeholder), suffix_len);
                buf[prefix_len + reply_len + suffix_len] = '\0';
                lv_textarea_set_text(s_chat_ta, buf);
                free(buf);
            }
        } else {
            chat_append(ai_buf);
        }
    } else {
        chat_append("AI: (no response)");
    }

    lv_coord_t sb = lv_obj_get_scroll_bottom(s_chat_ta);
    if(sb > 0) lv_obj_scroll_to_y(s_chat_ta, sb, LV_ANIM_OFF);
}
