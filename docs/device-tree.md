# 设备树与 SDK 适配

SDK：Luckfox Pico Ultra 官方 SDK（`sysdrv/source/kernel` 为 5.10 内核树）。

本仓库 `sdk/` 目录收录了全部 SDK 侧改动：

| 文件 | 说明 |
|---|---|
| `86panel_sdk.patch` | git diff 完整补丁（可 `git apply` 复现） |
| `rv1106-luckfox-pico-ultra-ipc.dtsi` | 改后全文（屏幕 + 触摸 + I2C2） |
| `rv1106g-luckfox-pico-ultra.dts` | 改后全文（UART2/RS485） |
| `luckfox_rv1106_linux_defconfig` | 改后全文（内核配置） |
| `Makefile.focaltech_touch` | FocalTech 触摸驱动 Makefile（编译修复） |

## 1. 屏幕点亮（ipc.dtsi）

默认 SDK 的 panel 节点缺背光和复位，且像素时钟极性不匹配：

```dts
panel: panel {
    compatible = "simple-panel";
    backlight = <&backlight>;                    /* + 挂背光 */

    reset-gpios = <&gpio0 RK_PA1 GPIO_ACTIVE_LOW>;  /* + 复位脚 */
    reset-delay-ms = <200>;                         /* + 复位等待 */
    status = "okay";

    bus-format = <MEDIA_BUS_FMT_RGB666_1X18>;
    ...
    port { ... timing {
        ...
        pixelclk-active = <1>;                    /* 0 → 1, 不翻白屏的关键 */
    }};
};
```

`pixelclk-active` 极性错误时屏幕背光亮但显示异常——这类"点不亮屏"问题先查时序极性三件套（hsync/vsync/pixelclk）。

## 2. FT6636U 触摸适配（ipc.dtsi）

板子默认按 GT911 适配，实际触摸 IC 为 FT6636U（FocalTech），I2C 地址 0x48：

```dts
&i2c3 {
    status = "okay";
    clock-frequency = <100000>;
    ft663u:touchscreen@48 {
        compatible = "focaltech,fts";
        reg = <0x48>;

        interrupt-parent = <&gpio0>;
        interrupts = <RK_PA3 IRQ_TYPE_LEVEL_LOW>;   /* 电平触发, 非边沿 */

        focaltech,irq-gpio = <&gpio0 RK_PA3 GPIO_ACTIVE_HIGH>;
        focaltech,reset-gpio = <&gpio0 RK_PA4 GPIO_ACTIVE_LOW>;
        focaltech,max-touch-number = <1>;
        focaltech,display-coords = <0 0 720 720>;   /* 与屏幕分辨率一致 */
        status = "okay";
    };
};
```

注意触摸挂在 **I2C3**（GPIO3_D1/D2），I2C3 被触摸占用后不可再挂 AHT20——传感器移到 I2C2。

### 配套内核配置（defconfig）

```diff
-CONFIG_TOUCHSCREEN_GOODIX=y
+CONFIG_TOUCHSCREEN_FTS=y          # 切换到 FocalTech 驱动
-CONFIG_BACKLIGHT_PWM=m
+CONFIG_BACKLIGHT_PWM=y            # 背光内建, 否则启动早期无背光
```

### 触摸驱动编译修复

`drivers/input/touchscreen/focaltech_touch/Makefile` 默认会编 `focaltech_test/` 子目录，该目录缺依赖编不过：

```diff
-focaltech-ts-y += focaltech_test/
+# focaltech-ts-y += focaltech_test/
```

适配完验证：`/dev/input/event*` 存在，`evtest`（或 `cat /dev/input/event0`）按下有事件输出，`hexdump` 可见坐标。

## 3. RS485 串口（ultra.dts）

MAX13487 自动收发方向，无需 GPIO 方向控制，只需使能 UART2 M1 引脚：

```dts
/**********RS485**********/
&uart2 {
    status = "okay";
    pinctrl-names = "default";
    pinctrl-0 = <&uart2m1_xfer>;
};
```

生效后出现 `/dev/ttyS2`。板载蓝牙占 UART1_M0（/dev/ttyS1），RS485 必须走 UART2_M1。

## 4. AHT20 传感器总线（ipc.dtsi）

```dts
&i2c2 {
    status = "okay";
    clock-frequency = <100000>;
    pinctrl-0 = <&i2c2m0_xfer>;
};
```

引脚：I2C2_M0_SCL (GPIO1_A0) / I2C2_M0_SDA (GPIO1_A1)。

验证：`i2cdetect -y 2` 应看到 `38`。

## 应用补丁

```bash
cd <你的SDK根目录>
git apply sdk/86panel_sdk.patch
# 或逐文件覆盖后重编内核+固件, 用 rkdeveloptool/SYD 分区烧录 update.img
```

patch 中另外三个二进制差异（hostapd / librkwifibt.so）是 WiFi 模组固件编译产物，与本项目逻辑无关，可不应用。
