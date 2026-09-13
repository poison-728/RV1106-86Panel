/*
 * 练习3: UDP 广播 — 板子把温湿度(模拟)直接广播给整个热点局域网
 *
 * 教学点 (TCP vs UDP 差异的活体对照):
 *   - UDP 没有 connect/listen/accept/挥手, 一个 sendto 就完事 — "发电报"而非"打电话"
 *   - 不可靠: 只管扔出去, 不确认、可能丢、不管对方在不在 — 但开销极小
 *   - 广播 255.255.255.255 = 局域网喊一嗓子, 谁在听谁收到;
 *     广播要先 setsockopt(SO_BROADCAST) 拿"授权", 否则 sendto 报权限错误
 *
 * 板端运行:  ./udp_bcast              广播模式 (发给 255.255.255.255:9999)
 *            ./udp_bcast <PC的IP>     点对点模式 (热点若拦广播, 用这个兜底)
 * PC 端:    python pc_net_tools.py udp  (监听 0.0.0.0:9999)
 *
 * 观察实验:
 *   1. 和练习2同时跑, 对比两者代码量/行为 — 这就是"面向连接 vs 无连接"
 *   2. PC 端 Ctrl+C 再重启 → 板子完全无感知, 消息照样发 (没有连接概念)
 *   3. TCP 版对面挂了要重连, UDP 版根本不存在"挂" — 各有适用场景
 */
#include <stdio.h>
#include <string.h>
#include <errno.h>
#include <unistd.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>

#define UDP_PORT 9999

int main(int argc, char *argv[])
{
    const char *dst = (argc > 1) ? argv[1] : "255.255.255.255";

    setvbuf(stdout, NULL, _IOLBF, 0);

    int fd = socket(AF_INET, SOCK_DGRAM, 0);      /* SOCK_DGRAM = 数据报 = UDP */
    if (fd < 0) { perror("socket"); return 1; }

    if (strcmp(dst, "255.255.255.255") == 0) {
        int opt = 1;
        if (setsockopt(fd, SOL_SOCKET, SO_BROADCAST, &opt, sizeof(opt)) < 0) {
            perror("SO_BROADCAST");               /* 广播授权 */
            return 1;
        }
    }

    struct sockaddr_in addr;
    memset(&addr, 0, sizeof(addr));
    addr.sin_family      = AF_INET;
    addr.sin_port        = htons(UDP_PORT);
    addr.sin_addr.s_addr = inet_addr(dst);

    printf("[udp] 目标 %s:%d, 每 2 秒发一帧\n", dst, UDP_PORT);
    for (int seq = 0; ; seq++) {
        char buf[128];
        float temp = 25.0f + (seq % 20) * 0.1f;
        int n = snprintf(buf, sizeof(buf), "seq=%d temp=%.1f\n", seq, temp);
        if (sendto(fd, buf, n, 0, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
            perror("sendto");                     /* UDP 几乎不会失败, 失败也继续下一轮 */
        } else {
            printf("[udp] 已发: %s", buf);
        }
        sleep(2);
    }
    return 0;
}
