# Socket+MQTT 从零到手机控灯 — 实现笔记规划

## 定位
- 类型：技术复盘教程（延续 rs485-modbus-notes 系列），面向小白可复现
- 语言：中文
- 结论先行：零依赖手写 MQTT 客户端接入 86 盒智能家居，实现手机/PC/屏幕三遥控器控灯

## 视觉决策
Color preset: Business Blue — cue: 网络协议/TCP/MQTT 技术主题，蓝色调系列延续工程笔记风格
Intro mode: contained — cue: 技术教程默认无图开头

## 图表规划
- Mermaid flowchart ×3：三遥控器总架构 / MQTT 下发-上报数据流 / mqtt.c 线程模型
- Mermaid sequenceDiagram ×2：TCP 三次握手+四次挥手 / MQTT 会话建立与消息交互
- 报文字节图：HTML 表格呈现（CONNECT/PUBLISH/SUBSCRIBE/PINGREQ）
- 无定量图表，不引入 ECharts

## 章节结构（10 章）
0. 全景：一句话成果 + 总架构图 + 里程碑路线（M1→M2→M3）
1. 概念课 A：Socket 基础（TCP vs UDP、五件套 API、握手挥手、粘包、TIME_WAIT）
2. 概念课 B：MQTT 协议（broker/topic/pub-sub/QoS/retain/keepalive、6 种报文、变长编码）
3. 实施一 M1：socket-lessons 三个练习（tcp_echo/tcp_client/udp_bcast + pc_net_tools.py）
4. 实施二：mosquitto 环境搭建（安装/conf 两行/防火墙/判定树）
5. 实施三 M2：手写 MQTT 四阶段（CONNECT→PUBLISH→SUBSCRIBE→心跳重连线程化）
6. 实施四 M3：集成进 demo_board（搬家/main.c/边沿上报/cfg IP/手机 App）
7. 文件清单总表（新建+修改+作用）
8. 踩坑实录（12+ 个真实坑：症状→根因→解法）
9. 验收清单 + 术语速查 + 常用命令

## 素材
- 代码：socket-lessons/*.c、mqtt.c/h、mqtt_test.c、ui_ScreenControl.c 改动、main.c 改动、cfg_get_str
- 复用 rs485-modbus-notes 的 assets/code-highlight.js（C+Python 轻量高亮）
- _shared/js/mermaid.min.js 本地化
