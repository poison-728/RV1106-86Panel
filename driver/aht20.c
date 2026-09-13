#include "aht20.h"
#include <stdio.h>
#include <stdint.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/ioctl.h>
#include <linux/i2c-dev.h>   /* I2C_SLAVE 定义 */

#define AHT20_ADDR     0x38
#define AHT20_CMD_RST  0xBA   /* 软复位 */
#define AHT20_CMD_INIT 0xE1   /* 初始化(校准) */
#define AHT20_CMD_TRIG 0xAC   /* 触发测量 */

static int g_fd = -1;

int aht20_open(const char *i2c_dev)
{
    g_fd = open(i2c_dev, O_RDWR);
    if (g_fd < 0) return -1;

    if (ioctl(g_fd, I2C_SLAVE, AHT20_ADDR) < 0) {
        close(g_fd);
        g_fd = -1;
        return -1;
    }

    /* 软复位，让传感器进入已知状态 */
    uint8_t rst = AHT20_CMD_RST;
    write(g_fd, &rst, 1);
    usleep(20 * 1000);

    /* 读状态字，bit3(0x08)=1 表示已校准；未校准则发初始化 */
    uint8_t st = 0;
    if (read(g_fd, &st, 1) == 1 && !(st & 0x08)) {
        uint8_t init[3] = { AHT20_CMD_INIT, 0x08, 0x00 };
        write(g_fd, init, 3);
        usleep(10 * 1000);
    }
    return 0;
}

int aht20_read(float *temp, float *hum)
{
    if (g_fd < 0 || !temp || !hum) return -1;

    /* 1. 触发测量 */
    uint8_t trig[3] = { AHT20_CMD_TRIG, 0x33, 0x00 };
    if (write(g_fd, trig, 3) != 3) return -1;
    usleep(80 * 1000);   /* 2. 等一轮采样完成 */

    /* 3. 读 6 字节：[0]=状态 [1~2]+[3高4位]=湿度 [3低4位]+[4~5]=温度 */
    uint8_t d[6] = {0};
    if (read(g_fd, d, 6) != 6) return -1;

    uint32_t raw_h = ((uint32_t)d[1] << 12) | ((uint32_t)d[2] << 4) | (d[3] >> 4);
    uint32_t raw_t = ((uint32_t)(d[3] & 0x0F) << 16) | ((uint32_t)d[4] << 8) | d[5];

    *hum  = (float)raw_h * 100.0f / 1048576.0f;          /* 0~100 % */
    *temp = (float)raw_t * 200.0f / 1048576.0f - 50.0f;  /* °C */
    return 0;
}

void aht20_close(void)
{
    if (g_fd >= 0) { close(g_fd); g_fd = -1; }
}
