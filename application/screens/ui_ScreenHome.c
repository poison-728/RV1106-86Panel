#include "lvgl/lvgl.h"
#include "ui.h"
#include "wifi.h"
#include "../music.h"
#include "modbus.h"
#include "../cfg.h"
#include <time.h>
#include <stdio.h>

static bool ac_on = false;

static lv_obj_t * label_time;
static lv_obj_t * label_date;
static lv_obj_t * label_net_status;
static lv_obj_t * net_card;
static lv_obj_t * ac_card;    /* 空调卡片 */
static lv_obj_t * label_music_name;
static lv_obj_t * label_play_btn;
static lv_obj_t * label_ac_status;
static lv_obj_t * lab_th;   /* 温湿度文字 */

static int net_stable = -1;    /* 当前已显示的稳定状态: -1 未知 0 断 1 连 */
static int net_last   = -1;   /* 上一次采样值 */
static int net_run    = 0;    /* 连续相同次数 */
static char net_ip[32] = {0}; /* 稳定时的 IP */

static void clock_cb(lv_timer_t * t)
{
    LV_UNUSED(t);
    time_t now = time(NULL);
    struct tm *tm = localtime(&now);
    if (tm) {
        lv_label_set_text_fmt(label_time, "%02d:%02d", tm->tm_hour, tm->tm_min);
        lv_label_set_text_fmt(label_date, "%d年%d月%d日",
                              tm->tm_year + 1900, tm->tm_mon + 1, tm->tm_mday);
    }

    /* 音乐卡片同步: 停止时重头播, 播放中显示当前歌名 */
    if (music_get_state() == MUSIC_PLAYING) {
        char name[64];
        music_get_name(music_current(), name, sizeof(name));
        lv_label_set_text(label_music_name, name[0] ? name : "正在播放");
        lv_label_set_text(label_play_btn, "停止");
    } else {
        lv_label_set_text(label_music_name, "未播放");
        lv_label_set_text(label_play_btn, "播放");
    }

    if (modbus_online()) {
    uint16_t v;
    if (modbus_get(MB_REG_AC, &v) == 0 && (v ? 1 : 0) != ac_on) {
        ac_on = v;
        lv_obj_set_style_bg_color(ac_card, lv_color_hex(ac_on ? 0x2F6FED : 0x2A333B), LV_STATE_DEFAULT);
        lv_label_set_text(label_ac_status, ac_on ? "开启" : "关闭");
    }
    }
}

static void ac_card_cb(lv_event_t * e)
{
    lv_obj_t * card = lv_event_get_target(e);
    ac_on = !ac_on;
    modbus_write(MB_REG_AC, ac_on ? 1 : 0);   /* 唯一新行: 登记期望值, 后台线程发送 */
    cfg_set_int("ac", ac_on ? 1 : 0);
    lv_obj_set_style_bg_color(card, lv_color_hex(ac_on ? 0x2F6FED : 0x2A333B), LV_STATE_DEFAULT);
    lv_label_set_text(label_ac_status, ac_on ? "开启" : "关闭");
}

static void music_play_cb(lv_event_t * e)
{
    LV_UNUSED(e);
    if (music_get_state() == MUSIC_PLAYING)
        music_stop();
    else
        music_play(-1);          /* 续播当前曲, 首次从第一首开始 */
}

static void music_next_cb(lv_event_t * e) { LV_UNUSED(e); music_next(); }
static void music_prev_cb(lv_event_t * e) { LV_UNUSED(e); music_prev(); }

static void home_network_timer(lv_timer_t * t)
{
    (void)t;
    char ip[32] = {0};
    int cur = (wifi_get_ip(ip, sizeof(ip)) == 0) ? 1 : 0;

    if (cur == net_last) {
        net_run++;
    } else {
        net_last = cur;
        net_run = 1;
    }

    /* 连续 3 次一致, 且与当前显示不同, 才更新 UI */
    if (net_run >= 3 && cur != net_stable) {
        net_stable = cur;
        if (cur) {
            snprintf(net_ip, sizeof(net_ip), "%s", ip);
            char buf[48];
            snprintf(buf, sizeof(buf), "已连接: %s", net_ip);
            lv_label_set_text(label_net_status, buf);
            lv_obj_set_style_text_color(label_net_status, lv_color_hex(0x4ECDC4), LV_STATE_DEFAULT);
            lv_obj_set_style_bg_color(net_card, lv_color_hex(0x2B4A52), LV_STATE_DEFAULT);
        } else {
            lv_label_set_text(label_net_status, "未连接");
            lv_obj_set_style_text_color(label_net_status, lv_color_hex(0xFF6F61), LV_STATE_DEFAULT);
            lv_obj_set_style_bg_color(net_card, lv_color_hex(0x2A333B), LV_STATE_DEFAULT);
        }
    }
}

void ui_ScreenHome_screen_init(void)
{
    lv_obj_set_flex_flow(ui_ScreenHome, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(ui_ScreenHome, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_all(ui_ScreenHome, 30, LV_STATE_DEFAULT);
    lv_obj_set_style_pad_top(ui_ScreenHome, 30, LV_STATE_DEFAULT);

    /* 顶部大卡片：时间 + 年月日 + 温湿度 */
    lv_obj_t * top = lv_obj_create(ui_ScreenHome);
    lv_obj_set_size(top, LV_PCT(100), LV_SIZE_CONTENT);
    lv_obj_set_style_bg_color(top, lv_color_hex(0x2A333B), LV_STATE_DEFAULT);
    lv_obj_set_style_radius(top, 18, LV_STATE_DEFAULT);
    lv_obj_set_style_border_width(top, 0, LV_STATE_DEFAULT);
    lv_obj_set_style_pad_all(top, 24, LV_STATE_DEFAULT);
    lv_obj_set_flex_flow(top, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(top, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);

    label_time = lv_label_create(top);
    lv_label_set_text(label_time, "--:--");
    lv_obj_set_style_text_font(label_time, &lv_font_montserrat_48, LV_STATE_DEFAULT);
    lv_obj_set_style_text_color(label_time, lv_color_hex(0xFFFFFF), LV_STATE_DEFAULT);

    label_date = lv_label_create(top);
    lv_label_set_text(label_date, "--年--月--日");
    lv_obj_set_style_text_font(label_date, &lv_font_zh_32, LV_STATE_DEFAULT);
    lv_obj_set_style_text_color(label_date, lv_color_hex(0xC8D2DA), LV_STATE_DEFAULT);
    lv_obj_set_style_pad_top(label_date, 6, LV_STATE_DEFAULT);

    lab_th = lv_label_create(top);
    lv_label_set_text(lab_th, "室内温度 25.5°C  室内湿度 60%");
    lv_obj_set_style_text_font(lab_th, &lv_font_zh_32, LV_STATE_DEFAULT);
    lv_obj_set_style_text_color(lab_th, lv_color_hex(0xFFFFFF), LV_STATE_DEFAULT);
    lv_obj_set_style_pad_top(lab_th, 12, LV_STATE_DEFAULT);

    /* 空调 + 网络 卡片行 */
    lv_obj_t * row = lv_obj_create(ui_ScreenHome);
    lv_obj_set_size(row, LV_PCT(100), LV_SIZE_CONTENT);
    lv_obj_set_style_bg_opa(row, LV_OPA_TRANSP, LV_STATE_DEFAULT);
    lv_obj_set_style_border_width(row, 0, LV_STATE_DEFAULT);
    lv_obj_set_style_pad_all(row, 0, LV_STATE_DEFAULT);
    lv_obj_set_style_pad_top(row, 20, LV_STATE_DEFAULT);
    lv_obj_set_flex_flow(row, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(row, LV_FLEX_ALIGN_SPACE_BETWEEN, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);

    /* 空调卡片 */
    ac_card = lv_obj_create(row);
    lv_obj_set_size(ac_card, 315, 180);
    lv_obj_set_style_bg_color(ac_card, lv_color_hex(0x2A333B), LV_STATE_DEFAULT);
    lv_obj_set_style_radius(ac_card, 15, LV_STATE_DEFAULT);
    lv_obj_set_style_border_width(ac_card, 0, LV_STATE_DEFAULT);
    lv_obj_add_flag(ac_card, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(ac_card, ac_card_cb, LV_EVENT_CLICKED, NULL);

    lv_obj_t * ac_title = lv_label_create(ac_card);
    lv_label_set_text(ac_title, "空调");
    lv_obj_set_style_text_font(ac_title, &lv_font_zh_32, LV_STATE_DEFAULT);
    lv_obj_set_style_text_color(ac_title, lv_color_hex(0xC8D2DA), LV_STATE_DEFAULT);
    lv_obj_align(ac_title, LV_ALIGN_TOP_MID, 0, 15);

    lv_obj_t * ac_temp = lv_label_create(ac_card);
    lv_label_set_text(ac_temp, "25 °C");
    lv_obj_set_style_text_font(ac_temp, &lv_font_zh_32, LV_STATE_DEFAULT);
    lv_obj_set_style_text_color(ac_temp, lv_color_hex(0xFFFFFF), LV_STATE_DEFAULT);
    lv_obj_align(ac_temp, LV_ALIGN_CENTER, 0, 5);

    label_ac_status = lv_label_create(ac_card);
    lv_label_set_text(label_ac_status, "关闭");
    lv_obj_set_style_text_font(label_ac_status, &lv_font_zh_32, LV_STATE_DEFAULT);
    lv_obj_set_style_text_color(label_ac_status, lv_color_hex(0xFFFFFF), LV_STATE_DEFAULT);
    
    /* 开机恢复空调状态 */
    ac_on = cfg_get_int("ac", 0);
    modbus_write(MB_REG_AC, ac_on ? 1 : 0);
    lv_obj_set_style_bg_color(ac_card, lv_color_hex(ac_on ? 0x2F6FED : 0x2A333B), LV_STATE_DEFAULT);
    lv_label_set_text(label_ac_status, ac_on ? "开启" : "关闭");lv_obj_align(label_ac_status, LV_ALIGN_BOTTOM_MID, 0, -5);

    /* 网络卡片 */
    net_card = lv_obj_create(row);
    lv_obj_set_size(net_card, 315, 180);
    lv_obj_set_style_bg_color(net_card, lv_color_hex(0x2A333B), LV_STATE_DEFAULT);
    lv_obj_set_style_radius(net_card, 15, LV_STATE_DEFAULT);
    lv_obj_set_style_border_width(net_card, 0, LV_STATE_DEFAULT);

    lv_obj_t * net_title = lv_label_create(net_card);
    lv_label_set_text(net_title, "网络");
    lv_obj_set_style_text_font(net_title, &lv_font_zh_32, LV_STATE_DEFAULT);
    lv_obj_set_style_text_color(net_title, lv_color_hex(0xC8D2DA), LV_STATE_DEFAULT);
    lv_obj_align(net_title, LV_ALIGN_TOP_MID, 0, 15);

    label_net_status = lv_label_create(net_card);
    lv_label_set_text(label_net_status, "未连接");
    lv_obj_set_style_text_font(label_net_status, &lv_font_zh_32, LV_STATE_DEFAULT);
    lv_obj_set_style_text_color(label_net_status, lv_color_hex(0xFFFFFF), LV_STATE_DEFAULT);
    lv_obj_align(label_net_status, LV_ALIGN_CENTER, 0, 5);

    /* 音乐长条卡片 */
    lv_obj_t * music_card = lv_obj_create(ui_ScreenHome);
    lv_obj_set_size(music_card, LV_PCT(100), 110);
    lv_obj_set_style_bg_color(music_card, lv_color_hex(0x2A333B), LV_STATE_DEFAULT);
    lv_obj_set_style_radius(music_card, 15, LV_STATE_DEFAULT);
    lv_obj_set_style_border_width(music_card, 0, LV_STATE_DEFAULT);
    lv_obj_set_style_pad_hor(music_card, 20, LV_STATE_DEFAULT);
    lv_obj_set_style_pad_top(music_card, 20, LV_STATE_DEFAULT);
    lv_obj_set_flex_flow(music_card, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(music_card, LV_FLEX_ALIGN_SPACE_BETWEEN, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);

    label_music_name = lv_label_create(music_card);
    lv_label_set_text(label_music_name, "未播放");
    lv_obj_set_style_text_font(label_music_name, &lv_font_zh_32, LV_STATE_DEFAULT);
    lv_obj_set_style_text_color(label_music_name, lv_color_hex(0xFFFFFF), LV_STATE_DEFAULT);

    lv_obj_t * btnbox = lv_obj_create(music_card);
    lv_obj_set_size(btnbox, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
    lv_obj_set_style_bg_opa(btnbox, LV_OPA_TRANSP, LV_STATE_DEFAULT);
    lv_obj_set_style_border_width(btnbox, 0, LV_STATE_DEFAULT);
    lv_obj_set_style_pad_all(btnbox, 0, LV_STATE_DEFAULT);
    lv_obj_set_style_pad_column(btnbox, 8, LV_STATE_DEFAULT);
    lv_obj_set_flex_flow(btnbox, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(btnbox, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);

    lv_obj_t * b_prev = lv_btn_create(btnbox);
    lv_obj_set_size(b_prev, 110, 60);
    lv_obj_set_style_bg_color(b_prev, lv_color_hex(0x1E2026), LV_STATE_DEFAULT);
    lv_obj_add_event_cb(b_prev, music_prev_cb, LV_EVENT_CLICKED, NULL);
    lv_obj_t * t_prev = lv_label_create(b_prev);
    lv_label_set_text(t_prev, "上一首");
    lv_obj_set_style_text_font(t_prev, &lv_font_zh_32, LV_STATE_DEFAULT);
    lv_obj_center(t_prev);

    lv_obj_t * b_play = lv_btn_create(btnbox);
    lv_obj_set_size(b_play, 90, 60);
    lv_obj_set_style_bg_color(b_play, lv_color_hex(0x2F6FED), LV_STATE_DEFAULT);
    lv_obj_add_event_cb(b_play, music_play_cb, LV_EVENT_CLICKED, NULL);
    label_play_btn = lv_label_create(b_play);
    lv_label_set_text(label_play_btn, "播放");
    lv_obj_set_style_text_font(label_play_btn, &lv_font_zh_32, LV_STATE_DEFAULT);
    lv_obj_center(label_play_btn);

    lv_obj_t * b_next = lv_btn_create(btnbox);
    lv_obj_set_size(b_next, 110, 60);
    lv_obj_set_style_bg_color(b_next, lv_color_hex(0x1E2026), LV_STATE_DEFAULT);
    lv_obj_add_event_cb(b_next, music_next_cb, LV_EVENT_CLICKED, NULL);
    lv_obj_t * t_next = lv_label_create(b_next);
    lv_label_set_text(t_next, "下一首");
    lv_obj_set_style_text_font(t_next, &lv_font_zh_32, LV_STATE_DEFAULT);
    lv_obj_center(t_next);

    lv_timer_create(clock_cb, 1000, NULL);

    lv_timer_create(home_network_timer, 2000, NULL);
}

void ui_ScreenHome_set_sensor(float temp, float hum)
{
    if (!lab_th) return;
    char buf[64];
    snprintf(buf, sizeof(buf), "室内温度 %.1f°C  室内湿度 %.0f%%", temp, hum);
    lv_label_set_text(lab_th, buf);
}

