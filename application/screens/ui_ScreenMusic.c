#include "lvgl/lvgl.h"
#include "ui.h"
#include "../music.h"
#include <stdio.h>

static lv_obj_t * lab_song;      /* 当前歌名 */
static lv_obj_t * lab_state;     /* 播放状态 */
static lv_obj_t * lab_btn_play; /* 播放按钮里的文字 */
static lv_obj_t * lab_vol;       /* 音量数值 */

static void play_cb(lv_event_t * e)
{
    LV_UNUSED(e);
    if (music_get_state() == MUSIC_PLAYING)
        music_stop();
    else
        music_play(-1);          /* 续播当前曲, 首次从第一首开始 */
}

static void next_cb(lv_event_t * e) { LV_UNUSED(e); music_next(); }
static void prev_cb(lv_event_t * e) { LV_UNUSED(e); music_prev(); }

static void vol_cb(lv_event_t * e)
{
    lv_obj_t * sl = lv_event_get_target(e);
    music_set_volume((int)lv_slider_get_value(sl));
}

/* 500ms 轮询: 自动切歌时 UI 也能跟上 */
static void music_ui_timer(lv_timer_t * t)
{
    (void)t;
    char name[64];

    music_get_name(music_current(), name, sizeof(name));
    if (name[0] == '\0')
        snprintf(name, sizeof(name), "%s", music_count() > 0 ? "未播放" : "曲库为空");
    lv_label_set_text(lab_song, name);

    if (music_get_state() == MUSIC_PLAYING) {
        lv_label_set_text(lab_state, "播放中");
        lv_label_set_text(lab_btn_play, "停止");
    } else {
        lv_label_set_text(lab_state, "已停止");
        lv_label_set_text(lab_btn_play, "播放");
    }

    lv_label_set_text_fmt(lab_vol, "音量 %d%%", music_get_volume());
}

static lv_obj_t * create_ctl_btn(lv_obj_t * parent, const char * txt, lv_event_cb_t cb)
{
    lv_obj_t * b = lv_btn_create(parent);
    lv_obj_set_size(b, 190, 70);
    lv_obj_set_style_bg_color(b, lv_color_hex(0x2A333B), LV_STATE_DEFAULT);
    lv_obj_set_style_bg_color(b, lv_color_hex(0x2F6FED), LV_STATE_PRESSED);
    lv_obj_set_style_radius(b, 12, LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_width(b, 0, LV_STATE_DEFAULT);
    lv_obj_add_event_cb(b, cb, LV_EVENT_CLICKED, NULL);

    lv_obj_t * t = lv_label_create(b);
    lv_label_set_text(t, txt);
    lv_obj_set_style_text_font(t, &lv_font_zh_32, LV_STATE_DEFAULT);
    lv_obj_center(t);
    return b;
}

void ui_ScreenMusic_screen_init(void)
{
    lv_obj_set_flex_flow(ui_ScreenMusic, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(ui_ScreenMusic, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_all(ui_ScreenMusic, 30, LV_STATE_DEFAULT);
    lv_obj_set_style_pad_top(ui_ScreenMusic, 40, LV_STATE_DEFAULT);

    /* 标题 */
    lv_obj_t * title = lv_label_create(ui_ScreenMusic);
    lv_label_set_text(title, "音乐播放");
    lv_obj_set_style_text_font(title, &lv_font_zh_32, LV_STATE_DEFAULT);
    lv_obj_set_style_text_color(title, lv_color_hex(0xFFFFFF), LV_STATE_DEFAULT);

    /* 歌曲卡片 */
    lv_obj_t * card = lv_obj_create(ui_ScreenMusic);
    lv_obj_set_size(card, LV_PCT(100), 220);
    lv_obj_set_style_bg_color(card, lv_color_hex(0x2A333B), LV_STATE_DEFAULT);
    lv_obj_set_style_bg_opa(card, LV_OPA_COVER, LV_STATE_DEFAULT);
    lv_obj_set_style_radius(card, 18, LV_STATE_DEFAULT);
    lv_obj_set_style_border_width(card, 0, LV_STATE_DEFAULT);
    lv_obj_set_style_pad_all(card, 16, LV_STATE_DEFAULT);
    lv_obj_set_style_pad_top(card, 45, LV_STATE_DEFAULT);
    lv_obj_set_flex_flow(card, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(card, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);

    lab_song = lv_label_create(card);
    lv_label_set_text(lab_song, "未播放");
    lv_obj_set_style_text_font(lab_song, &lv_font_zh_32, LV_STATE_DEFAULT);
    lv_obj_set_style_text_color(lab_song, lv_color_hex(0xFFFFFF), LV_STATE_DEFAULT);

    lab_state = lv_label_create(card);
    lv_label_set_text(lab_state, "已停止");
    lv_obj_set_style_text_font(lab_state, &lv_font_zh_32, LV_STATE_DEFAULT);
    lv_obj_set_style_text_color(lab_state, lv_color_hex(0x9AA7B0), LV_STATE_DEFAULT);
    lv_obj_set_style_pad_top(lab_state, 12, LV_STATE_DEFAULT);

    /* 控制按钮行: 上一首 / 播放 / 下一首 */
    lv_obj_t * row = lv_obj_create(ui_ScreenMusic);
    lv_obj_set_size(row, LV_PCT(100), LV_SIZE_CONTENT);
    lv_obj_set_style_bg_opa(row, LV_OPA_TRANSP, LV_STATE_DEFAULT);
    lv_obj_set_style_border_width(row, 0, LV_STATE_DEFAULT);
    lv_obj_set_style_pad_all(row, 0, LV_STATE_DEFAULT);
    lv_obj_set_style_pad_top(row, 30, LV_STATE_DEFAULT);
    lv_obj_set_flex_flow(row, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(row, LV_FLEX_ALIGN_SPACE_BETWEEN, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);

    create_ctl_btn(row, "上一首", prev_cb);
    lv_obj_t * btn_play = create_ctl_btn(row, "播放", play_cb);
    create_ctl_btn(row, "下一首", next_cb);
    lab_btn_play = lv_obj_get_child(btn_play, 0);

    /* 音量 */
    lab_vol = lv_label_create(ui_ScreenMusic);
    lv_label_set_text(lab_vol, "音量 80%");
    lv_obj_set_style_text_font(lab_vol, &lv_font_zh_32, LV_STATE_DEFAULT);
    lv_obj_set_style_text_color(lab_vol, lv_color_hex(0xCCCCCC), LV_STATE_DEFAULT);
    lv_obj_set_style_pad_top(lab_vol, 24, LV_STATE_DEFAULT);

    lv_obj_t * slider = lv_slider_create(ui_ScreenMusic);
    lv_obj_set_width(slider, LV_PCT(100));
    lv_slider_set_range(slider, 0, 100);
    lv_slider_set_value(slider, music_get_volume(), LV_ANIM_OFF);
    lv_obj_set_style_bg_color(slider, lv_color_hex(0x2A333B), LV_PART_MAIN);
    lv_obj_set_style_bg_color(slider, lv_color_hex(0x2F6FED), LV_PART_INDICATOR);
    lv_obj_set_style_bg_color(slider, lv_color_hex(0xFFFFFF), LV_PART_KNOB);
    lv_obj_add_event_cb(slider, vol_cb, LV_EVENT_VALUE_CHANGED, NULL);
    lv_obj_set_style_pad_top(slider, 12, LV_STATE_DEFAULT);

    lv_timer_create(music_ui_timer, 500, NULL);
}

