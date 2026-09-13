# Roadmap

当前状态：本地控制（触摸 + Modbus）与远程控制（MQTT + 手机 App）双向闭环已全部实物验收。候选方向：

## OTA 升级

- 基于 Luckfox SDK 的 update.img 分区升级机制做整机 OTA
- 应用层先行：`demo_board` + 配置打 tar，MQTT 下发升级命令 → 下载 → md5 校验 → 替换 → 重启
- 配合版本号上报（`home/86panel/state` 加 `fw` 字段）

## 数据上云

- 温湿度历史落 SQLite（板上 `/userdata/db`），解决 lv_chart 只有内存窗口的问题
- 接 EMQX Cloud / 自建公网 broker，脱离"手机热点局域网"限制
- 简单 Web 曲线页（PC / 手机浏览器直接看历史）

## 交互升级

- lv_语音播报（codec 已就绪，差一个 TTS 源或预录音频包）
- 息屏策略 + 亮屏背光渐变（backlight PWM 已启用）
- 多语言字体子集工具化（当前新增页面中文要手工补字符集重新生成）

## 生态接入

- Home Assistant：MQTT discovery 报文（`homeassistant/` 主题），自动发现设备
- ESPHome 风格的局域网 mDNS 发现

## 兄弟项目：Linux QT CAN 上位机

规划中的下一个项目：PC 端 QT + SocketCAN，复用本项目练熟的线程模型、协议栈设计（CRC/切帧/超时）、日志与调试套路。
