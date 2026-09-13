#ifndef SMART_HOME_AHT20_H
#define SMART_HOME_AHT20_H

/* AHT20 温湿度传感器驱动（I2C，从机地址 0x38） */
int  aht20_open(const char *i2c_dev);      /* 打开总线 + 初始化传感器 */
int  aht20_read(float *temp, float *hum);  /* 触发并读取一次，0=成功 */
void aht20_close(void);

#endif
