#!/usr/bin/env python3
"""
PC 端网络调试小工具 — 配合板子上的三个 Socket 练习程序使用 (PC 与板子连同一手机热点)

用法:
    python pc_net_tools.py chat <板子IP>   练习1: 连板子的 TCP echo(8888), 你发什么回什么
    python pc_net_tools.py sink            练习2: 监听 9000, 接收板子 tcp_client 上报的数据
    python pc_net_tools.py udp             练习3: 监听 9999, 接收板子的 UDP 广播

注意: 首次运行若弹出 Windows 防火墙提示, 请勾选"专用网络"允许。
"""
import socket
import sys
import threading


def local_ip():
    """取本机在局域网内的 IP (UDP connect 只是让内核选路由, 不会真发包)"""
    try:
        s = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
        s.connect(("8.8.8.8", 80))
        ip = s.getsockname()[0]
        s.close()
        return ip
    except OSError:
        return "127.0.0.1"


def chat(board_ip):
    """练习1: TCP 客户端连板子的 echo 服务"""
    s = socket.create_connection((board_ip, 8888), timeout=5)
    print(f"已连上板子 {board_ip}:8888  (输入回车发送, 输入 quit 断开, Ctrl+C 退出)")
    print("粘包实验: 快速连发/粘贴多行, 观察板子一次 recv 收到的合并内容\n")

    def rx():
        while True:
            try:
                data = s.recv(1024)
            except (ConnectionResetError, OSError):
                print("\n[连接异常断开]")
                return
            if not data:
                print("\n[板子关闭了连接]")
                return
            print(f"  [回显 {len(data):3d}字节] {data.decode(errors='replace')}", end="")

    threading.Thread(target=rx, daemon=True).start()
    try:
        for line in sys.stdin:
            s.sendall(line.encode())
            if line.strip() == "quit":
                break
    finally:
        s.close()
        print("\n已断开")


def sink():
    """练习2: TCP 服务端, 接收板子 tcp_client 的上报"""
    srv = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
    srv.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
    srv.bind(("0.0.0.0", 9000))
    srv.listen(1)
    print(f"本机IP: {local_ip()}   ← 板子上执行: ./tcp_client {local_ip()}")
    print("TCP sink 监听 0.0.0.0:9000, 等板子连入... (Ctrl+C 退出, 可测板子重连)\n")
    while True:
        conn, addr = srv.accept()
        print(f"[板子连入 {addr[0]}:{addr[1]}]")
        with conn:
            while True:
                data = conn.recv(1024)
                if not data:
                    print("[板子断开, 等它自动重连...]\n")
                    break
                print(f"  {data.decode(errors='replace')}", end="")


def udp_listen():
    """练习3: UDP 监听, 接收板子广播"""
    s = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
    s.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
    s.bind(("0.0.0.0", 9999))
    print(f"本机IP: {local_ip()}")
    print("UDP 监听 0.0.0.0:9999, 等板子广播... (Ctrl+C 退出)\n")
    while True:
        data, addr = s.recvfrom(2048)
        print(f"  [来自 {addr[0]}] {data.decode(errors='replace')}", end="")


def main():
    if len(sys.argv) == 3 and sys.argv[1] == "chat":
        chat(sys.argv[2])
    elif len(sys.argv) == 2 and sys.argv[1] == "sink":
        sink()
    elif len(sys.argv) == 2 and sys.argv[1] == "udp":
        udp_listen()
    else:
        print(__doc__)
        try:
            input("\n>> 这是命令行工具, 请在终端里带参数运行; 双击打开的请看完说明后按回车退出... ")
        except EOFError:
            pass


if __name__ == "__main__":
    main()
