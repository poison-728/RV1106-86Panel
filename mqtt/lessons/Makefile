# Socket 练习独立编译 — 与主程序 demo_board 互不干扰
# 在 Ubuntu 构建环境(工具链在 PATH)下执行: make && make clean 可重编
CROSS   = arm-rockchip830-linux-uclibcgnueabihf-
CC      = $(CROSS)gcc
CFLAGS  = -Wall -O2 -static

BINS = tcp_echo tcp_client udp_bcast mqtt_test

all: $(BINS)

tcp_echo: tcp_echo.c
	$(CC) $(CFLAGS) -o $@ $<

tcp_client: tcp_client.c
	$(CC) $(CFLAGS) -o $@ $<

udp_bcast: udp_bcast.c
	$(CC) $(CFLAGS) -o $@ $<

mqtt_test: mqtt_test.c mqtt.c mqtt.h
	$(CC) $(CFLAGS) -o $@ mqtt_test.c mqtt.c -lpthread

clean:
	rm -f $(BINS)

.PHONY: all clean
