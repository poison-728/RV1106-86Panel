#include "ui.h"

lv_obj_t * ui_ScreenHome;
lv_obj_t * ui_ScreenClimate;
lv_obj_t * ui_ScreenMusic;
lv_obj_t * ui_ScreenNetwork;
lv_obj_t * ui_ScreenControl;

static void theme_all_tab_buttons(lv_obj_t * obj)
{
    uint32_t i;
    if (lv_obj_check_type(obj, &lv_btn_class)) {
        lv_obj_set_style_bg_color(obj, lv_color_hex(0x1A2026), LV_STATE_DEFAULT);
        lv_obj_set_style_bg_opa(obj,  LV_OPA_COVER,            LV_STATE_DEFAULT);
        lv_obj_set_style_text_color(obj, lv_color_hex(0x9AA7B0), LV_STATE_DEFAULT);
        lv_obj_set_style_bg_color(obj, lv_color_hex(0x2A333B), LV_STATE_CHECKED);
        lv_obj_set_style_text_color(obj, lv_color_hex(0xFFFFFF), LV_STATE_CHECKED);
    }
    for (i = 0; i < lv_obj_get_child_cnt(obj); i++) {
        theme_all_tab_buttons(lv_obj_get_child(obj, i));
    }
}

/* 找到装着按钮的 tab 栏容器,把它的白底也刷成深色 */
static void darken_tab_bar(lv_obj_t * tv)
{
    uint32_t i, n = lv_obj_get_child_cnt(tv);
    for (i = 0; i < n; i++) {
        lv_obj_t * c = lv_obj_get_child(tv, i);
        lv_obj_t * sub;
        if (lv_obj_get_child_cnt(c) == 0) continue;
        sub = lv_obj_get_child(c, 0);
        if (lv_obj_check_type(sub, &lv_btn_class)) {
            lv_obj_set_style_bg_color(c, lv_color_hex(0x1A2026), LV_PART_MAIN);
            lv_obj_set_style_bg_opa(c,  LV_OPA_COVER,            LV_PART_MAIN);
            lv_obj_set_style_border_width(c, 0, LV_PART_MAIN);
        }
    }
}

static void apply_dark_tab_theme(lv_obj_t * tv)
{
    lv_obj_t * bar = lv_tabview_get_tab_btns(tv);
    lv_obj_set_style_bg_color(bar, lv_color_hex(0x12161A), LV_STATE_DEFAULT);
    lv_obj_set_style_bg_opa(bar, LV_OPA_COVER, LV_STATE_DEFAULT);
    lv_obj_set_style_border_width(bar, 0, LV_STATE_DEFAULT);
    lv_obj_set_style_radius(bar, 0, LV_STATE_DEFAULT);

    lv_obj_set_style_bg_color(bar, lv_color_hex(0x12161A), LV_PART_ITEMS | LV_STATE_DEFAULT);
    lv_obj_set_style_text_color(bar, lv_color_hex(0x9AA7B0), LV_PART_ITEMS | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_color(bar, lv_color_hex(0x2A333B), LV_PART_ITEMS | LV_STATE_CHECKED);
    lv_obj_set_style_text_color(bar, lv_color_hex(0xFFFFFF), LV_PART_ITEMS | LV_STATE_CHECKED);
}

void ui_init(void)
{
    lv_disp_t  * dispp;
    lv_theme_t * theme;
    lv_obj_t   * tv;
    lv_obj_t   * content;

    dispp = lv_disp_get_default();
    theme = lv_theme_default_init(dispp,
            lv_palette_main(LV_PALETTE_BLUE),
            lv_palette_main(LV_PALETTE_RED), false,
            LV_FONT_DEFAULT);
    lv_disp_set_theme(dispp, theme);

    tv = lv_tabview_create(lv_scr_act(), LV_DIR_TOP, 56);

    ui_ScreenHome    = lv_tabview_add_tab(tv, "Home");
    ui_ScreenClimate = lv_tabview_add_tab(tv, "Climate");
    ui_ScreenMusic   = lv_tabview_add_tab(tv, "Music");
    ui_ScreenNetwork = lv_tabview_add_tab(tv, "Network");
    ui_ScreenControl = lv_tabview_add_tab(tv, "Conctrl");

    content = lv_tabview_get_content(tv);
    lv_obj_set_scrollbar_mode(content, LV_SCROLLBAR_MODE_OFF);
    lv_obj_set_scroll_dir(content, LV_DIR_HOR);

    ui_ScreenHome_screen_init();
    ui_ScreenClimate_screen_init();
    ui_ScreenMusic_screen_init();
    ui_ScreenNetwork_screen_init();
    ui_ScreenControl_screen_init();

    apply_dark_tab_theme(tv);

    /* 统一五个 Tab 页为深色背景（关键：bg_opa 必须 COVER，否则透明透白） */
    lv_obj_set_style_bg_color(ui_ScreenHome,    lv_color_hex(0x1A2026), LV_STATE_DEFAULT);
    lv_obj_set_style_bg_opa(ui_ScreenHome,      LV_OPA_COVER, LV_STATE_DEFAULT);

    lv_obj_set_style_bg_color(ui_ScreenClimate, lv_color_hex(0x1A2026), LV_STATE_DEFAULT);
    lv_obj_set_style_bg_opa(ui_ScreenClimate,   LV_OPA_COVER, LV_STATE_DEFAULT);

    lv_obj_set_style_bg_color(ui_ScreenMusic,   lv_color_hex(0x1A2026), LV_STATE_DEFAULT);
    lv_obj_set_style_bg_opa(ui_ScreenMusic,     LV_OPA_COVER, LV_STATE_DEFAULT);

    lv_obj_set_style_bg_color(ui_ScreenNetwork, lv_color_hex(0x1A2026), LV_STATE_DEFAULT);
    lv_obj_set_style_bg_opa(ui_ScreenNetwork,   LV_OPA_COVER, LV_STATE_DEFAULT);

    lv_obj_set_style_bg_color(ui_ScreenControl, lv_color_hex(0x1A2026), LV_STATE_DEFAULT);
    lv_obj_set_style_bg_opa(ui_ScreenControl,   LV_OPA_COVER, LV_STATE_DEFAULT);
}

