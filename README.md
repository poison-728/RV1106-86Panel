# RV1106-86Panel

基于 **Luckfox Pico Ultra (RV1106)** 的 86 盒智能家居控制面板：从 SDK 编译、触摸屏 bring-up、LVGL UI，到 RS485/Modbus 本地控制和 MQTT 远程控制，完整走通一块嵌入式智能面板的全栈开发。

![language](https://img.shields.io/badge/language-C-blue) ![platform](https://img.shields.io/badge/platform-Linux%20%2B%20RV1106-orange) ![ui](https://img.shields.io/badge/UI-LVGL%208.3-green) ![bus](https://img.shields.io/badge/bus-RS485%20%2F%20Modbus%20RTU-critical) ![net](https://img.shields.io/badge/net-MQTT%20(手写客户端)-9cf)

> 照片位：86 盒正面运行图（拍照后放 `docs/images/hero.jpg` 并取消本行注释）
>
> <!-- ![86Panel](docs/images/hero.jpg) -->

## 功能特性

- **720×720 电容触摸屏 UI**：LVGL 8.3 + fbdev 显示 + evdev 触摸，五个页面（首页 / 气候 / 音乐 / 网络 / 设备控制）
- **环境感知**：AHT20 温湿度传感器（I2C），实时显示 + 历史曲线（lv_chart）+ 传感器线程与 UI 线程解耦
- **本地控制**：RS485 + Modbus RTU 主机，轮询 STM32 从机控制灯光 / 窗帘 / 空调，7 寄存器约定
- **远程控制**：从零手写 MQTT 客户端（不依赖任何 MQTT 库），手机 App 一键控灯，状态双向同步
- **音频播放**：RV1106 板载 codec 直推喇叭，WAV 曲库播放 + 音量控制
- **联网能力**：屏上 WiFi 扫描配网（支持中文 SSID）、NTP 对时、断线自动重连
- **掉电恢复**：key=value 配置持久化（tmp+rename 原子写），重启后恢复音量 / 设备状态 / 设定温度
- **开机自启**：init.d 服务，上电即进入面板界面

## 系统架构图

```mermaid
flowchart LR
    subgraph 远程["远程入口"]
        APP["手机 App<br/>(IoT MQTT Panel)"]
    end

    subgraph PC["PC 侧"]
        BR["MQTT Broker<br/>(mosquitto)"]
    end

    subgraph panel["86 盒 · Luckfox Pico Ultra (RV1106)"]
        subgraph threads["多线程应用 (demo_board)"]
            UI["UI 线程<br/>LVGL 主循环 + lv_timer"]
            MQTT_T["MQTT 线程<br/>手写客户端: 收发/心跳/重连"]
            MB_T["Modbus 线程<br/>500ms 轮询 03读/06写"]
            SEN_T["Sensor 线程<br/>AHT20 采集"]
            CFG[("cfg 持久化<br/>/userdata/smarthome.cfg")]
        end
        LCD["720×720 RGB 屏<br/>fbdev"]
        TS["FT6636U 触摸<br/>evdev (I2C3)"]
        CODEC["RV1106 codec<br/>aplay/amixer"]
    end

    subgraph slave["STM32F103 从机"]
        RELAY["继电器×2 + 窗帘 + 空调"]
    end

    APP -- "home/&lt;dev&gt;/set" --> BR --> MQTT_T
    MQTT_T -- "state 上报 (retain)" --> BR --> APP
    TS --> UI
    UI --> LCD
    UI --> CODEC
    SEN_T -- "温湿度" --> UI
    MQTT_T -- "期望值登记" --> MB_T
    UI -- "点击卡片" --> MB_T
    MB_T == "RS485 / Modbus RTU" ==> RELAY
    RELAY -- "回读真值" ==> MB_T
    MB_T -- "回读→刷UI→触发上报" --> UI
    UI -- 保存/恢复 --> CFG
```

三种控制入口（触摸屏 / PC 命令行 / 手机 App）在 Modbus 期望值登记处汇合成**同一条下发链路**；从机回读真值统一刷新 UI 并触发 MQTT 状态上报，天然防回环。

详细设计：[docs/architecture.md](docs/architecture.md)

## 目录结构

```
RV1106-86Panel/
├── main.c                 # 应用入口: 初始化顺序 + 主循环
├── Makefile.board         # 交叉编译脚本 (ARM 工具链)
├── lv_conf.h              # LVGL 配置 (32bpp / 256KB 内存池)
├── lv_drv_conf.h          # 显示/输入驱动配置 (fbdev + evdev)
├── lvgl/                  # LVGL 8.3.11 本体 (已裁剪 demos/examples/tests)
├── lv_drivers/            # LVGL 官方驱动库
├── application/           # 应用层: UI 页面 / 配置持久化 / 音乐
│   ├── screens/           #   五个屏幕实现
│   └── fonts/             #   中文字体子集 lv_font_zh_32
├── driver/                # 设备驱动封装: AHT20 / 传感器线程 / WiFi
├── modbus/                # Modbus RTU 主机协议栈 + STM32 从机示例
├── mqtt/                  # 手写 MQTT 客户端
│   └── lessons/           #   Socket/MQTT 学习练习 (独立可编译)
├── sdk/                   # SDK 侧修改: 设备树 / defconfig / 补丁
└── docs/                  # 文档 (架构/设备树/驱动/Modbus/MQTT)
    └── notes/             #   两份全程开发复盘笔记 (HTML, 可离线阅读)
```

## 硬件清单

| 器件 | 型号 / 说明 | 接到 86 盒 |
|---|---|---|
| 主控板 | Luckfox Pico Ultra (RV1106, 720×720 屏) | — |
| 触摸屏 | FT6636U 电容触摸，I2C 地址 0x48 | I2C3, 中断 GPIO0_A3 |
| 温湿度传感器 | AHT20, I2C 地址 0x38 | I2C2_M0 (GPIO1_A0/A1) |
| RS485 收发 ×2 | MAX13487 (自动收发方向) | UART2_M1 → /dev/ttyS2 |
| 从机 | STM32F103C8T6 + 继电器 | RS485 A/B 双绞 |
| 调试用 | USB 转 RS485 模块 | PC 端无硬件联调 |
| 喇叭 | 板载 codec 直推 (SPK 2pin) | — |

## 快速开始

### 编译

```bash
# Ubuntu, Luckfox SDK 自带交叉工具链
make -f Makefile.board TOOLCHAIN=/path/to/arm-rockchip830-linux-uclibcgnueabihf-gcc
# 产物: ./demo_board (静态链接)
```

> SDK 侧（设备树使能 UART2_M1/I2C2、FT6636U 触摸驱动）需要先打上 `sdk/86panel_sdk.patch` 并重新编译烧录内核，详见 [docs/device-tree.md](docs/device-tree.md)。

### 运行

```bash
adb push demo_board /userdata/
adb shell chmod +x /userdata/demo_board
adb shell /userdata/demo_board        # 前台运行观察日志
# 或安装开机自启:
adb push application/S99demo /etc/init.d/
adb shell chmod +x /etc/init.d/S99demo
```

无外设也能跑：AHT20 / Modbus / MQTT 任一初始化失败时自动降级（模拟数据 / 设备离线 / 不联网），UI 不崩。

## LVGL UI

> 照片位：五个页面截图（拍照后放 `docs/images/ui-*.jpg`）

| 页面 | 内容 |
|---|---|
| **Home** | 时钟、温湿度卡片、音乐快捷控制、网络状态卡片 |
| **Climate** | 温湿度实时值 + 历史曲线（lv_chart）、温度设定滑条、空调四模式 |
| **Music** | 曲库（/userdata/music/*.wav）、播放控制、音量滑条 |
| **Network** | WiFi 扫描列表、SSID/密码输入、一键连接（支持中文 SSID） |
| **Conctrl** | 五张设备卡片（客厅灯/卧室灯/客厅窗帘/卧室窗帘/空调），点击即控 |

UI 细节：中文字体子集（按需生成，避免内置 simsun 缺字）、深色主题统一配色、卡片点击态变色、输入框光标聚焦显隐。

## DTS / 驱动适配

板级 bring-up 完成了屏背光 / 触摸 / 串口 / I2C 四项适配，SDK 侧改动见 `sdk/`（含完整 patch）：

| 改动 | 内容 |
|---|---|
| 屏幕点亮 | panel 节点挂 backlight + reset-gpios(GPIO0_A1)，pixelclk 极性翻转 |
| FT6636U 触摸 | I2C3 节点 GT911→FT6636U 替换（0x48 / 电平中断 / focaltech 私有属性） |
| RS485 串口 | uart2 M1 引脚使能 → /dev/ttyS2 |
| AHT20 总线 | i2c2 使能 100kHz |
| 内核配置 | CONFIG_TOUCHSCREEN_FTS=y、CONFIG_BACKLIGHT_PWM=y |

详解：[docs/device-tree.md](docs/device-tree.md) · [docs/driver.md](docs/driver.md)

## RS485 / Modbus 通信

主机协议栈纯手写：CRC16 查表校验、T3.5 帧超时切帧、03 读 / 06 写功能码、后台线程 500ms 轮询（3 次无响应判离线）、互斥锁保护状态。

**寄存器约定**（主从一致）：

| 地址 | 含义 | 读写 |
|---|---|---|
| 0x0000 | 空调开关 | RW |
| 0x0001 | 温度 ×10 | RO |
| 0x0002 | 湿度 ×10 | RO |
| 0x0003 / 0x0004 | 客厅灯 / 卧室灯 | RW |
| 0x0005 / 0x0006 | 客厅窗帘 / 卧室窗帘 | RW |

交互采用**乐观更新**：点卡片本地立即变色，Modbus 线程下轮下发；从机应答后以回读真值覆盖。详解：[docs/modbus.md](docs/modbus.md)

## MQTT 数据流

不依赖 paho 等库，从 TCP 裸 socket 起手写完整 MQTT 3.1.1 客户端：CONNECT/CONNACK、SUBSCRIBE/SUBACK、PUBLISH（QoS0）、PINGREQ/PINGRESP 心跳、变长编码、断线重连。

| 主题 | 方向 | 说明 |
|---|---|---|
| `home/<dev>/set` | App → 板 | 下发命令（订阅 `home/+/set`） |
| `home/<dev>/state` | 板 → App | 状态上报，retain=1，App 重连可补状态 |
| `home/86panel/temp` `/hum` | 板 → App | 温湿度上报 |

状态上报**边沿触发**（值变化才发）；MQTT 离线→在线上升沿时全景 retain 补报。详解：[docs/mqtt.md](docs/mqtt.md)

## 典型 Bug 与解决方法

开发全程踩坑 40+，完整记录见 `docs/notes/`，精选：

| 症状 | 根因 | 解法 |
|---|---|---|
| UI 随机白屏/崩溃 | 事件回调里删当前对象 / 引用已销毁局部变量 | `lv_obj_del_async()`；控件提升为文件级 static 指针 |
| 时钟 8↔16 点来回跳 | S99demo 自启 + 手动跑，双实例抢 framebuffer | 排查前先停服务：`/etc/init.d/S99demo stop` |
| WiFi 显示假连接 | 只查 IP，关联已断但 IP 残留 | `iw link` 关联 + IP 双条件，UI 三次消抖 |
| 中文 SSID 连不上 | wpa_supplicant.conf 带 BOM / SSID 混入换行符 | UTF-8 无 BOM + 三层剥换行 |
| 板子连 broker 干挂 2 分钟 | 手机热点 DHCP 漂移，SYN 被静默丢弃 | connect 非阻塞 + select 限 5s |
| App 一连板子就被踢 | MQTT client_id 撞号，broker 踢旧连接 | App 端 client ID 留空 |
| LVGL 内存崩溃 | LV_MEM_SIZE 不足 | 256KB 内存池 |
| 新增中文显示方框 | 字体子集没收录新字 | 补字符集重新生成 lv_font_zh_32 |

## 开发时间线

| 阶段 | 内容 |
|---|---|
| 板级 bring-up | SDK 完整编译、烧录点屏、FT6636U 触摸设备树与驱动适配 |
| LVGL 移植 | 工程骨架、交叉编译、fbdev/evdev 跑通、中文字体子集 |
| 五页 UI | Tab 导航 + 各页实现 + 深色主题打磨 |
| 多线程传感器 | 传感器线程 + mutex 邮箱 + lv_timer 刷新，AHT20 实测 |
| 网络 | WiFi 配网全链路、状态可靠判定、NTP 对时、开机自启 |
| 音频/持久化 | codec 播放、历史曲线、cfg 原子持久化 |
| RS485/Modbus | 主从协议栈、设备卡片、无硬件联调、真机闭环 |
| Socket/MQTT | TCP/UDP 练习 → 手写 MQTT → 板端集成 → 手机远程闭环 |

## 文档索引

| 文档 | 内容 |
|---|---|
| [docs/architecture.md](docs/architecture.md) | 系统架构：线程模型、数据流、初始化顺序 |
| [docs/device-tree.md](docs/device-tree.md) | SDK 侧：DTS 修改、defconfig、触摸驱动适配 |
| [docs/driver.md](docs/driver.md) | 驱动层：AHT20、传感器线程、WiFi |
| [docs/modbus.md](docs/modbus.md) | Modbus RTU：协议栈实现、寄存器表、从机代码 |
| [docs/mqtt.md](docs/mqtt.md) | MQTT：报文实现、主题约定、远程下发链路 |
| [docs/roadmap.md](docs/roadmap.md) | 后续规划 |
| docs/notes/*.html | RS485/Modbus 与 Socket/MQTT 全程复盘笔记 |

## License

本项目代码采用 MIT License（见 [LICENSE](LICENSE)）；`lvgl/` 与 `lv_drivers/` 为 LVGL 官方 MIT 开源库。
