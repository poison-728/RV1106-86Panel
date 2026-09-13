# 驱动层

`driver/` 目录封装三类板端外设访问，全部走 Linux 标准 `/dev` 接口，不依赖任何厂商库。

## AHT20 温湿度（aht20.c/h）

I2C 协议读写，设备 `/dev/i2c-2`，地址 0x38：

| 步骤 | 命令 | 说明 |
|---|---|---|
| 1 | 写 `0x71`（状态字） | 校验 bit3，忙位 bit7 |
| 2 | 写 `0xAC 0x33 0x00` | 触发测量 |
| 3 | 等 80ms | 数据就绪 |
| 4 | 读 7 字节 | 状态 + 温度20bit + 湿度20bit + CRC |

换算（`aht20_read()` 内）：

```c
hum  = (raw_h >> 12) * 100.0 / 1048576.0;   /* 2^20 */
temp = ((raw & 0xFFFFF) >> 12) * 200.0 / 1048576.0 - 50.0;
```

要点：

- 打开设备后 `ioctl(I2C_SLAVE, 0x38)` 锁定从机地址
- 连续失败自动降级：返回错误由上层 sensor 线程处理，不阻塞 UI

## 传感器线程（sensor.c/h）

生产者-消费者模型，UI 与采集解耦：

```c
static float s_temp, s_hum;
static pthread_mutex_t s_lock;

static void *sensor_thread(void *arg)
{
    for (;;) {
        float t, h;
        if (aht20_read(&t, &h) == 0) {           /* 真实传感器 */
            pthread_mutex_lock(&s_lock);
            s_temp = t; s_hum = h;
            pthread_mutex_unlock(&s_lock);
        }
        usleep(500 * 1000);                       /* 500ms 采样 */
    }
}

int sensor_get(float *temp, float *hum)           /* UI 线程调用 */
```

- `sensor_init()`：起线程；AHT20 打不开时走**模拟数据**（正弦波模拟温湿度漂移），保证无硬件也能开发 UI
- `sensor_get()`：带锁快照拷贝，绝不在锁内做任何 UI 操作

UI 侧在 `main.c` 注册 `lv_timer`（500ms）调 `sensor_get()` → 更新 Home/Climate 两页数值并喂给历史曲线。

## WiFi（wifi.c/h）

纯 Linux 工具链方案（wpa_supplicant + iw + udhcpc），`popen()` 拉起：

### 扫描（wifi_scan）

```c
popen("iw dev wlan0 scan", "r")
```

解析每行 `SSID: xxx`，两个坑：

1. **中文/非 ASCII SSID 被 iw 转义成 `\xNN`**——逐段解码回 UTF-8
2. **行尾带 `\n`**——必须剥掉，否则点扫描项回填输入框时混入换行，后续写 conf 会把引号断行，wpa_supplicant 直接解析退出

### 连接（wifi_connect）

```c
/* 后台线程执行, UI 只轮询状态——回调里跑 popen 会卡死界面 */
system("killall wpa_supplicant");        /* 先杀系统服务防多实例互踢 */
/* 写 /etc/wpa_supplicant.conf: UTF-8 无 BOM! 中文 SSID 带 BOM 认证失败 */
system("wpa_supplicant -B -i wlan0 -c /etc/wpa_supplicant.conf");
system("udhcpc -i wlan0 -q");
```

### 状态判定（wifi_get_ip）

"有 IP" ≠ "连上了"：关联已断但 IP 残留会误报在线。判定必须双条件：

```c
int wifi_online(void)
{
    /* 条件1: iw link 有关联 */
    /* 条件2: ioctl SIOCGIFADDR 有合法 IP (非 0.0.0.0) */
    return associated && has_ip;
}
```

UI 显示再叠一层**三次消抖**（连续 3 次一致才切换显示），避免信号边界抖动导致卡片颜色闪烁。

## 触摸与显示

不在 driver/ 里，由 LVGL 驱动库直接提供（`lv_drv_conf.h` 中 `USE_FBDEV=1`、`USE_EVDEV=1`）：

- 显示：`lv_drivers/display/fbdev.c` → `/dev/fb0`，32bpp
- 触摸：`lv_drivers/indev/evdev.c` → 自动探测 `/dev/input/event*`，上报坐标即屏幕坐标

## 音频（application/music.c，非 driver/）

板载 RV1106 codec 直推喇叭，同样走系统工具：

```c
popen("aplay /userdata/music/xxx.wav &", "r");     /* 播放 */
system("amixer set 'DAC LINEOUT' 50%");             /* 音量 */
system("killall aplay");                            /* 停止/切歌 */
```

注意 aplay 只吃 WAV/PCM——MP3 字节流直接播是"炸麦"白噪音，PC 端先 `ffmpeg` 转 `pcm_s16le`。amixer 控制名带空格要加引号。
