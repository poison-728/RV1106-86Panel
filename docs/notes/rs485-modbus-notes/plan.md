# Plan — RS485+Modbus 从零到落地实现笔记

- Type: 中文技术教程/工程实现笔记（teaching report）
- Audience: 无 RS485/Modbus 基础的小白 + 作者本人复习
- Decision: 看懂概念（RS485/Modbus RTU/帧/CRC），并能按步骤复现整条链路
- Main conclusion (Intro 可见): 两根信号线 + 一张寄存器约定表 + 三个自写文件（modbus.c/h、ui_ScreenControl.c、stm32_modbus_slave.c），就把 86 盒 UI 的"假开关"变成跨设备的真实控制；全程不用任何 Modbus 库。

Color preset: Business Blue — cue: 工程技术文档，冷色正式，与板载 UI 蓝色 accent（#2F6FED）同族
Intro mode: contained — cue: 技术教程、无 Hero 素材、正文导向

## Sections (top-down: 宏观 → 细节, 符合学习者偏好)
0. Intro (h1 + 结论摘要 + 元数据)
1. 全景：从假开关到真控制（总架构图 + 五步路线图）
2. 概念课：RS485 与 Modbus RTU（类比讲解 + 帧格式字节图 + 主从问答时序图）
3. Step1 定协议：7 寄存器约定表 + 通信参数
4. Step2 接硬件：清单 + UART1_M1 启用 + 接线表 + 接线图
5. Step3 写主机：软件线程架构图 + modbus.h 逐行 + modbus.c 分块解析 + 三数组设计
6. Step4 接 UI：ui.c 加 tab + ui_ScreenControl.c + Home 空调卡片 + cfg 持久化
7. Step5 联调（无硬件）：Python 模拟从机（全文）+ 串口通路验证
8. Step6 STM32 真从机：CubeMX 配置 + stm32_modbus_slave.c 解析
9. 踩坑实录：症状 → 根因 → 解法 表
10. 验收清单 + 术语速查表 + 帧速查

## Visuals (all HTML/CSS, no runtime libs)
- 总架构图: CSS 分层泳道
- Modbus 06/03 帧格式: CSS 字节格子 (逐字节标注, 真实 CRC 已用脚本算出)
- 主从问答时序: CSS 双泳道
- 接线图: CSS 引脚排布图
- 代码块: 深色 pre + 本地 assets/code-highlight.js 轻量高亮 (C/Python)

## Evidence
- 全部代码来自本项目实际文件: modbus.c/modbus.h/ui_ScreenControl.c/stm32_modbus_slave.c/S99demo/cfg.c
- 示例帧 CRC 用 PowerShell 脚本实算: 01 06 00 00 00 01 48 0A / 01 03 00 00 00 07 04 08
- 引用: Modbus 官方规范 PDF (modbus.org), MAX13487 datasheet (analog.com)

## QA boundary
- 静态 QA: 结构/引用/相对路径/组件规则自查; 不做浏览器渲染验证
