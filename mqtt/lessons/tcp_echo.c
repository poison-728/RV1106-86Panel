/*
 * 练习1: 板子作为 TCP 服务端 — PC 连上来, 发什么就回什么 (echo 回显)
 *
 * 教学点 (服务端五件套 API):
 *   socket()  向内核申请一个"电话机"
 *   bind()    把电话机绑定到 IP:端口 (8888), 别人才能拨这个号
 *   listen()  标记为服务端, 开始等待来电 (第二个参数 = 排队上限)
 *   accept()  接听一个来电, 返回一条新的"专线" fd (原 srv fd 继续接下一通)
 *   recv/send 在专线上收发数据
 *
 * 板端运行:  ./tcp_echo
 * PC 端:    python pc_net_tools.py chat <板子IP>    (IP 在 86盒 Network 页能看到)
 *
 * 观察实验:
 *   1. 粘包:   在 PC 端快速连续发多行, 看板子一次 recv() 收到的字节数 — TCP 是字节流,
 *              没有"消息边界", 3 条短消息可能被合成 1 次 recv (这就是粘包, MQTT 解析要按长度切)
 *   2. TIME_WAIT: 快速 Ctrl+C 重启本程序若报 "Address already in use",
 *              是上一条连接还在 TIME_WAIT 占着端口 — SO_REUSEADDR 就是解药 (注释见下)
 *   3. 四次挥手: 输入 quit 后看双方关闭日志
 */
#include <stdio.h>
#include <string.h>
#include <errno.h>
#include <unistd.h>
#include <signal.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>

#define ECHO_PORT 8888
#define BUF_SIZE  1024

int main(void)
{
    setvbuf(stdout, NULL, _IOLBF, 0);
    /* 对端意外崩溃时, 我们再往这条连接 send 会触发 SIGPIPE, 默认直接杀进程;
     * 忽略它, 改为通过 send 返回值感知 — 这是 TCP 服务端的标准操作 */
    signal(SIGPIPE, SIG_IGN);

    int srv = socket(AF_INET, SOCK_STREAM, 0);       /* SOCK_STREAM = 字节流 = TCP */
    if (srv < 0) { perror("socket"); return 1; }

    int opt = 1;
    /* 没有 SO_REUSEADDR: 上一条连接的 TIME_WAIT (持续 ~1-2 分钟) 会让 bind 报
     * "Address already in use", 服务端就没法秒级重启。注释掉这行试试看! */
    setsockopt(srv, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));

    struct sockaddr_in addr;
    memset(&addr, 0, sizeof(addr));
    addr.sin_family      = AF_INET;
    addr.sin_port        = htons(ECHO_PORT);   /* 网络字节序: 小端PC/大端设备都常见, htons 统一 */
    addr.sin_addr.s_addr = htonl(INADDR_ANY);  /* 0.0.0.0 = 监听本机所有网卡的 8888 */

    if (bind(srv, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
        perror("bind"); return 1;
    }
    if (listen(srv, 5) < 0) {                    /* 5 = 半连接+已完成连接的排队上限 */
        perror("listen"); return 1;
    }
    printf("[echo] 监听 0.0.0.0:%d, 等待 PC 连入 (netstat -tln 可查)...\n", ECHO_PORT);

    while (1) {
        struct sockaddr_in cli;
        socklen_t clen = sizeof(cli);
        int cfd = accept(srv, (struct sockaddr *)&cli, &clen);
        if (cfd < 0) { perror("accept"); continue; }

        printf("[echo] PC 连入: %s:%d\n", inet_ntoa(cli.sin_addr), ntohs(cli.sin_port));

        /* 一次只服务一个连接, 串行处理 (简化教学; 多客户端并发要 select/epoll,
         * 那是 M2 MQTT 单线程收发 + 定时器事件循环的伏笔) */
        char buf[BUF_SIZE + 1];
        while (1) {
            int n = recv(cfd, buf, BUF_SIZE, 0);
            if (n < 0) {
                perror("recv"); break;
            }
            if (n == 0) {                          /* recv 返回 0 = 对端优雅关闭(FIN) = 挂断 */
                printf("[echo] 对端已断开\n");
                break;
            }
            buf[n] = 0;
            /* 注意看 n: 一次 recv 拿到的字节数不一定等于对方一次 send 的量!
             * TCP 只保证字节顺序, 不保证"条数" — 这就是粘包/半包的现场 */
            printf("[recv %3d B] %s", n, buf);

            if (strncmp(buf, "quit", 4) == 0) {    /* 演示服务端主动挂断 → 四次挥手 */
                printf("[echo] 收到 quit, 主动关闭连接\n");
                break;
            }
            if (send(cfd, buf, n, 0) < 0) {        /* 回显给 PC */
                perror("send"); break;
            }
        }
        close(cfd);                                /* 关闭这条专线, 回去 accept 下一通 */
        printf("[echo] 连接已关闭, 继续等待...\n\n");
    }
    close(srv);
    return 0;
}
