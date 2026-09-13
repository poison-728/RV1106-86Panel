#ifndef CFG_H
#define CFG_H

/* 简单持久化: key=value 文本, 存 /userdata/smarthome.cfg
 * 读不存在返回默认值; 写立即落盘(tmp+rename 原子替换, 掉电不丢整文件) */
int  cfg_get_int(const char *key, int def);
void cfg_set_int(const char *key, int val);

/* 字符串值(如 broker IP): 命中拷贝到 out 返回0, 未命中拷贝默认值返回-1 */
int  cfg_get_str(const char *key, const char *def, char *out, int out_sz);

#endif /* CFG_H */
