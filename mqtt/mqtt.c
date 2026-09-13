/*
 * mqtt.c — 手写 MQTT 3.1.1 客户端 (QoS0) — 阶段4: 线程化 + 心跳 + 断线重连
 *
 * 架构与 modbus.c 完全同款:
 *   后台线程独占 socket, 循环 read_packet()
 *   主线程随时调 mqtt_publish() (互斥锁保护)
 *   断线 → sleep 3s → 重连 → 重新订阅 (clean session 下 broker 忘了一切)
 */
#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include <stdlib.h>
#include <unistd.h>
#include <errno.h>
#include <time.h>
#include <fcntl.h>
#include <pthread.h>
#include <sys/socket.h>
#include <sys/select.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include "mqtt.h"

static int  g_fd = -1;                       /* -1 = 离线 */
static pthread_t       g_tid;
static volatile int    g_running = 0;
static pthread_mutex_t g_lock = PTHREAD_MUTEX_INITIALIZER;

static char g_host[48];
static int  g_port;
static char g_cid[24];
static char g_sub[64];                       /* 空串 = 不订阅 */

static void (*g_msg_cb)(const char *topic, const char *payload) = NULL;

/* 剩余长度变长编码: 每字节低7位有效, 最高位=1 表示后面还有字节 */
static int encode_remlen(int len, uint8_t *out)
{
    int n = 0;
    do {
        uint8_t b = (uint8_t)(len % 128);
        len /= 128;
        if (len > 0) b |= 0x80;
        out[n++] = b;
    } while (len > 0);
    return n;
}

/* 教学用: 把原始字节打出来, 对照协议文档看 */
static void hexdump(const char *tag, const uint8_t *buf, int len)
{
    printf("%s [%d bytes]:", tag, len);
    for (int i = 0; i < len; i++) printf(" %02X", buf[i]);
    printf("\n");
}

/*
 * ---- connect 限时 5 秒: 非阻塞 connect + select ----
 * 治"IP 漂移后干挂 2 分钟"的保险丝
 */
static int tcp_connect(const char *host, int port)
{
    int fd = socket(AF_INET, SOCK_STREAM, 0);
    if (fd < 0) return -1;

    long fl = fcntl(fd, F_GETFL, 0);
    fcntl(fd, F_SETFL, fl | O_NONBLOCK);     /* 切非阻塞 */

    struct sockaddr_in srv;
    memset(&srv, 0, sizeof(srv));
    srv.sin_family = AF_INET;
    srv.sin_port   = htons((uint16_t)port);
    inet_pton(AF_INET, host, &srv.sin_addr);

    int r = connect(fd, (struct sockaddr *)&srv, sizeof(srv));
    if (r == 0) {
        /* 本机回环常这样: 瞬间连上 */
    } else if (errno == EINPROGRESS) {
        fd_set wset;                         /* select 等它可写 = 握手完成 */
        FD_ZERO(&wset);
        FD_SET(fd, &wset);
        struct timeval tv = { 5, 0 };
        if (select(fd + 1, NULL, &wset, NULL, &tv) <= 0) {
            close(fd); return -1;            /* 5秒没应答(SYN石沉大海) */
        }
        int err = 0; socklen_t len = sizeof(err);
        getsockopt(fd, SOL_SOCKET, SO_ERROR, &err, &len);
        if (err) { close(fd); return -1; }   /* 拒绝/不可达等 */
    } else {
        close(fd); return -1;
    }

    fcntl(fd, F_SETFL, fl);                  /* 恢复阻塞 */
    struct timeval rt = { 1, 0 };            /* recv 1秒超时 → 轮询节拍 */
    setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &rt, sizeof(rt));
    return fd;
}

/* 读恰好 n 字节; 0=读满 1=超时 -1=断 */
static int readn(int fd, uint8_t *buf, int n)
{
    int got = 0;
    while (got < n) {
        int r = recv(fd, buf + got, n - got, 0);
        if (r < 0 && (errno == EAGAIN || errno == EWOULDBLOCK)) return 1;
        if (r <= 0) return -1;
        got += r;
    }
    return 0;
}

/* ---- CONNECT 握手 (阶段1代码, 加 fd 参数) ---- */
static int mqtt_handshake(int fd, const char *client_id)
{
    uint8_t pkt[256];
    int p = 0;
    size_t idlen = strlen(client_id);
    int rem = (2 + 4) + 1 + 1 + 2 + (2 + (int)idlen);

    pkt[p++] = 0x10;                         /* CONNECT */
    p += encode_remlen(rem, pkt + p);
    pkt[p++] = 0x00; pkt[p++] = 0x04;
    pkt[p++] = 'M'; pkt[p++] = 'Q'; pkt[p++] = 'T'; pkt[p++] = 'T';
    pkt[p++] = 0x04;                         /* 3.1.1 */
    pkt[p++] = 0x02;                         /* clean session */
    pkt[p++] = 0x00; pkt[p++] = 0x3C;        /* keepalive 60s */
    pkt[p++] = (uint8_t)(idlen >> 8);
    pkt[p++] = (uint8_t)(idlen & 0xFF);
    memcpy(pkt + p, client_id, idlen); p += (int)idlen;

    hexdump("[mqtt] send CONNECT", pkt, p);
    if (send(fd, pkt, p, 0) != p) return -1;

    uint8_t ack[4];
    if (readn(fd, ack, 4) != 0) return -1;
    hexdump("[mqtt] recv CONNACK", ack, 4);
    if (ack[0] != 0x20 || ack[3] != 0x00) return -1;
    return 0;
}

/* ---- SUBSCRIBE (阶段3代码, 加 fd 参数) ---- */
static int mqtt_sub(int fd, const char *topic)
{
    size_t tlen = strlen(topic);
    uint8_t pkt[160];
    int p = 0;
    int rem = 2 + 2 + (int)tlen + 1;

    pkt[p++] = 0x82;
    p += encode_remlen(rem, pkt + p);
    pkt[p++] = 0x00; pkt[p++] = 0x01;        /* 报文标识符=1 */
    pkt[p++] = (uint8_t)(tlen >> 8);
    pkt[p++] = (uint8_t)(tlen & 0xFF);
    memcpy(pkt + p, topic, tlen); p += (int)tlen;
    pkt[p++] = 0x00;                         /* QoS0 */

    hexdump("[mqtt] send SUBSCRIBE", pkt, p);
    if (send(fd, pkt, p, 0) != p) return -1;

    uint8_t ack[5];
    if (readn(fd, ack, 5) != 0) return -1;
    hexdump("[mqtt] recv SUBACK", ack, 5);
    return (ack[0] == 0x90 && ack[4] != 0x80) ? 0 : -1;
}

/* ---- PINGREQ: 全协议最简报文, 就 2 字节 C0 00 ---- */
static int mqtt_ping(int fd)
{
    uint8_t pkt[2] = { 0xC0, 0x00 };
    if (send(fd, pkt, 2, 0) != 2) return -1;
    printf("[mqtt] PINGREQ\n");
    return 0;
}

/*
 * ---- 切流 + 分发 (阶段3核心代码, 加 fd 参数) ----
 * 1字节固定头 → 变长剩余长度 → body → 是PUBLISH则回调
 */
static int read_packet(int fd)
{
    uint8_t hdr;
    int r = readn(fd, &hdr, 1);
    if (r != 0) return r;

    uint32_t remlen = 0, mult = 1;
    uint8_t b;
    do {
        if (readn(fd, &b, 1) != 0) return -1;
        remlen += (b & 0x7F) * mult;
        mult *= 128;
    } while (b & 0x80);

    uint8_t body[512];
    if (remlen > sizeof(body)) {
        uint32_t left = remlen;
        uint8_t sink[256];
        while (left) {
            uint32_t n = left > sizeof(sink) ? sizeof(sink) : left;
            if (readn(fd, sink, (int)n) != 0) return -1;
            left -= n;
        }
        printf("[mqtt] 超大报文 %u 字节已丢弃\n", remlen);
        return 0;
    }
    if (remlen > 0 && readn(fd, body, (int)remlen) != 0) return -1;

    int type = hdr >> 4;
    int qos  = (hdr >> 1) & 0x03;

    if (type == 3) {                         /* PUBLISH */
        int tlen = (body[0] << 8) | body[1];
        if (tlen > 120 || (int)remlen < 2 + tlen) return 0;

        char topic[128];
        memcpy(topic, body + 2, tlen);
        topic[tlen] = 0;

        int off = 2 + tlen;
        if (qos > 0) off += 2;

        int plen = (int)remlen - off;
        char payload[256];
        if (plen < 0) plen = 0;
        if (plen > 255) plen = 255;
        memcpy(payload, body + off, plen);
        payload[plen] = 0;

        if (g_msg_cb) g_msg_cb(topic, payload);
        else printf("[mqtt] PUBLISH \"%s\" = \"%s\"\n", topic, payload);
    } else if (type == 13) {
        printf("[mqtt] PINGRESP\n");         /* 心跳回音 */
    } else {
        printf("[mqtt] 忽略 type=%d\n", type);
    }
    return 0;
}

/* ---- 后台线程: 与 modbus.c poll_thread 同款骨架 ---- */
static void *mqtt_thread(void *arg)
{
    (void)arg;
    while (g_running) {
        /* 1. 连接 + 握手 + 订阅 (三连, 任一失败 3s 后重来) */
        int fd = tcp_connect(g_host, g_port);
        if (fd < 0) {
            printf("[mqtt] 连不上 %s:%d, 3秒后重试\n", g_host, g_port);
            sleep(3);
            continue;
        }
        if (mqtt_handshake(fd, g_cid) != 0 || (g_sub[0] && mqtt_sub(fd, g_sub) != 0)) {
            printf("[mqtt] 握手/订阅失败, 3秒后重试\n");
            close(fd);
            sleep(3);
            continue;
        }

        pthread_mutex_lock(&g_lock);
        g_fd = fd;                           /* 拿到 socket 才算在线 */
        pthread_mutex_unlock(&g_lock);
        printf("[mqtt] === 在线 ===\n");
        mqtt_publish("home/86panel/state", "online", 1);  /* retain 播报上线 */

        /* 2. 内层循环: 听包 + 每30s心跳 (keepalive=60 的一半) */
        time_t last_ping = time(NULL);
        while (g_running) {
            int r = read_packet(fd);
            if (r < 0) { printf("[mqtt] === 连接断开 ===\n"); break; }
            if (time(NULL) - last_ping >= 30) {
                if (mqtt_ping(fd) < 0) { printf("[mqtt] === 心跳失败 ===\n"); break; }
                last_ping = time(NULL);
            }
        }

        /* 3. 断了: 关socket 回到外层循环重连 */
        pthread_mutex_lock(&g_lock);
        close(g_fd);
        g_fd = -1;
        pthread_mutex_unlock(&g_lock);
        if (g_running) { printf("[mqtt] 3秒后重连...\n"); sleep(3); }
    }
    return NULL;
}

/* ---- 对外 API ---- */
void mqtt_set_msg_cb(void (*cb)(const char *topic, const char *payload))
{
    g_msg_cb = cb;
}

int mqtt_start(const char *host, int port, const char *client_id, const char *sub_topic)
{
    if (g_running) return 0;
    snprintf(g_host, sizeof(g_host), "%s", host);
    snprintf(g_cid,  sizeof(g_cid),  "%s", client_id);
    snprintf(g_sub,  sizeof(g_sub),  "%s", sub_topic ? sub_topic : "");
    g_port = port;

    g_running = 1;
    if (pthread_create(&g_tid, NULL, mqtt_thread, NULL) != 0) {
        g_running = 0;
        return -1;
    }
    return 0;
}

void mqtt_stop(void)
{
    if (!g_running) return;
    g_running = 0;
    pthread_join(g_tid, NULL);               /* 等线程退出 */
    if (g_fd >= 0) { close(g_fd); g_fd = -1; }
    printf("[mqtt] 已停止\n");
}

/* 线程安全: 锁内取 fd + 发送, 防止重连换 fd 的瞬间发到旧 socket */
int mqtt_publish(const char *topic, const char *payload, int retain)
{
    if (!topic || !payload) return -1;
    size_t tlen = strlen(topic);
    size_t plen = strlen(payload);
    if (tlen == 0 || tlen > 128 || plen > 128) return -1;

    uint8_t pkt[300];
    int p = 0;
    int rem = (int)(2 + tlen + plen);

    pkt[p++] = (uint8_t)(0x30 | (retain ? 0x01 : 0x00));
    p += encode_remlen(rem, pkt + p);
    pkt[p++] = (uint8_t)(tlen >> 8);
    pkt[p++] = (uint8_t)(tlen & 0xFF);
    memcpy(pkt + p, topic, tlen); p += (int)tlen;
    memcpy(pkt + p, payload, plen); p += (int)plen;

    pthread_mutex_lock(&g_lock);
    int fd = g_fd;
    int ok = -1;
    if (fd >= 0) {
        hexdump("[mqtt] send PUBLISH", pkt, p);
        ok = (send(fd, pkt, p, 0) == p) ? 0 : -1;
    }
    pthread_mutex_unlock(&g_lock);
    return ok;                               /* 离线时丢弃, 调用方不阻塞 */
}

int mqtt_is_connected(void)
{
    return g_fd >= 0;
}
