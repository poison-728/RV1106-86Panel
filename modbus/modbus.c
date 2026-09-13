/* modbus.c — Modbus RTU 主机 (自实现: CRC16 + 03读/06写 + 轮询线程 + 掉线检测)
 *
 * 线程模型 (与 sensor.c / music.c 同款):
 *   UI线程 --modbus_write(非阻塞登记)--> 后台轮询线程 --> /dev/ttyS?
 *   UI线程 <--modbus_get/modbus_online(互斥锁读缓存)<--
 *
 * 串口: 自动收发RS485模块, 无需控制DE/RE方向
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <termios.h>
#include <pthread.h>
#include <sys/select.h>

#include "modbus.h"

#define MB_SLAVE_ADDR  0x01
#define MB_TIMEOUT_MS  300            /* 单次应答超时 */
#define MB_RETRY       3              /* 连续失败次数 -> 掉线 */
#define MB_POLL_US     (500 * 1000)    /* 轮询周期 500ms */

static int g_fd = -1;
static volatile int g_running = 0;
static pthread_t g_tid;

static pthread_mutex_t g_lock = PTHREAD_MUTEX_INITIALIZER;
static uint16_t g_cache[MB_REG_NUM];  /* 从机回读的真实值 */
static uint16_t g_shadow[MB_REG_NUM];/* UI期望写入的值 */
static uint8_t  g_dirty[MB_REG_NUM]; /* 有待发送的写请求 */
static int g_online = 0;

/* CRC16-Modbus: 多项式0xA001, 初值0xFFFF, 应答里低字节在前 */
static uint16_t crc16(const uint8_t *buf, int len)
{
    uint16_t crc = 0xFFFF;
    for (int i = 0; i < len; i++) {
        crc ^= buf[i];
        for (int j = 0; j < 8; j++) {
            if (crc & 1) crc = (uint16_t)((crc >> 1) ^ 0xA001);
            else         crc >>= 1;
        }
    }
    return crc;
}

static void append_crc(uint8_t *frame, int len)
{
    uint16_t crc = crc16(frame, len);
    frame[len]      = (uint8_t)(crc & 0xFF);
    frame[len + 1]  = (uint8_t)(crc >> 8);
}

/* 带超时收满 want 字节 */
static int rx_bytes(uint8_t *buf, int want, int timeout_ms)
{
    int got = 0;
    while (got < want) {
        fd_set rf;
        struct timeval tv;
        tv.tv_sec  = timeout_ms / 1000;
        tv.tv_usec = (timeout_ms % 1000) * 1000;
        FD_ZERO(&rf);
        FD_SET(g_fd, &rf);
        int n = select(g_fd + 1, &rf, NULL, NULL, &tv);
        if (n <= 0) return -1;
        n = read(g_fd, buf + got, (size_t)(want - got));
        if (n <= 0) return -1;
        got += n;
    }
    return 0;
}

/* 发请求收应答; 校验地址和CRC; resp_len为应答定长 */
static int transact(const uint8_t *req, int req_len,
                    uint8_t *resp, int resp_len)
{
    tcflush(g_fd, TCIFLUSH);
    if (write(g_fd, req, (size_t)req_len) != req_len) return -1;

    if (rx_bytes(resp, resp_len, MB_TIMEOUT_MS) != 0) return -1;
    if (resp[0] != MB_SLAVE_ADDR) return -1;

    uint16_t calc = crc16(resp, resp_len - 2);
    uint16_t recv = (uint16_t)(resp[resp_len - 2] | (resp[resp_len - 1] << 8));
    if (calc != recv) return -1;
    return 0;
}

/* 功能码06 写单寄存器: 从机应答=原样回显请求帧 */
static int write_reg_raw(uint16_t reg, uint16_t val)
{
    uint8_t req[8], resp[8];
    req[0] = MB_SLAVE_ADDR;
    req[1] = 0x06;
    req[2] = (uint8_t)(reg >> 8);
    req[3] = (uint8_t)(reg & 0xFF);
    req[4] = (uint8_t)(val >> 8);
    req[5] = (uint8_t)(val & 0xFF);
    append_crc(req, 6);

    if (transact(req, 8, resp, 8) != 0) return -1;
    if (memcmp(req, resp, 8) != 0) return -1;
    return 0;
}

/* 功能码03 读保持寄存器: 一次读全部 */
static int read_regs_raw(uint16_t *out, int num)
{
    uint8_t req[8];
    uint8_t resp[5 + MB_REG_NUM * 2];
    int resp_len = 5 + num * 2;

    req[0] = MB_SLAVE_ADDR;
    req[1] = 0x03;
    req[2] = 0;
    req[3] = 0;
    req[4] = 0;
    req[5] = (uint8_t)num;
    append_crc(req, 6);

    if (transact(req, 8, resp, resp_len) != 0) return -1;
    if (resp[1] != 0x03 || resp[2] != num * 2) return -1;

    for (int i = 0; i < num; i++)
        out[i] = (uint16_t)(resp[3 + i * 2] << 8 | resp[4 + i * 2]);
    return 0;
}

static void *poll_thread(void *arg)
{
    (void)arg;
    uint16_t tmp[MB_REG_NUM];
    uint8_t  dirty[MB_REG_NUM];
    uint16_t shadow[MB_REG_NUM];
    int fail = 0;

    while (g_running) {
        int ok = 1;

        /* 1. 先发挂起的写请求 */
        pthread_mutex_lock(&g_lock);
        memcpy(dirty, g_dirty, sizeof(dirty));
        memcpy(shadow, g_shadow, sizeof(shadow));
        pthread_mutex_unlock(&g_lock);

        for (int r = 0; r < MB_REG_NUM; r++) {
            if (!dirty[r]) continue;
            if (write_reg_raw((uint16_t)r, shadow[r]) == 0) {
                pthread_mutex_lock(&g_lock);
                g_dirty[r] = 0;
                g_cache[r] = shadow[r];
                pthread_mutex_unlock(&g_lock);
            } else {
                ok = 0;
            }
        }

        /* 2. 整体回读: 从机的真实状态 */
        if (read_regs_raw(tmp, MB_REG_NUM) == 0) {
            pthread_mutex_lock(&g_lock);
            memcpy(g_cache, tmp, sizeof(tmp));
            pthread_mutex_unlock(&g_lock);
        } else {
            ok = 0;
        }

        /* 3. 掉线判定 */
        pthread_mutex_lock(&g_lock);
        if (ok) {
            fail = 0;
            g_online = 1;
        } else if (++fail >= MB_RETRY) {
            g_online = 0;
        }
        pthread_mutex_unlock(&g_lock);

        usleep(MB_POLL_US);
    }
    return NULL;
}

int modbus_init(const char *dev, int baud)
{
    if (g_running) return 0;

    g_fd = open(dev, O_RDWR | O_NOCTTY);
    if (g_fd < 0) return -1;               /* 串口不存在: 模块保持离线态 */

    struct termios tio;
    if (tcgetattr(g_fd, &tio) != 0) { close(g_fd); g_fd = -1; return -1; }

    speed_t sp = B9600;
    if (baud == 4800)       sp = B4800;
    else if (baud == 19200)  sp = B19200;
    else if (baud == 115200) sp = B115200;

    cfmakeraw(&tio);
    cfsetispeed(&tio, sp);
    cfsetospeed(&tio, sp);
    tio.c_cflag |= CLOCAL | CREAD;
    tio.c_cflag &= ~(CSTOPB | PARENB);    /* 8N1 */
    tio.c_cflag &= ~CSIZE;
    tio.c_cflag |= CS8;
    tio.c_cc[VMIN]  = 0;
    tio.c_cc[VTIME] = 0;

    if (tcsetattr(g_fd, TCSANOW, &tio) != 0) {
        close(g_fd); g_fd = -1; return -1;
    }
    tcflush(g_fd, TCIFLUSH);

    g_running = 1;
    if (pthread_create(&g_tid, NULL, poll_thread, NULL) != 0) {
        g_running = 0;
        close(g_fd); g_fd = -1;
        return -1;
    }
    return 0;
}

int modbus_write(uint16_t reg, uint16_t val)
{
    if (reg >= MB_REG_NUM) return -1;
    pthread_mutex_lock(&g_lock);
    g_shadow[reg] = val;
    g_dirty[reg] = 1;
    pthread_mutex_unlock(&g_lock);
    return 0;
}

int modbus_get(uint16_t reg, uint16_t *val)
{
    if (reg >= MB_REG_NUM || val == NULL) return -1;
    pthread_mutex_lock(&g_lock);
    *val = g_cache[reg];
    int online = g_online;
    pthread_mutex_unlock(&g_lock);
    return online ? 0 : -1;
}

int modbus_online(void)
{
    pthread_mutex_lock(&g_lock);
    int v = g_online;
    pthread_mutex_unlock(&g_lock);
    return v;
}

