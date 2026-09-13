#include "lvgl/lvgl.h"
#include "ui.h"
#include "../cfg.h"
#include <stdio.h>

static lv_obj_t * label_target;
static lv_obj_t * btn_cool;
static lv_obj_t * btn_heat;
static lv_obj_t * btn_fan;
static lv_obj_t * btn_dehum;
static lv_obj_t * lab_temp_v;   /* 温度数值 */
static lv_obj_t * lab_humi_v;   /* 湿度数值 */

/* ---------- 历史曲线 ---------- */
#define CHART_PNTS 60    /* 曲线点数 */
#define CHART_DIV  120   /* 每 N 次 set_sensor 记 1 个点 (500ms×120=60s/点 → 60点=1小时) */

static lv_obj_t * chart;
static lv_chart_series_t * ser_temp;
static lv_chart_series_t * ser_hum;
static int chart_cnt = 0;

static void temp_slider_cb(lv_event_t * e)
{
    lv_obj_t * sl = lv_event_get_target(e);
    int v = (int)lv_slider_get_value(sl);
    lv_label_set_text_fmt(label_target, "%d °C", v);
    cfg_set_int("set_temp", v);
}

static void mode_btn_cb(lv_event_t * e)
{
    lv_obj_t * cur = lv_event_get_target(e);
    lv_obj_clear_state(btn_cool,  LV_STATE_CHECKED);
    lv_obj_clear_state(btn_heat,  LV_STATE_CHECKED);
    lv_obj_clear_state(btn_fan,   LV_STATE_CHECKED);
    lv_obj_clear_state(btn_dehum, LV_STATE_CHECKED);
    lv_obj_add_state(cur, LV_STATE_CHECKED);

    int m = 0;
    if      (cur == btn_heat)  m = 1;
    else if (cur == btn_fan)   m = 2;
    else if (cur == btn_dehum) m = 3;
    cfg_set_int("ac_mode", m);
}

static lv_obj_t * create_mode_btn(lv_obj_t * parent, const char * txt)
{
    lv_obj_t * b = lv_btn_create(parent);
    lv_obj_set_size(b, 150, 64);
    lv_obj_set_style_bg_color(b, lv_color_hex(0x2A333B), LV_STATE_DEFAULT);
    lv_obj_set_style_bg_color(b, lv_color_hex(0x2F6FED), LV_STATE_CHECKED);
    lv_obj_add_event_cb(b, mode_btn_cb, LV_EVENT_CLICKED, NULL);
    lv_obj_t * t = lv_label_create(b);
    lv_label_set_text(t, txt);
    lv_obj_set_style_text_font(t, &lv_font_zh_32, LV_STATE_DEFAULT);
    lv_obj_center(t);
    return b;
}

void ui_ScreenClimate_screen_init(void)
{
    lv_obj_set_flex_flow(ui_ScreenClimate, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(ui_ScreenClimate, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_all(ui_ScreenClimate, 30, LV_STATE_DEFAULT);
    lv_obj_set_style_pad_top(ui_ScreenClimate, 40, LV_STATE_DEFAULT);

    /* 标题 */
    lv_obj_t * title = lv_label_create(ui_ScreenClimate);
    lv_label_set_text(title, "气候控制");
    lv_obj_set_style_text_font(title, &lv_font_zh_32, LV_STATE_DEFAULT);
    lv_obj_set_style_text_color(title, lv_color_hex(0xFFFFFF), LV_STATE_DEFAULT);

    /* 温湿度: 普通文字行 (替代原两张卡片) */
    lv_obj_t * th_row = lv_obj_create(ui_ScreenClimate);
    lv_obj_set_size(th_row, LV_PCT(100), LV_SIZE_CONTENT);
    lv_obj_set_style_bg_opa(th_row, LV_OPA_TRANSP, LV_STATE_DEFAULT);
    lv_obj_set_style_border_width(th_row, 0, LV_STATE_DEFAULT);
    lv_obj_set_style_pad_all(th_row, 0, LV_STATE_DEFAULT);
    lv_obj_set_style_pad_top(th_row, 8, LV_STATE_DEFAULT);
    lv_obj_set_flex_flow(th_row, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(th_row, LV_FLEX_ALIGN_SPACE_BETWEEN, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);

    lab_temp_v = lv_label_create(th_row);
    lv_label_set_text(lab_temp_v, "温度 --.- °C");
    lv_obj_set_style_text_font(lab_temp_v, &lv_font_zh_32, LV_STATE_DEFAULT);
    lv_obj_set_style_text_color(lab_temp_v, lv_color_hex(0xFFFFFF), LV_STATE_DEFAULT);

    lab_humi_v = lv_label_create(th_row);
    lv_label_set_text(lab_humi_v, "湿度 --%");
    lv_obj_set_style_text_font(lab_humi_v, &lv_font_zh_32, LV_STATE_DEFAULT);
    lv_obj_set_style_text_color(lab_humi_v, lv_color_hex(0xFFFFFF), LV_STATE_DEFAULT);

    /* ---------- 历史曲线卡片 ---------- */
    lv_obj_t * chart_card = lv_obj_create(ui_ScreenClimate);
    lv_obj_set_size(chart_card, LV_PCT(100), LV_SIZE_CONTENT);
    lv_obj_set_style_bg_color(chart_card, lv_color_hex(0x2A333B), LV_STATE_DEFAULT);
    lv_obj_set_style_radius(chart_card, 18, LV_STATE_DEFAULT);
    lv_obj_set_style_border_width(chart_card, 0, LV_STATE_DEFAULT);
    lv_obj_set_style_pad_all(chart_card, 12, LV_STATE_DEFAULT);
    lv_obj_set_style_pad_top(chart_card, 12, LV_STATE_DEFAULT);
    lv_obj_set_flex_flow(chart_card, LV_FLEX_FLOW_COLUMN);

    lv_obj_t * lab_title = lv_label_create(chart_card);
    lv_label_set_text(lab_title, "历史曲线");
    lv_obj_set_style_text_font(lab_title, &lv_font_zh_32, LV_STATE_DEFAULT);
    lv_obj_set_style_text_color(lab_title, lv_color_hex(0xC8D2DA), LV_STATE_DEFAULT);
    lv_obj_set_style_pad_bottom(lab_title, 8, LV_STATE_DEFAULT);

    chart = lv_chart_create(chart_card);
    lv_obj_set_size(chart, 620, 280);
    lv_chart_set_type(chart, LV_CHART_TYPE_LINE);
    lv_chart_set_point_count(chart, CHART_PNTS);
    lv_chart_set_range(chart, LV_CHART_AXIS_PRIMARY_Y, 0, 400);    /* 温度 0~40.0°C */
    lv_chart_set_range(chart, LV_CHART_AXIS_SECONDARY_Y, 0, 100); /* 湿度 0~100% */
    lv_chart_set_update_mode(chart, LV_CHART_UPDATE_MODE_SHIFT);
    lv_obj_set_style_bg_opa(chart, LV_OPA_TRANSP, LV_STATE_DEFAULT); /* 透出卡片底色 */
    lv_obj_set_style_line_width(chart, 2, LV_PART_ITEMS);          /* 线宽: 不设可能不可见 */
    lv_obj_set_style_border_width(chart, 0, LV_STATE_DEFAULT);

    /* 关键: 创建数据系列! 没有系列的 chart 是空白的 */
    ser_temp = lv_chart_add_series(chart, lv_palette_main(LV_PALETTE_ORANGE),
                                   LV_CHART_AXIS_PRIMARY_Y);
    ser_hum  = lv_chart_add_series(chart, lv_palette_main(LV_PALETTE_TEAL),
                                   LV_CHART_AXIS_SECONDARY_Y);
    lv_chart_set_all_value(chart, ser_temp, LV_CHART_POINT_NONE);  /* 初始空白 */
    lv_chart_set_all_value(chart, ser_hum,  LV_CHART_POINT_NONE);

    /* ---------- 空调设定 (放最底下) ---------- */
    lv_obj_t * lab_set = lv_label_create(ui_ScreenClimate);
    lv_label_set_text(lab_set, "设定温度");
    lv_obj_set_style_text_font(lab_set, &lv_font_zh_32, LV_STATE_DEFAULT);
    lv_obj_set_style_text_color(lab_set, lv_color_hex(0xCCCCCC), LV_STATE_DEFAULT);
    lv_obj_set_style_pad_top(lab_set, 16, LV_STATE_DEFAULT);

    label_target = lv_label_create(ui_ScreenClimate);
    lv_obj_set_style_text_font(label_target, &lv_font_zh_32, LV_STATE_DEFAULT);
    lv_obj_set_style_text_color(label_target, lv_color_hex(0xFFFFFF), LV_STATE_DEFAULT);

    /* 温度滑块 16~30℃ */
    lv_obj_t * slider = lv_slider_create(ui_ScreenClimate);
    lv_obj_set_width(slider, LV_PCT(100));
    lv_slider_set_range(slider, 16, 30);
    lv_obj_set_style_bg_color(slider, lv_color_hex(0x2A333B), LV_PART_MAIN);
    lv_obj_set_style_bg_color(slider, lv_color_hex(0x2F6FED), LV_PART_INDICATOR);
    lv_obj_set_style_bg_color(slider, lv_color_hex(0xFFFFFF), LV_PART_KNOB);
    lv_obj_add_event_cb(slider, temp_slider_cb, LV_EVENT_VALUE_CHANGED, NULL);
    lv_obj_set_style_pad_top(slider, 12, LV_STATE_DEFAULT);

    /* 模式按钮行：制冷/制热/送风/除湿 */
    lv_obj_t * row = lv_obj_create(ui_ScreenClimate);
    lv_obj_set_size(row, LV_PCT(100), LV_SIZE_CONTENT);
    lv_obj_set_style_bg_opa(row, LV_OPA_TRANSP, LV_STATE_DEFAULT);
    lv_obj_set_style_border_width(row, 0, LV_STATE_DEFAULT);
    lv_obj_set_style_pad_all(row, 0, LV_STATE_DEFAULT);
    lv_obj_set_style_pad_top(row, 20, LV_STATE_DEFAULT);
    lv_obj_set_flex_flow(row, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(row, LV_FLEX_ALIGN_SPACE_BETWEEN, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);

    btn_cool  = create_mode_btn(row, "制冷");
    btn_heat  = create_mode_btn(row, "制热");
    btn_fan   = create_mode_btn(row, "送风");
    btn_dehum = create_mode_btn(row, "除湿");

    /* ---------- 开机恢复持久化状态 ---------- */
    int set_temp = cfg_get_int("set_temp", 25);
    if (set_temp < 16) set_temp = 16;
    if (set_temp > 30) set_temp = 30;
    lv_slider_set_value(slider, set_temp, LV_ANIM_OFF);
    lv_label_set_text_fmt(label_target, "%d °C", set_temp);

    int mode = cfg_get_int("ac_mode", 0);
    if (mode < 0 || mode > 3) mode = 0;
    lv_obj_t * btns[4] = { btn_cool, btn_heat, btn_fan, btn_dehum };
    lv_obj_add_state(btns[mode], LV_STATE_CHECKED);
}

void ui_ScreenClimate_set_sensor(float temp, float hum)
{
    char buf[32];
    if (lab_temp_v) {
        snprintf(buf, sizeof(buf), "温度 %.1f °C", temp);
        lv_label_set_text(lab_temp_v, buf);
    }
    if (lab_humi_v) {
        snprintf(buf, sizeof(buf), "湿度 %.0f%%", hum);
        lv_label_set_text(lab_humi_v, buf);
    }
    /* 历史曲线: 降采样后推入, lv_chart 内部环形缓冲自动滚动 */
    if (chart && ser_temp) {
        if (++chart_cnt >= CHART_DIV) {
            chart_cnt = 0;
            lv_chart_set_next_value(chart, ser_temp, (lv_coord_t)(temp * 10)); /* ×10 保 0.1°C 精度 */
            lv_chart_set_next_value(chart, ser_hum,  (lv_coord_t)hum);
        }
    }
}

