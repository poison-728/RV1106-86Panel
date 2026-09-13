#define _DEFAULT_SOURCE
#include <unistd.h>
#include <time.h>
#include <stdlib.h>
#include <sys/time.h>
#include "music.h"
#include "modbus.h"
#include "mqtt.h"
#include "cfg.h"
#include "lvgl/lvgl.h"
#include "lv_drivers/display/fbdev.h"
#include "lv_drivers/indev/evdev.h"
#include "ui.h"
#include "sensor.h"

#define DRAW_BUF_SIZE (720 * 720 / 10)

static lv_color_t buf1[DRAW_BUF_SIZE];
static lv_disp_draw_buf_t draw_buf;
static lv_disp_drv_t disp_drv;

uint32_t custom_tick_get(void)
{
    struct timeval tv;
    gettimeofday(&tv, NULL);
    return (uint32_t)(tv.tv_sec * 1000 + tv.tv_usec / 1000);
}

static void sensor_ui_timer(lv_timer_t * t)
{
    (void)t;
    float temp, hum;
    if (sensor_get(&temp, &hum) == 0) {
        ui_ScreenHome_set_sensor(temp, hum);
        ui_ScreenClimate_set_sensor(temp, hum);
    }
}

static void on_mqtt_msg(const char *topic, const char *payload)
{
    printf("[mqtt-app] 收到下发: %s = %s\n", topic, payload);

    int on = (strcmp(payload, "1") == 0) ? 1 : 0;

    if      (strcmp(topic, "home/light_liv/set") == 0) modbus_write(MB_REG_LIGHT_LIV, on);
    else if (strcmp(topic, "home/light_bed/set") == 0) modbus_write(MB_REG_LIGHT_BED, on);
    else if (strcmp(topic, "home/curt_liv/set")  == 0) modbus_write(MB_REG_CURT_LIV,  on);
    else if (strcmp(topic, "home/curt_bed/set")  == 0) modbus_write(MB_REG_CURT_BED,  on);
    else if (strcmp(topic, "home/ac/set")        == 0) modbus_write(MB_REG_AC,        on);
    else printf("[mqtt-app] 未知topic, 忽略\n");
}

int main(void)
{
    lv_init();
    fbdev_init();      /* 从 /dev/fb0 读实际分辨率 */
    evdev_init();      /* 触摸 */

    setenv("TZ", "CST-8", 1);   /* POSIX 格式: CST-8 = UTC+8, 减号表示东八区 */
    tzset();

    uint32_t hor = 720;
    uint32_t ver = 720;

    lv_disp_draw_buf_init(&draw_buf, buf1, NULL, DRAW_BUF_SIZE);
    lv_disp_drv_init(&disp_drv);
    disp_drv.hor_res  = hor;
    disp_drv.ver_res  = ver;
    disp_drv.flush_cb = fbdev_flush;
    disp_drv.draw_buf = &draw_buf;
    lv_disp_drv_register(&disp_drv);

    music_init();
    modbus_init("/dev/ttyS2", 9600);
    mqtt_set_msg_cb(on_mqtt_msg);
    char broker_ip[64];
    cfg_get_str("mqtt_ip", "192.168.187.85", broker_ip, sizeof(broker_ip));
    mqtt_start(broker_ip, 1883, "luckfox86", "home/+/set");

    static lv_indev_drv_t indev_drv;
    lv_indev_drv_init(&indev_drv);
    indev_drv.type    = LV_INDEV_TYPE_POINTER;
    indev_drv.read_cb = evdev_read;
    lv_indev_drv_register(&indev_drv);

    ui_init();
    sensor_init();
    lv_timer_create(sensor_ui_timer, 500, NULL);

    while (1) {
        lv_timer_handler();
        usleep(5000);
    }
    return 0;
}
