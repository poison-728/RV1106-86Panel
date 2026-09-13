#ifndef _SMART_HOME_UI_H
#define _SMART_HOME_UI_H
#include "lvgl/lvgl.h"

extern lv_obj_t * ui_ScreenHome;
extern lv_obj_t * ui_ScreenClimate;
extern lv_obj_t * ui_ScreenMusic;
extern lv_obj_t * ui_ScreenNetwork;
extern lv_obj_t * ui_ScreenControl;
extern lv_font_t lv_font_zh_32;

void ui_init(void);
void ui_ScreenHome_screen_init(void);
void ui_ScreenClimate_screen_init(void);
void ui_ScreenMusic_screen_init(void);
void ui_ScreenNetwork_screen_init(void);
void ui_ScreenControl_screen_init(void);
void ui_ScreenHome_set_sensor(float temp, float hum);
void ui_ScreenClimate_set_sensor(float temp, float hum);
#endif

