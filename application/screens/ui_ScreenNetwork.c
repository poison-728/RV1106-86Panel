#include "lvgl/lvgl.h"
#include "ui.h"
#include "wifi.h"
#include <pthread.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>

static lv_obj_t * ta_ssid;
static lv_obj_t * ta_psw;
static lv_obj_t * label_status;
static lv_obj_t * kb;

/* ---------- WiFi 扫描 (B方案) ---------- */
/* 列表与键盘互斥: 都在页面 flex 流末尾, 显示一个必须收起另一个 */
static lv_obj_t * list_panel;
static char scan_list[WIFI_SCAN_MAX][64];   /* 后台线程写, UI 线程读 */
static int   scan_cnt = 0;
static volatile int scan_state = 0;        /* 0 空闲 / 1 扫描中 / 2 结果就绪 */

static void network_wifi_cb(lv_event_t *e)
{
    lv_obj_t *sw = lv_event_get_target(e);
    if (lv_obj_has_state(sw, LV_STATE_CHECKED)) {
        lv_label_set_text(label_status, "WiFi 已开启");
        system("ifconfig wlan0 up");
    } else {
        lv_label_set_text(label_status, "WiFi 已关闭");
        wifi_disconnect();
    }
}

static void *network_scan_thread(void *arg)
{
    (void)arg;
    scan_cnt = wifi_scan(scan_list, WIFI_SCAN_MAX);   /* 阻塞 2~5 秒, 只在后台线程调 */
    scan_state = 2;                                   /* 数据写完最后置标志 */
    return NULL;
}

static void network_scan_cb(lv_event_t * e)
{
    LV_UNUSED(e);
    if (scan_state == 1) return;          /* 正在扫, 防重入 */

    /* 收起键盘和旧列表: 位置让给新列表 */
    lv_obj_add_flag(kb, LV_OBJ_FLAG_HIDDEN);
    if (list_panel) {
        lv_obj_del(list_panel);
        list_panel = NULL;
    }

    scan_state = 0;                       /* 丢弃旧结果 */
    lv_label_set_text(label_status, "正在扫描附近 WiFi...");

    pthread_t tid;
    pthread_create(&tid, NULL, network_scan_thread, NULL);
    pthread_detach(tid);
}

/* 点列表项: SSID 回填输入框, 列表收起 */
static void ssid_item_cb(lv_event_t *e)
{
    lv_obj_t * item = lv_event_get_target(e);
    lv_obj_t * lab  = lv_obj_get_child(item, 0);

    /* 先把文字取出来, 再处理删除 */
    char ssid[64];
    snprintf(ssid, sizeof(ssid), "%s", lv_label_get_text(lab));
    lv_textarea_set_text(ta_ssid, ssid);

    /* 本函数运行时, 事件目标(按钮)还在 list_panel 内部。
       同步 lv_obj_del 祖先 = use-after-free, 会写坏堆;
       必须用 del_async 推迟到下一帧主循环再删 */
    if (list_panel) {
        lv_obj_del_async(list_panel);
        list_panel = NULL;
    }
}

/* UI 线程轮询: 扫描完成后建列表 */
static void network_scan_timer(lv_timer_t *t)
{
    (void)t;
    if (scan_state != 2) return;
    scan_state = 0;

    if (list_panel) {
        lv_obj_del(list_panel);
        list_panel = NULL;
    }

    if (scan_cnt <= 0) {
        lv_label_set_text(label_status, "没有找到热点");
        return;
    }

    char buf[64];
    snprintf(buf, sizeof(buf), "扫描完成: %d 个", scan_cnt);
    lv_label_set_text(label_status, buf);

    /* 列表放在按钮行下方(与键盘同一位置), 深色卡片风格与整页一致 */
    list_panel = lv_obj_create(ui_ScreenNetwork);
    lv_obj_set_size(list_panel, LV_PCT(100), 230);
    lv_obj_set_style_bg_color(list_panel, lv_color_hex(0x1A2026), LV_STATE_DEFAULT);
    lv_obj_set_style_bg_opa(list_panel, LV_OPA_COVER, LV_STATE_DEFAULT);
    lv_obj_set_style_border_width(list_panel, 1, LV_STATE_DEFAULT);
    lv_obj_set_style_border_color(list_panel, lv_color_hex(0x2A333B), LV_STATE_DEFAULT);
    lv_obj_set_style_radius(list_panel, 14, LV_STATE_DEFAULT);
    lv_obj_set_style_pad_all(list_panel, 10, LV_STATE_DEFAULT);
    lv_obj_set_flex_flow(list_panel, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(list_panel, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_row(list_panel, 8, LV_STATE_DEFAULT);

    for (int i = 0; i < scan_cnt; i++) {
        lv_obj_t * item = lv_btn_create(list_panel);
        lv_obj_set_size(item, LV_PCT(100), 56);
        lv_obj_set_style_bg_color(item, lv_color_hex(0x2A333B), LV_STATE_DEFAULT);
        lv_obj_set_style_radius(item, 10, LV_STATE_DEFAULT);
        lv_obj_set_style_shadow_width(item, 0, LV_STATE_DEFAULT);
        lv_obj_add_event_cb(item, ssid_item_cb, LV_EVENT_CLICKED, NULL);

        lv_obj_t * lab = lv_label_create(item);
        lv_label_set_text(lab, scan_list[i]);
        lv_obj_set_style_text_font(lab, &lv_font_zh_32, LV_STATE_DEFAULT);
        lv_obj_set_style_text_color(lab, lv_color_hex(0xE0E6EB), LV_STATE_DEFAULT);
        lv_obj_center(lab);
    }

    lv_obj_scroll_to_view(list_panel, LV_ANIM_ON);
}

typedef struct {
    char ssid[64];
    char psk[64];
} wifi_param_t;

static void *network_connect_thread(void *arg)
{
    wifi_param_t *p = (wifi_param_t *)arg;
    wifi_connect(p->ssid, p->psk);   /* 阻塞的，所以放后台线程，不卡 UI */
    free(p);
    return NULL;
}

static void network_status_timer(lv_timer_t *t)
{
    (void)t;
    char ip[32] = {0};
    if (wifi_get_ip(ip, sizeof(ip)) == 0) {
        char buf[64];
        snprintf(buf, sizeof(buf), "已连接: %s", ip);
        lv_label_set_text(label_status, buf);
    }
}

static void network_connect_cb(lv_event_t *e)
{
    (void)e;
    const char *ssid = lv_textarea_get_text(ta_ssid);
    const char *psk  = lv_textarea_get_text(ta_psw);

    if (ssid[0] == '\0' || psk[0] == '\0') {
        lv_label_set_text(label_status, "请输入名称和密码");
        return;
    }

    wifi_param_t *p = (wifi_param_t *)malloc(sizeof(wifi_param_t));
    strncpy(p->ssid, ssid, sizeof(p->ssid) - 1);
    strncpy(p->psk, psk, sizeof(p->psk) - 1);

    lv_label_set_text(label_status, "正在连接...");

    pthread_t tid;
    pthread_create(&tid, NULL, network_connect_thread, p);
    pthread_detach(tid);
}

/* 输入框聚焦/失焦时，弹出/收起键盘 */
static void ta_event_cb(lv_event_t * e)
{
    lv_event_code_t code = lv_event_get_code(e);
    lv_obj_t * ta = lv_event_get_target(e);

    if (code == LV_EVENT_FOCUSED) {
        /* 收起热点列表: 位置让给键盘 */
        if (list_panel) {
            lv_obj_del(list_panel);
            list_panel = NULL;
        }
        lv_keyboard_set_textarea(kb, ta);
        lv_obj_clear_flag(kb, LV_OBJ_FLAG_HIDDEN);
        lv_obj_scroll_to_view(kb, LV_ANIM_ON);
    }
    else if (code == LV_EVENT_DEFOCUSED) {
        lv_keyboard_set_textarea(kb, NULL);
        lv_obj_add_flag(kb, LV_OBJ_FLAG_HIDDEN);
    }
}

void ui_ScreenNetwork_screen_init(void)
{
    /* 整页容器：统一深色背景，去边框，去卡片 */
    lv_obj_set_style_bg_color(ui_ScreenNetwork, lv_color_hex(0x12161A), LV_STATE_DEFAULT);
    lv_obj_set_style_border_width(ui_ScreenNetwork, 0, LV_STATE_DEFAULT);
    lv_obj_set_style_pad_all(ui_ScreenNetwork, 30, LV_STATE_DEFAULT);
    lv_obj_set_style_pad_top(ui_ScreenNetwork, 50, LV_STATE_DEFAULT);
    lv_obj_set_flex_flow(ui_ScreenNetwork, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(ui_ScreenNetwork, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);

    /* 标题 */
    lv_obj_t * title = lv_label_create(ui_ScreenNetwork);
    lv_label_set_text(title, "网络设置");
    lv_obj_set_style_text_font(title, &lv_font_zh_32, LV_STATE_DEFAULT);
    lv_obj_set_style_text_color(title, lv_color_hex(0xFFFFFF), LV_STATE_DEFAULT);

    /* WiFi 开关行 */
    lv_obj_t * row = lv_obj_create(ui_ScreenNetwork);
    lv_obj_set_size(row, LV_PCT(100), LV_SIZE_CONTENT);
    lv_obj_set_style_bg_opa(row, LV_OPA_TRANSP, LV_STATE_DEFAULT);
    lv_obj_set_style_border_width(row, 0, LV_STATE_DEFAULT);
    lv_obj_set_style_pad_all(row, 0, LV_STATE_DEFAULT);
    lv_obj_set_flex_flow(row, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(row, LV_FLEX_ALIGN_SPACE_BETWEEN, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);

    lv_obj_t * lab = lv_label_create(row);
    lv_label_set_text(lab, "WiFi 开关");
    lv_obj_set_style_text_font(lab, &lv_font_zh_32, LV_STATE_DEFAULT);
    lv_obj_set_style_text_color(lab, lv_color_hex(0xFFFFFF), LV_STATE_DEFAULT);

    lv_obj_t * sw = lv_switch_create(row);
    lv_obj_add_state(sw, LV_STATE_CHECKED);
    lv_obj_add_event_cb(sw, network_wifi_cb, LV_EVENT_VALUE_CHANGED, NULL);

    /* 状态标签 */
    label_status = lv_label_create(ui_ScreenNetwork);
    lv_label_set_text(label_status, "WiFi 已开启");
    lv_obj_set_style_text_font(label_status, &lv_font_zh_32, LV_STATE_DEFAULT);
    lv_obj_set_style_text_color(label_status, lv_color_hex(0x9AA7B0), LV_STATE_DEFAULT);
    lv_obj_set_style_pad_top(label_status, 16, LV_STATE_DEFAULT);
    lv_obj_set_style_pad_bottom(label_status, 16, LV_STATE_DEFAULT);

    /* WiFi 名称 */
    lv_obj_t * l1 = lv_label_create(ui_ScreenNetwork);
    lv_label_set_text(l1, "WiFi 名称");
    lv_obj_set_style_text_font(l1, &lv_font_zh_32, LV_STATE_DEFAULT);
    lv_obj_set_style_text_color(l1, lv_color_hex(0xCCCCCC), LV_STATE_DEFAULT);
    lv_obj_set_style_pad_top(l1, 10, LV_STATE_DEFAULT);
    lv_obj_set_style_pad_bottom(l1, 6, LV_STATE_DEFAULT);

    ta_ssid = lv_textarea_create(ui_ScreenNetwork);
    lv_obj_set_width(ta_ssid, LV_PCT(100));
    lv_textarea_set_one_line(ta_ssid, true);
    lv_textarea_set_placeholder_text(ta_ssid, "请输入 WiFi 名称");
    lv_obj_set_style_bg_color(ta_ssid, lv_color_hex(0x12161A), LV_STATE_DEFAULT);
    lv_obj_set_style_text_color(ta_ssid, lv_color_hex(0xCCCCCC), LV_STATE_DEFAULT);
    lv_obj_set_style_border_width(ta_ssid, 1, LV_STATE_DEFAULT);
    lv_obj_set_style_border_color(ta_ssid, lv_color_hex(0x2A333B), LV_STATE_DEFAULT);
    lv_obj_set_style_text_font(ta_ssid, &lv_font_zh_32, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_font(ta_ssid, &lv_font_zh_32, LV_PART_TEXTAREA_PLACEHOLDER | LV_STATE_DEFAULT);
    lv_obj_add_event_cb(ta_ssid, ta_event_cb, LV_EVENT_ALL, NULL);
    /* 未聚焦：光标全透明，看不见 */
    lv_obj_set_style_bg_opa(ta_ssid, LV_OPA_TRANSP, LV_PART_CURSOR | LV_STATE_DEFAULT);

    lv_obj_set_style_bg_color(ta_ssid,  lv_color_hex(0x4ECDC4), LV_PART_CURSOR | LV_STATE_FOCUSED);
    lv_obj_set_style_bg_opa(ta_ssid,   LV_OPA_COVER,            LV_PART_CURSOR | LV_STATE_FOCUSED);

    /* 密码 */
    lv_obj_t * l2 = lv_label_create(ui_ScreenNetwork);
    lv_label_set_text(l2, "密码");
    lv_obj_set_style_text_font(l2, &lv_font_zh_32, LV_STATE_DEFAULT);
    lv_obj_set_style_text_color(l2, lv_color_hex(0xCCCCCC), LV_STATE_DEFAULT);
    lv_obj_set_style_pad_top(l2, 10, LV_STATE_DEFAULT);
    lv_obj_set_style_pad_bottom(l2, 6, LV_STATE_DEFAULT);

    ta_psw = lv_textarea_create(ui_ScreenNetwork);
    lv_obj_set_width(ta_psw, LV_PCT(100));
    lv_textarea_set_one_line(ta_psw, true);
    lv_textarea_set_password_mode(ta_psw, true);
    lv_textarea_set_password_bullet(ta_psw, "*");
    lv_textarea_set_placeholder_text(ta_psw, "请输入密码");
    lv_obj_set_style_bg_color(ta_psw, lv_color_hex(0x12161A), LV_STATE_DEFAULT);
    lv_obj_set_style_text_color(ta_psw, lv_color_hex(0xCCCCCC), LV_STATE_DEFAULT);
    lv_obj_set_style_border_width(ta_psw, 1, LV_STATE_DEFAULT);
    lv_obj_set_style_border_color(ta_psw, lv_color_hex(0x2A333B), LV_STATE_DEFAULT);
    lv_obj_set_style_text_font(ta_psw, &lv_font_zh_32, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_font(ta_psw, &lv_font_zh_32, LV_PART_TEXTAREA_PLACEHOLDER | LV_STATE_DEFAULT);
    lv_obj_add_event_cb(ta_psw, ta_event_cb, LV_EVENT_ALL, NULL);
    /* 未聚焦：光标全透明，看不见 */
    lv_obj_set_style_bg_opa(ta_psw,  LV_OPA_TRANSP, LV_PART_CURSOR | LV_STATE_DEFAULT);

    /* 聚焦后：青绿色光标，不透明 */
    lv_obj_set_style_bg_color(ta_psw, lv_color_hex(0x4ECDC4), LV_PART_CURSOR | LV_STATE_FOCUSED);
    lv_obj_set_style_bg_opa(ta_psw,  LV_OPA_COVER,            LV_PART_CURSOR | LV_STATE_FOCUSED);

    /* 按钮行 */
    lv_obj_t * btns = lv_obj_create(ui_ScreenNetwork);
    lv_obj_set_size(btns, LV_PCT(100), LV_SIZE_CONTENT);
    lv_obj_set_style_bg_opa(btns, LV_OPA_TRANSP, LV_STATE_DEFAULT);
    lv_obj_set_style_border_width(btns, 0, LV_STATE_DEFAULT);
    lv_obj_set_style_pad_all(btns, 0, LV_STATE_DEFAULT);
    lv_obj_set_style_pad_top(btns, 24, LV_STATE_DEFAULT);
    lv_obj_set_flex_flow(btns, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(btns, LV_FLEX_ALIGN_SPACE_BETWEEN, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);

    lv_obj_t * b1 = lv_btn_create(btns);
    lv_obj_set_size(b1, 280, 64);
    lv_obj_set_style_bg_color(b1, lv_color_hex(0x2A333B), LV_STATE_DEFAULT);
    lv_obj_add_event_cb(b1, network_scan_cb, LV_EVENT_CLICKED, NULL);
    lv_obj_t * t1 = lv_label_create(b1);
    lv_label_set_text(t1, "扫描");
    lv_obj_set_style_text_font(t1, &lv_font_zh_32, LV_STATE_DEFAULT);
    lv_obj_center(t1);

    lv_obj_t * b2 = lv_btn_create(btns);
    lv_obj_set_size(b2, 280, 64);
    lv_obj_set_style_bg_color(b2, lv_color_hex(0x2F6FED), LV_STATE_DEFAULT);
    lv_obj_add_event_cb(b2, network_connect_cb, LV_EVENT_CLICKED, NULL);
    lv_obj_t * t2 = lv_label_create(b2);
    lv_label_set_text(t2, "连接");
    lv_obj_set_style_text_font(t2, &lv_font_zh_32, LV_STATE_DEFAULT);
    lv_obj_center(t2);

    /* 键盘：默认隐藏，点输入框时弹出 */
    kb = lv_keyboard_create(ui_ScreenNetwork);
    lv_obj_add_flag(kb, LV_OBJ_FLAG_HIDDEN);

    lv_timer_create(network_status_timer, 2000, NULL);
    lv_timer_create(network_scan_timer, 200, NULL);
}

