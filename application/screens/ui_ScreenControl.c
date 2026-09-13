#include "lvgl/lvgl.h"
#include "ui.h"
#include "modbus.h"
#include "../cfg.h"
#include "mqtt.h"
#include <stdint.h>
#include <stdio.h>

/* 设备表: 卡片顺序与 modbus 寄存器一一对应 */
typedef struct {
    const char * name;
    const char * cfg_key;
    const char * topic_state;
    uint16_t      reg;
} dev_item_t;

static const dev_item_t dev_list[] = {
    { "客厅灯",   "light_liv", "home/light_liv/state", MB_REG_LIGHT_LIV },
    { "卧室灯",   "light_bed", "home/light_bed/state", MB_REG_LIGHT_BED },
    { "客厅窗帘", "curt_liv",  "home/curt_liv/state",  MB_REG_CURT_LIV  },
    { "卧室窗帘", "curt_bed",  "home/curt_bed/state",  MB_REG_CURT_BED  },
};
#define DEV_NUM ((int)(sizeof(dev_list) / sizeof(dev_list[0])))

static lv_obj_t * dev_card[DEV_NUM];
static lv_obj_t * dev_state[DEV_NUM];
static lv_obj_t * lab_offline;
static uint16_t   dev_val[DEV_NUM];    /* 期望值(乐观更新), 上线后被真实值覆盖 */

/* 边沿上报: 只在值变化的那一拍发出, retain=1 让新订阅者连上即得最新状态 */
static void dev_report(int i, int val)
{
    char buf[4];
    snprintf(buf, sizeof(buf), "%d", val);
    mqtt_publish(dev_list[i].topic_state, buf, 1);
}

static void card_apply(int i, int on)
{
    lv_obj_set_style_bg_color(dev_card[i],
        lv_color_hex(on ? 0x2F6FED : 0x2A333B), LV_STATE_DEFAULT);
    lv_obj_set_style_text_color(dev_state[i],
        lv_color_hex(on ? 0xFFFFFF : 0x9AA7B0), LV_STATE_DEFAULT);
    lv_label_set_text(dev_state[i], on ? "开" : "关");
}

static void card_cb(lv_event_t * e)
{
    int i = (int)(intptr_t)lv_event_get_user_data(e);
    dev_val[i] = !dev_val[i];
    card_apply(i, dev_val[i]);
    modbus_write(dev_list[i].reg, dev_val[i]);   /* 非阻塞, 后台线程发送 */
    cfg_set_int(dev_list[i].cfg_key, dev_val[i]); /* 状态持久化 */
    dev_report(i, dev_val[i]);                   /* 本地点击 → 边沿上报 */
}

/* 1s 轮询: 上线时用从机真实状态刷新卡片(掉线保持期望值并提示) */
static void ctrl_ui_timer(lv_timer_t * t)
{
    (void)t;
    static int prev_mqtt = 0;
    int online = modbus_online();
    int m_online = mqtt_is_connected();

    if (online) lv_obj_add_flag(lab_offline, LV_OBJ_FLAG_HIDDEN);
    else        lv_obj_clear_flag(lab_offline, LV_OBJ_FLAG_HIDDEN);

    /* MQTT 离线→在线的上升沿: 全景 retain 补报(覆盖开机和断线重连两种场景) */
    if (m_online && !prev_mqtt) {
        for (int i = 0; i < DEV_NUM; i++) dev_report(i, dev_val[i]);
    }
    prev_mqtt = m_online;

    if (!online) return;

    for (int i = 0; i < DEV_NUM; i++) {
        uint16_t v;
        if (modbus_get(dev_list[i].reg, &v) == 0 && v != dev_val[i]) {
            dev_val[i] = v;
            card_apply(i, v);
            dev_report(i, v);                    /* 远程/从机侧变化 → 边沿上报 */
        }
    }
}

static lv_obj_t * create_dev_card(lv_obj_t * parent, int i)
{
    lv_obj_t * card = lv_obj_create(parent);
    lv_obj_set_size(card, 315, 200);
    lv_obj_set_style_bg_color(card, lv_color_hex(0x2A333B), LV_STATE_DEFAULT);
    lv_obj_set_style_bg_opa(card, LV_OPA_COVER, LV_STATE_DEFAULT);
    lv_obj_set_style_radius(card, 18, LV_STATE_DEFAULT);
    lv_obj_set_style_border_width(card, 0, LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_width(card, 0, LV_STATE_DEFAULT);
    lv_obj_set_style_pad_all(card, 16, LV_STATE_DEFAULT);
    lv_obj_set_flex_flow(card, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(card, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_add_flag(card, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(card, card_cb, LV_EVENT_CLICKED, (void *)(intptr_t)i);

    lv_obj_t * name = lv_label_create(card);
    lv_label_set_text(name, dev_list[i].name);
    lv_obj_set_style_text_font(name, &lv_font_zh_32, LV_STATE_DEFAULT);
    lv_obj_set_style_text_color(name, lv_color_hex(0xC8D2DA), LV_STATE_DEFAULT);

    lv_obj_t * st = lv_label_create(card);
    lv_label_set_text(st, "关");
    lv_obj_set_style_text_font(st, &lv_font_zh_32, LV_STATE_DEFAULT);
    lv_obj_set_style_text_color(st, lv_color_hex(0x9AA7B0), LV_STATE_DEFAULT);
    lv_obj_set_style_pad_top(st, 16, LV_STATE_DEFAULT);

    dev_card[i] = card;
    dev_state[i] = st;
    return card;
}

static lv_obj_t * create_row(lv_obj_t * parent)
{
    lv_obj_t * row = lv_obj_create(parent);
    lv_obj_set_size(row, LV_PCT(100), LV_SIZE_CONTENT);
    lv_obj_set_style_bg_opa(row, LV_OPA_TRANSP, LV_STATE_DEFAULT);
    lv_obj_set_style_border_width(row, 0, LV_STATE_DEFAULT);
    lv_obj_set_style_pad_all(row, 0, LV_STATE_DEFAULT);
    lv_obj_set_style_pad_top(row, 24, LV_STATE_DEFAULT);
    lv_obj_set_flex_flow(row, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(row, LV_FLEX_ALIGN_SPACE_BETWEEN, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    return row;
}

void ui_ScreenControl_screen_init(void)
{
    lv_obj_set_flex_flow(ui_ScreenControl, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(ui_ScreenControl, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_all(ui_ScreenControl, 30, LV_STATE_DEFAULT);
    lv_obj_set_style_pad_top(ui_ScreenControl, 40, LV_STATE_DEFAULT);

    lv_obj_t * title = lv_label_create(ui_ScreenControl);
    lv_label_set_text(title, "设备控制");
    lv_obj_set_style_text_font(title, &lv_font_zh_32, LV_STATE_DEFAULT);
    lv_obj_set_style_text_color(title, lv_color_hex(0xFFFFFF), LV_STATE_DEFAULT);

    lv_obj_t * row1 = create_row(ui_ScreenControl);
    create_dev_card(row1, 0);
    create_dev_card(row1, 1);

    lv_obj_t * row2 = create_row(ui_ScreenControl);
    create_dev_card(row2, 2);
    create_dev_card(row2, 3);

    lab_offline = lv_label_create(ui_ScreenControl);
    lv_label_set_text(lab_offline, "设备离线");
    lv_obj_set_style_text_font(lab_offline, &lv_font_zh_32, LV_STATE_DEFAULT);
    lv_obj_set_style_text_color(lab_offline, lv_color_hex(0xFF6F61), LV_STATE_DEFAULT);
    lv_obj_set_style_pad_top(lab_offline, 24, LV_STATE_DEFAULT);
    lv_obj_add_flag(lab_offline, LV_OBJ_FLAG_HIDDEN);

    /* 开机恢复: 读持久化状态刷卡片, 并登记到 modbus(上线后同步给从机)
     * 状态上报交给 ctrl_ui_timer 的"Mqtt上升沿全景补报", 此处不发(此刻MQTT还没连上) */
    for (int i = 0; i < DEV_NUM; i++) {
        dev_val[i] = (uint16_t)cfg_get_int(dev_list[i].cfg_key, 0);
        card_apply(i, dev_val[i]);
        modbus_write(dev_list[i].reg, dev_val[i]);
    }

    lv_timer_create(ctrl_ui_timer, 1000, NULL);
}
