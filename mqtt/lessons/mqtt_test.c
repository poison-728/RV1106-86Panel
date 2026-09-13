/*
 * mqtt_test.c — 阶段4验收: 线程化 + 心跳 + 断线重连
 * 用法: ./mqtt_test <PC的IP>     (不带参数默认 192.168.187.85)
 *
 * 后台线程: 听下发 + 心跳 + 断线重连 (全自动)
 * 主线程  : 每5秒 publish 一个递增序号 (证明边听边说互不干扰)
 * Ctrl+C 退出
 */
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <signal.h>
#include "mqtt.h"

static volatile int g_run = 1;
static void on_sig(int sig) { (void)sig; g_run = 0; }

static void on_msg(const char *topic, const char *payload)
{
    printf("      ↓ 收到下发: topic=\"%s\" payload=\"%s\"\n", topic, payload);
}

int main(int argc, char **argv)
{
    const char *host = argc > 1 ? argv[1] : "192.168.187.85";
    char buf[16];
    int seq = 0;

    signal(SIGINT, on_sig);

    mqtt_set_msg_cb(on_msg);
    if (mqtt_start(host, 1883, "luckfox86", "home/+/set") != 0) {
        printf("[test] 起线程失败\n");
        return 1;
    }

    printf("[test] 运行中: 后台听包+心跳, 主线程每5秒上报. Ctrl+C 退出\n");
    while (g_run) {
        sleep(5);
        if (!mqtt_is_connected()) { printf("[test] (离线中, 序号暂停)\n"); continue; }
        snprintf(buf, sizeof(buf), "%d", seq++);
        mqtt_publish("home/86panel/up", buf, 0);
    }

    mqtt_stop();
    printf("[test] 阶段4结束\n");
    return 0;
}
