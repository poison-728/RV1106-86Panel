#ifndef MODBUS_H
#define MODBUS_H

#include <stdint.h>

/* 保持寄存器定义 (主从两端必须一致!) */
enum {
    MB_REG_AC        = 0,   /* 0x0000 空调开关: 0关 1开 */
    MB_REG_TEMP      = 1,   /* 0x0001 从机温度 x10 (只读, 25.6C=256) */
    MB_REG_HUM       = 2,   /* 0x0002 从机湿度 x10 (只读, 60.5%=605) */
    MB_REG_LIGHT_LIV = 3,   /* 0x0003 客厅灯: 0关 1开 */
    MB_REG_LIGHT_BED = 4,   /* 0x0004 卧室灯: 0关 1开 */
    MB_REG_CURT_LIV  = 5,   /* 0x0005 客厅窗帘: 0关 1开 (预留0~100开度) */
    MB_REG_CURT_BED  = 6,   /* 0x0006 卧室窗帘: 0关 1开 (预留0~100开度) */
    MB_REG_NUM       = 7,
};

/* 打开串口并启动轮询线程; 串口不存在返回-1, 模块进入离线态(UI可用,不崩) */
int  modbus_init(const char *dev, int baud);

/* 非阻塞写: 只登记期望值, 由后台线程实际发送, 立即返回 */
int  modbus_write(uint16_t reg, uint16_t val);

/* 读轮询缓存的寄存器值; 从机在线返回0, 离线返回-1 */
int  modbus_get(uint16_t reg, uint16_t *val);

/* 从机是否在线: 连续3次无响应判离线 */
int  modbus_online(void);

#endif /* MODBUS_H */

