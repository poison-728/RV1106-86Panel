/*
 * 练习2: 板子作为 TCP 客户端 — 主动连接 PC, 每 2 秒上报一条温湿度(模拟数据)
 *
 * 教学点 (客户端三件套 API):
 *   socket()   申请"电话机"
 *   connect()  拨号到 PC 的 IP:端口 (成功 = 三次握手已在内核完成)
 *   send()     说话
 *
 * 板端运行:  ./tcp_client <PC的IP>      例: ./tcp_client 192.168.43.100
 * PC 端:    python pc_net_tools.py sink  (先启动, 它会打印 PC 的 IP)
 *
 * 观察实验:
 *   1. 先杀 sink (Ctrl+C)      → 板子几秒内 send 报错, 自动重连 (体验 TCP 双向管道)
 *   2. 再把 sink 重新拉起来    → 板子自动连回来, 日志全程可见
 *   3. PC 不在网/还没连热点    → connect 报 "No route to host" 或卡住(alarm 5秒强断), 3 秒重试
 *   外层 while = 断线重连骨架, M2 的 MQTT 客户端就是把这个循环抄过去加协议
 */
#include <stdio.h>
#include <string.h>
#include <errno.h>
#include <unistd.h>
#include <signal.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>

#define SRV_PORT 9000
#define BUF_SIZE 256

static volatile sig_atomic_t g_alrm = 0;

static void on_alarm(int sig)
{
    (void)sig;
    g_alrm = 1;          /* 信号处理函数里只做标记, 不打印不做复杂事 */
}

int main(int argc, char *argv[])
{
    if (argc < 2) {
        printf("用法: %s <PC的IP>\n例:   %s 192.168.43.100\n", argv[0], argv[0]);
        return 1;
    }
    const char *srv_ip = argv[1];

    setvbuf(stdout, NULL, _IOLBF, 0);
    signal(SIGPIPE, SIG_IGN);     /* 对端关闭后再 send → SIGPIPE, 忽略, 看返回值 */
    signal(SIGALRM, on_alarm);

    int seq = 0;
    while (1) {                   /* ============ 外层大循环: 断线重连 ============ */
        int fd = socket(AF_INET, SOCK_STREAM, 0);
        if (fd < 0) { perror("socket"); return 1; }

        struct sockaddr_in addr;
        memset(&addr, 0, sizeof(addr));
        addr.sin_family      = AF_INET;
        addr.sin_port        = htons(SRV_PORT);
        addr.sin_addr.s_addr = inet_addr(srv_ip);

        printf("[cli] 正在连接 %s:%d ...\n", srv_ip, SRV_PORT);
        g_alrm = 0;
        alarm(5);                 /* PC 彻底离线时 connect 内核要重试 ~2 分钟;
                                     alarm 让它 5 秒内必须返回, 超时按失败处理 */
        int r = connect(fd, (struct sockaddr *)&addr, sizeof(addr));
        alarm(0);
        if (r < 0) {
            if (g_alrm) printf("[cli] connect 5 秒超时 (PC 不在线?), 3 秒后重试...\n");
            else        printf("[cli] connect 失败(%s), 3 秒后重试...\n", strerror(errno));
            close(fd);
            sleep(3);
            continue;
        }
        printf("[cli] 已连上!\n");

        while (1) {               /* ======== 内层循环: 连上后周期上报 ======== */
            char buf[BUF_SIZE];
            float temp = 25.0f + (seq % 20) * 0.1f;   /* 模拟数据, M2 换真 AHT20 */
            float hum  = 55.0f + (seq % 10) * 0.5f;
            int n = snprintf(buf, sizeof(buf), "seq=%d temp=%.1f hum=%.1f\n",
                             seq, temp, hum);
            if (send(fd, buf, n, 0) < 0) {
                printf("[cli] send 失败(%s) → 对面挂了, 3 秒后重连...\n", strerror(errno));
                break;
            }
            seq++;
            sleep(2);
        }
        close(fd);                /* 报废旧连接, 回外层循环重建 */
    }
    return 0;
}
