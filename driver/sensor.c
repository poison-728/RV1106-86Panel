#include "sensor.h"
#include "aht20.h"
#include <pthread.h>
#include <unistd.h>
#include <math.h>
#include <stdbool.h>
#include <stdio.h>

#define AHT20_I2C_DEV "/dev/i2c-2" 

typedef struct {
    float temp;
    float hum;
    bool  valid;
} sensor_data_t;

static sensor_data_t g_data;
static pthread_mutex_t g_mutex = PTHREAD_MUTEX_INITIALIZER;

static void * sensor_thread(void * arg)
{
    (void)arg;

    /* 尝试连 AHT20；没接线/总线没使能时 open 失败，自动回退模拟数据 */
    bool aht20_ok = (aht20_open(AHT20_I2C_DEV) == 0);
    if (aht20_ok) {
        printf("[sensor] AHT20 已连接，使用真实数据\n");
    } else {
        printf("[sensor] AHT20 未连接，回退模拟数据\n");
    }

    float t = 0.0f;
    while (1) {
        float temp, hum;
        if (!aht20_ok || aht20_read(&temp, &hum) != 0) {
            temp = 25.5f + 2.0f * sinf(t);
            hum  = 55.0f + 10.0f * cosf(t * 0.7f);
            t += 0.4f;
        }

        pthread_mutex_lock(&g_mutex);
        g_data.temp  = temp;
        g_data.hum   = hum;
        g_data.valid = true;
        pthread_mutex_unlock(&g_mutex);

        usleep(1000 * 1000);
    }
    return NULL;
}

int sensor_init(void)
{
    pthread_t tid;
    return pthread_create(&tid, NULL, sensor_thread, NULL);
}

int sensor_get(float *temp, float *hum)
{
    if (!temp || !hum) return -1;

    pthread_mutex_lock(&g_mutex);
    if (!g_data.valid) {
        pthread_mutex_unlock(&g_mutex);
        return -1;
    }
    *temp = g_data.temp;
    *hum  = g_data.hum;
    pthread_mutex_unlock(&g_mutex);
    return 0;
}
