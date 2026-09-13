#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/socket.h>
#include <sys/ioctl.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <net/if.h>
#include "wifi.h"

#define WPA_CONF "/etc/wpa_supplicant.conf"

/* ---------- 工具: 去掉字符串尾部换行/回车/空格 ----------
   脏数据从这里彻底拦截, 换行混进 SSID 会写坏 conf */
static void trim_tail(char *s)
{
    int len = (int)strlen(s);
    while (len > 0 && (s[len-1] == '\n' || s[len-1] == '\r' || s[len-1] == ' '))
        s[--len] = '\0';
}

/* ---------- 工具: iw 的 \xNN 转义解码回 UTF-8 ---------- */
static int hex_val(char c)
{
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return -1;
}

static void decode_escapes(const char *src, char *dst, int dst_size)
{
    int si = 0, di = 0;
    while (src[si] != '\0' && di < dst_size - 1) {
        if (src[si] == '\\' && src[si + 1] == 'x') {
            int hi = hex_val(src[si + 2]);
            int lo = hex_val(src[si + 3]);
            if (hi >= 0 && lo >= 0) {
                dst[di++] = (char)((hi << 4) | lo);
                si += 4;
                continue;
            }
        }
        dst[di++] = src[si++];
    }
    dst[di] = '\0';
}

/* ---------- 已关联返回 1 (关联层和 IP 层分开判断, 防误报) ---------- */
static int wifi_is_associated(void)
{
    FILE *fp = popen("iw dev wlan0 link 2>/dev/null", "r");
    if (!fp) return 0;

    char line[256];
    int ok = 0;
    while (fgets(line, sizeof(line), fp)) {
        if (strstr(line, "SSID:")) { ok = 1; break; }
    }
    pclose(fp);
    return ok;
}

/* ---------- 扫描热点: 后台线程调用 ---------- */
int wifi_scan(char list[][64], int max)
{
    FILE *fp = popen("ifconfig wlan0 up 2>/dev/null; iw dev wlan0 scan 2>/dev/null", "r");
    if (!fp) return 0;

    char line[512];
    int cnt = 0;

    while (fgets(line, sizeof(line), fp)) {
        trim_tail(line);                        /* 第一道: 整行剥换行 */

        char *p = strstr(line, "SSID: ");
        if (!p) continue;
        p += 6;
        trim_tail(p);                          /* 第二道: SSID 部分再剥 */

        char decoded[64];
        decode_escapes(p, decoded, sizeof(decoded));
        if (decoded[0] == '\0') continue;      /* 隐藏网络跳过 */

        int dup = 0;                           /* 2.4G/5G 同名去重 */
        for (int i = 0; i < cnt; i++)
            if (strcmp(list[i], decoded) == 0) { dup = 1; break; }
        if (dup || cnt >= max) continue;

        snprintf(list[cnt], 64, "%s", decoded);
        cnt++;
    }
    pclose(fp);
    return cnt;
}

/* ---------- 连接: 后台线程调用, 会阻塞十几秒 ---------- */
int wifi_connect(const char *ssid, const char *psk)
{
    char s_ssid[64], s_psk[64];
    snprintf(s_ssid, sizeof(s_ssid), "%s", ssid ? ssid : "");
    snprintf(s_psk,  sizeof(s_psk),  "%s",  psk  ? psk  : "");

    /* 第三道闸: 落盘前无论如何再剥一次, 保证引号内绝无换行 */
    trim_tail(s_ssid);
    trim_tail(s_psk);

    if (s_ssid[0] == '\0') return -1;

    FILE *fp = fopen(WPA_CONF, "w");
    if (!fp) return -1;
    fprintf(fp,
        "ctrl_interface=/var/run/wpa_supplicant\n"
        "ap_scan=1\n"
        "update_config=1\n"
        "network={\n"
        "\tssid=\"%s\"\n"
        "\tpsk=\"%s\"\n"
        "\tkey_mgmt=WPA-PSK\n"
        "}\n",
        s_ssid, s_psk);
    fclose(fp);
    sync();

    system("killall udhcpc 2>/dev/null");
    system("killall wpa_supplicant 2>/dev/null");
    sleep(1);
    system("wpa_supplicant -B -i wlan0 -c /etc/wpa_supplicant.conf");
    sleep(4);
    system("udhcpc -i wlan0 -b 2>/dev/null");
    return 0;
}

/* ---------- 断开 ---------- */
int wifi_disconnect(void)
{
    system("killall udhcpc 2>/dev/null");
    system("killall wpa_supplicant 2>/dev/null");
    system("ifconfig wlan0 0.0.0.0 2>/dev/null");
    return 0;
}

/* ---------- 查 IP: 已关联 + 有 IP 才算连上 ---------- */
int wifi_get_ip(char *ip, int len)
{
    if (!wifi_is_associated()) {
        system("ifconfig wlan0 0.0.0.0 2>/dev/null");  /* 清残留 IP */
        return -1;
    }

    int fd = socket(AF_INET, SOCK_DGRAM, 0);
    if (fd < 0) return -1;

    struct ifreq ifr;
    memset(&ifr, 0, sizeof(ifr));
    strncpy(ifr.ifr_name, "wlan0", IFNAMSIZ - 1);

    int ret = -1;
    if (ioctl(fd, SIOCGIFADDR, &ifr) == 0) {
        char *p = inet_ntoa(((struct sockaddr_in *)&ifr.ifr_addr)->sin_addr);
        if (p && strcmp(p, "0.0.0.0") != 0) {
            snprintf(ip, len, "%s", p);
            ret = 0;
        }
    }
    close(fd);
    return ret;
}

