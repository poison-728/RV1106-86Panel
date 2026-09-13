#ifndef WIFI_H
#define WIFI_H

#define WIFI_SCAN_MAX 16

int wifi_connect(const char *ssid, const char *psk);
int wifi_disconnect(void);
int wifi_get_ip(char *ip, int len);

/* 阻塞 2~5 秒, 只能放后台线程调用; 返回扫到的 SSID 个数 */
int wifi_scan(char list[][64], int max);

#endif /* WIFI_H */

