#include "page_recorder.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <time.h>
#include <sys/stat.h>
#include <sys/statfs.h>
#include <errno.h>

#define RECORDER_DEBUG(fmt, ...) printf("[Recorder] " fmt "\n", ##__VA_ARGS__)

// 页面私有数据结构
typedef struct {
    BasePage base;

    // UI 对象指针
    lv_obj_t *label_space;
    lv_obj_t *label_status;
    lv_obj_t *label_duration;
    lv_obj_t *label_filename;
    lv_obj_t *btn_record;
    lv_obj_t *btn_record_label;
    lv_obj_t *btn_back;
    lv_obj_t *btn_delete;

    // 对话框相关
    lv_obj_t *dialog_bg;
    lv_obj_t *dialog_box;
    lv_obj_t *dialog_label;
    lv_obj_t *dialog_btn_confirm;
    lv_obj_t *dialog_btn_cancel;
    bool      dialog_showing;

    // 录音后端
    AlsaRecorder *recorder;

    // 当前文件名（如果有）
    char current_filename[256];

    // 定时器（用于更新时长和检查状态）
    lv_timer_t *timer;

    // 标志：是否有已保存的文件（用于返回时判断是否删除）
    bool file_saved;   // 录音正常停止后设为 true，放弃录音或清空后设为 false
} RecorderPage;

// 静态函数声明
static lv_obj_t *recorder_ui_create(RecorderPage *page);
static void recorder_on_destroy(void *p);

static void btn_record_cb(lv_event_t *e);
static void btn_back_cb(lv_event_t *e);
static void btn_delete_cb(lv_event_t *e);
static void dialog_confirm_cb(lv_event_t *e);
static void dialog_cancel_cb(lv_event_t *e);

static void timer_cb(lv_timer_t *timer);
static void recorder_finish_cb(void *user_data);  // 录音后端完成的回调

static int ensure_rec_dir_exists(void);
static void update_space_info(RecorderPage *page);
static void generate_filename(char *buffer, size_t size);
static void update_ui_state(RecorderPage *page);
static void show_delete_dialog(RecorderPage *page);
static void hide_delete_dialog(RecorderPage *page);
static void delete_all_recordings(RecorderPage *page);

// ---------- 页面创建函数 ----------
BasePage *recorder_page_create(void)
{
    RecorderPage *page = malloc(sizeof(RecorderPage));
    if (!page) return NULL;
    memset(page, 0, sizeof(RecorderPage));

    // 创建 UI，返回的 obj 作为 BasePage 的 obj
    page->base.obj = recorder_ui_create(page);
    page->base.on_destroy = recorder_on_destroy;

    RECORDER_DEBUG("Page created");
    return (BasePage *)page;
}

// ---------- UI 创建 ----------
static lv_obj_t *recorder_ui_create(RecorderPage *page)
{
    lv_obj_t *scr = lv_obj_create(lv_scr_act());
    lv_obj_remove_style_all(scr);
    lv_obj_set_size(scr, lv_pct(100), lv_pct(100));
    lv_obj_set_style_bg_color(scr, lv_color_white(), 0);

    // 剩余空间标签
    page->label_space = lv_label_create(scr);
    lv_obj_set_width(page->label_space, lv_pct(90));
    lv_obj_align(page->label_space, LV_ALIGN_TOP_MID, 0, 10);
    lv_obj_set_style_text_align(page->label_space, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_style_text_color(page->label_space, lv_color_hex(0x888888), 0);

    // 状态标签
    page->label_status = lv_label_create(scr);
    lv_obj_set_width(page->label_status, lv_pct(90));
    lv_obj_align(page->label_status, LV_ALIGN_TOP_MID, 0, 35);
    lv_label_set_text(page->label_status, "状态: 空闲");
    lv_obj_set_style_text_align(page->label_status, LV_TEXT_ALIGN_CENTER, 0);

    // 时长标签
    page->label_duration = lv_label_create(scr);
    lv_obj_set_width(page->label_duration, lv_pct(90));
    lv_obj_align(page->label_duration, LV_ALIGN_TOP_MID, 0, 58);
    lv_label_set_text(page->label_duration, "时长: 00:00");
    lv_obj_set_style_text_align(page->label_duration, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_style_text_color(page->label_duration, lv_color_hex(0x666666), 0);

    // 主按钮
    page->btn_record = lv_btn_create(scr);
    lv_obj_set_size(page->btn_record, 145, 65);
    lv_obj_align(page->btn_record, LV_ALIGN_CENTER, 0, 0);
    lv_obj_add_event_cb(page->btn_record, btn_record_cb, LV_EVENT_CLICKED, page);
    page->btn_record_label = lv_label_create(page->btn_record);
    lv_label_set_text(page->btn_record_label, "开始录音");
    lv_obj_center(page->btn_record_label);

    // 文件名标签
    page->label_filename = lv_label_create(scr);
    lv_obj_set_width(page->label_filename, lv_pct(90));
    lv_obj_align(page->label_filename, LV_ALIGN_CENTER, 0, 55);
    lv_label_set_text(page->label_filename, "文件: (未开始)");
    lv_obj_set_style_text_align(page->label_filename, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_style_text_color(page->label_filename, lv_color_hex(0x666666), 0);

    // 返回按钮
    page->btn_back = lv_btn_create(scr);
    lv_obj_set_size(page->btn_back, lv_pct(25), lv_pct(12));
    lv_obj_align(page->btn_back, LV_ALIGN_BOTTOM_LEFT, 0, 0);
    lv_obj_t *lbl_back = lv_label_create(page->btn_back);
    lv_label_set_text(lbl_back, CUSTOM_SYMBOL_BACK "");
    lv_obj_center(lbl_back);
    lv_obj_add_event_cb(page->btn_back, btn_back_cb, LV_EVENT_CLICKED, page);

    // 清空按钮
    page->btn_delete = lv_btn_create(scr);
    lv_obj_set_size(page->btn_delete, 45, 28);
    lv_obj_align(page->btn_delete, LV_ALIGN_BOTTOM_RIGHT, -2, 0);
    lv_obj_t *lbl_delete = lv_label_create(page->btn_delete);
    lv_label_set_text(lbl_delete, "清空");
    lv_obj_center(lbl_delete);
    lv_obj_set_style_bg_color(page->btn_delete, lv_color_hex(0xFF4444), LV_PART_MAIN);
    lv_obj_set_style_text_color(lbl_delete, lv_color_white(), 0);
    lv_obj_add_event_cb(page->btn_delete, btn_delete_cb, LV_EVENT_CLICKED, page);

    // 创建录音后端对象
    page->recorder = alsa_recorder_create("default", 16000, 1, SND_PCM_FORMAT_S16_LE);
    if (!page->recorder) {
        lv_label_set_text(page->label_status, "状态: 初始化失败");
        RECORDER_DEBUG("Failed to create AlsaRecorder");
    } else {
        // 设置完成回调
        alsa_recorder_set_finish_callback(page->recorder, recorder_finish_cb, page);
    }

    // 创建定时器（每秒触发）
    page->timer = lv_timer_create(timer_cb, 1000, page);

    // 初始化状态
    page->dialog_showing = false;
    page->file_saved = false;
    page->current_filename[0] = '\0';

    // 更新剩余空间
    update_space_info(page);
    update_ui_state(page);

    return scr;
}

// ---------- 页面销毁回调 ----------
static void recorder_on_destroy(void *p)
{
    RecorderPage *page = (RecorderPage *)p;
    if (!page) return;

    RECORDER_DEBUG("on_destroy");

    // 如果正在录音，放弃录音（不保存）
    if (alsa_recorder_is_recording(page->recorder)) {
        alsa_recorder_abort(page->recorder);
    }

    // 如果存在已保存的文件，根据规则：返回时不删除已保存的文件，所以这里什么也不做
    // 但如果有未保存的文件（例如刚停止但文件已保存？file_saved 为 true 时表示已保存，保留）
    // 实际上，file_saved 在正常停止时设为 true，在 abort 或清空时设为 false，所以这里不用处理文件删除

    // 销毁录音后端
    if (page->recorder) {
        alsa_recorder_destroy(page->recorder);
        page->recorder = NULL;
    }

    // 删除定时器
    if (page->timer) {
        lv_timer_del(page->timer);
        page->timer = NULL;
    }

    // UI 对象由 LVGL 自动删除（因为 obj 是页面的根），无需手动删除

    RECORDER_DEBUG("on_destroy finished");
}

// ---------- 事件回调 ----------
static void btn_record_cb(lv_event_t *e)
{
    RecorderPage *page = (RecorderPage *)lv_event_get_user_data(e);
    if (!page) return;

    // 如果对话框正在显示，先隐藏（但通常按钮不可点击，以防万一）
    if (page->dialog_showing) {
        hide_delete_dialog(page);
    }

    if (!alsa_recorder_is_recording(page->recorder)) {
        // 开始录音
        if (!ensure_rec_dir_exists()) {
            lv_label_set_text(page->label_status, "状态: 目录创建失败");
            RECORDER_DEBUG("Failed to create rec dir");
            return;
        }

        generate_filename(page->current_filename, sizeof(page->current_filename));
        int ret = alsa_recorder_start(page->recorder, page->current_filename);
        if (ret == 0) {
            lv_label_set_text(page->label_status, "状态: 录音中...");
            page->file_saved = false;  // 新录音尚未保存
            RECORDER_DEBUG("Recording started: %s", page->current_filename);
        } else {
            lv_label_set_text(page->label_status, "状态: 启动失败");
            page->current_filename[0] = '\0';
            RECORDER_DEBUG("Failed to start recording");
        }
    } else {
        // 停止录音
        alsa_recorder_stop(page->recorder);
        // 注意：停止后文件会在后台完成，状态更新在回调中进行
        lv_label_set_text(page->label_status, "状态: 停止中...");
    }

    update_ui_state(page);
}

static void btn_back_cb(lv_event_t *e)
{
    RecorderPage *page = (RecorderPage *)lv_event_get_user_data(e);
    if (!page) return;

    if (page->dialog_showing) {
        hide_delete_dialog(page);
        return;
    }

    // 如果正在录音，放弃录音（不保存文件）
    if (alsa_recorder_is_recording(page->recorder)) {
        alsa_recorder_abort(page->recorder);
        page->file_saved = false;
        page->current_filename[0] = '\0';
        RECORDER_DEBUG("Recording aborted on back");
    }

    // 返回上一页
    page_back();
}

static void btn_delete_cb(lv_event_t *e)
{
    RecorderPage *page = (RecorderPage *)lv_event_get_user_data(e);
    if (!page) return;
    show_delete_dialog(page);
}

static void dialog_confirm_cb(lv_event_t *e)
{
    RecorderPage *page = (RecorderPage *)lv_event_get_user_data(e);
    if (!page) return;
    hide_delete_dialog(page);
    delete_all_recordings(page);
}

static void dialog_cancel_cb(lv_event_t *e)
{
    RecorderPage *page = (RecorderPage *)lv_event_get_user_data(e);
    if (!page) return;
    hide_delete_dialog(page);
}

// ---------- 定时器回调 ----------
static void timer_cb(lv_timer_t *timer)
{
    RecorderPage *page = (RecorderPage *)timer->user_data;
    if (!page) return;

    // 更新时长显示
    if (alsa_recorder_is_recording(page->recorder)) {
        unsigned int sec = alsa_recorder_get_duration(page->recorder);
        char text[20];
        snprintf(text, sizeof(text), "时长: %02u:%02u", sec / 60, sec % 60);
        lv_label_set_text(page->label_duration, text);
    }

    // 更新剩余空间（也可以定时刷新）
    update_space_info(page);

    // 其他状态更新已经在事件中处理
}

// ---------- 录音后端完成回调 ----------
static void recorder_finish_cb(void *user_data)
{
    RecorderPage *page = (RecorderPage *)user_data;
    if (!page) return;

    RECORDER_DEBUG("Recorder finish callback");

    // 检查是否是正常停止（非 abort）
    // 由于 abort 也会触发线程结束，但 abort 后不会更新 file_saved
    // 这里我们根据是否有文件名和是否在 abort 后决定
    // 简单起见，只要不是 abort 且文件存在，就认为已保存
    if (page->current_filename[0] != '\0' && access(page->current_filename, F_OK) == 0) {
        page->file_saved = true;
        lv_label_set_text(page->label_status, "状态: 已保存");
        RECORDER_DEBUG("File saved: %s", page->current_filename);
    } else {
        lv_label_set_text(page->label_status, "状态: 已放弃");
        page->current_filename[0] = '\0';
        RECORDER_DEBUG("Recording aborted, no file saved");
    }

    update_ui_state(page);
    update_space_info(page);
}

// ---------- 工具函数 ----------
static int ensure_rec_dir_exists(void)
{
    struct stat st;
    if (stat("/mnt/UDISK/rec", &st) == 0) {
        if (S_ISDIR(st.st_mode)) return 1;
    }
    int ret = mkdir("/mnt/UDISK/rec", 0755);
    if (ret == 0 || errno == EEXIST) return 1;
    RECORDER_DEBUG("mkdir failed: %s", strerror(errno));
    return 0;
}

static void update_space_info(RecorderPage *page)
{
    struct statfs fs;
    if (statfs("/mnt/UDISK", &fs) == 0) {
        long long free_bytes = (long long)fs.f_bfree * fs.f_bsize;
        double free_mb = free_bytes / (1024.0 * 1024.0);
        char info[64];
        snprintf(info, sizeof(info), "剩余空间: %.1f MB", free_mb);
        lv_label_set_text(page->label_space, info);
    } else {
        lv_label_set_text(page->label_space, "剩余空间: -- MB");
        RECORDER_DEBUG("statfs failed: %s", strerror(errno));
    }
}

static void generate_filename(char *buffer, size_t size)
{
    time_t now = time(NULL);
    struct tm *tm = localtime(&now);
    snprintf(buffer, size, "/mnt/UDISK/rec/%02d%02d%02d_%02d%02d%02d.wav",
             tm->tm_year % 100, tm->tm_mon + 1, tm->tm_mday,
             tm->tm_hour, tm->tm_min, tm->tm_sec);
    RECORDER_DEBUG("Generated filename: %s", buffer);
}

static void update_ui_state(RecorderPage *page)
{
    bool recording = alsa_recorder_is_recording(page->recorder);
    if (recording) {
        lv_label_set_text(page->btn_record_label, "停止录音");
        lv_obj_set_style_bg_color(page->btn_record, lv_color_hex(0xFF5555), LV_PART_MAIN);
    } else {
        lv_label_set_text(page->btn_record_label, "开始录音");
        lv_obj_set_style_bg_color(page->btn_record, lv_color_hex(0x4CAF50), LV_PART_MAIN);
    }

    if (page->current_filename[0] != '\0' && !recording) {
        // 显示文件名（仅当有文件且不在录音时）
        char *fname = strrchr(page->current_filename, '/');
        char display[150];
        snprintf(display, sizeof(display), "文件: %s", fname ? fname + 1 : page->current_filename);
        lv_label_set_text(page->label_filename, display);
    } else if (recording) {
        lv_label_set_text(page->label_filename, "文件: 录音中...");
    } else {
        lv_label_set_text(page->label_filename, "文件: (未开始)");
    }
}

// ---------- 对话框 ----------
static void show_delete_dialog(RecorderPage *page)
{
    if (page->dialog_showing) return;
    page->dialog_showing = true;

    // 如果正在录音，先停止（按照之前逻辑，清空时应该停止录音并丢弃）
    if (alsa_recorder_is_recording(page->recorder)) {
        alsa_recorder_abort(page->recorder);
        page->file_saved = false;
        page->current_filename[0] = '\0';
        update_ui_state(page);
    }

    lv_obj_t *scr = page->base.obj; // 根对象

    // 创建遮罩
    page->dialog_bg = lv_obj_create(scr);
    lv_obj_set_size(page->dialog_bg, lv_pct(100), lv_pct(100));
    lv_obj_set_style_bg_color(page->dialog_bg, lv_color_black(), 0);
    lv_obj_set_style_bg_opa(page->dialog_bg, LV_OPA_50, 0);
    lv_obj_clear_flag(page->dialog_bg, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_move_foreground(page->dialog_bg);

    // 对话框容器
    page->dialog_box = lv_obj_create(page->dialog_bg);
    lv_obj_set_size(page->dialog_box, 220, 140);
    lv_obj_align(page->dialog_box, LV_ALIGN_CENTER, 0, 0);
    lv_obj_set_style_bg_color(page->dialog_box, lv_color_white(), 0);
    lv_obj_set_style_radius(page->dialog_box, 10, 0);
    lv_obj_clear_flag(page->dialog_box, LV_OBJ_FLAG_SCROLLABLE);

    // 提示文字
    page->dialog_label = lv_label_create(page->dialog_box);
    lv_label_set_text(page->dialog_label, "确认删除所有录音文件？\n此操作不可恢复！");
    lv_obj_set_width(page->dialog_label, lv_pct(100));
    lv_obj_align(page->dialog_label, LV_ALIGN_TOP_MID, 0, 10);
    lv_obj_set_style_text_align(page->dialog_label, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_style_text_color(page->dialog_label, lv_color_hex(0xFF4444), 0);

    // 确认按钮
    page->dialog_btn_confirm = lv_btn_create(page->dialog_box);
    lv_obj_set_size(page->dialog_btn_confirm, 80, 35);
    lv_obj_align(page->dialog_btn_confirm, LV_ALIGN_BOTTOM_LEFT, 10, -10);
    lv_obj_t *lbl_cfm = lv_label_create(page->dialog_btn_confirm);
    lv_label_set_text(lbl_cfm, "确认");
    lv_obj_center(lbl_cfm);
    lv_obj_set_style_bg_color(page->dialog_btn_confirm, lv_color_hex(0xFF4444), LV_PART_MAIN);
    lv_obj_set_style_text_color(lbl_cfm, lv_color_white(), 0);
    lv_obj_add_event_cb(page->dialog_btn_confirm, dialog_confirm_cb, LV_EVENT_CLICKED, page);

    // 取消按钮
    page->dialog_btn_cancel = lv_btn_create(page->dialog_box);
    lv_obj_set_size(page->dialog_btn_cancel, 80, 35);
    lv_obj_align(page->dialog_btn_cancel, LV_ALIGN_BOTTOM_RIGHT, -10, -10);
    lv_obj_t *lbl_ccl = lv_label_create(page->dialog_btn_cancel);
    lv_label_set_text(lbl_ccl, "取消");
    lv_obj_center(lbl_ccl);
    lv_obj_set_style_bg_color(page->dialog_btn_cancel, lv_color_hex(0xCCCCCC), LV_PART_MAIN);
    lv_obj_add_event_cb(page->dialog_btn_cancel, dialog_cancel_cb, LV_EVENT_CLICKED, page);
}

static void hide_delete_dialog(RecorderPage *page)
{
    if (!page->dialog_showing) return;
    page->dialog_showing = false;

    if (page->dialog_bg) {
        lv_obj_del(page->dialog_bg);
        page->dialog_bg = NULL;
        page->dialog_box = NULL;
        page->dialog_label = NULL;
        page->dialog_btn_confirm = NULL;
        page->dialog_btn_cancel = NULL;
    }
}

static void delete_all_recordings(RecorderPage *page)
{
    RECORDER_DEBUG("Deleting all recordings in /mnt/UDISK/rec/");
    int ret = system("rm -rf /mnt/UDISK/rec/* 2>/dev/null");
    if (ret == 0) {
        lv_label_set_text(page->label_status, "状态: 录音文件已清空");
        page->current_filename[0] = '\0';
        page->file_saved = false;
        update_ui_state(page);
        RECORDER_DEBUG("All files deleted");
    } else {
        lv_label_set_text(page->label_status, "状态: 删除失败");
        RECORDER_DEBUG("rm failed, ret=%d", ret);
    }
    update_space_info(page);
}