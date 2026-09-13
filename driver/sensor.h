#ifndef SMART_HOME_SENSOR_H
#define SMART_HOME_SENSOR_H

/* 传感器模块对外接口 */
int sensor_init(void);                      /* 创建采集线程 */
int sensor_get(float *temp, float *hum);    /* UI 线程轮询读取最新温湿度 */

#endif
