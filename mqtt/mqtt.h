#ifndef MQTT_H
#define MQTT_H

/*
 * 手写 MQTT 3.1.1 客户端 (QoS0) — 阶段4: 线程化 + 心跳 + 断线重连
 * 架构与 modbus.c 同款: 后台线程轮询, 主线程随时调 mqtt_publish
 *
 * 用法:
 *   mqtt_set_msg_cb(on_msg);                          // 收到下发时的回调
 *   mqtt_start(host, 1883, "luckfox86", "home/+/set");// 起线程, 自动重连
 *   mqtt_publish("home/86panel/temp", "25.6", 0);     // 随时发, 非阻塞
 *   mqtt_stop();                                      // 退出前调用
 */

/* 起后台线程: 连接+握手+订阅+心跳+断线自动重连. 0=成功 */
int  mqtt_start(const char *host, int port, const char *client_id, const char *sub_topic);

/* 停线程并断开 */
void mqtt_stop(void);

/* 当前是否在线 */
int  mqtt_is_connected(void);

/* 发布消息 (QoS0, 线程安全, 离线时丢弃不阻塞). retain=1 则 broker 存住 */
int  mqtt_publish(const char *topic, const char *payload, int retain);

/* 注册回调: 收到订阅的 PUBLISH 时在后台线程里调用 (勿做耗时操作) */
void mqtt_set_msg_cb(void (*cb)(const char *topic, const char *payload));

#endif
