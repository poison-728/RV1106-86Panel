/* stm32_modbus_slave.c — STM32F103C8T6 裸机 Modbus RTU 从机 (与 86盒 modbus.c 配套)
 *
 * ================= CubeMX 配置 (照做即可编译) =================
 * 芯片: STM32F103C8T6, 主频 72MHz (HSE 8MHz x9)
 * USART1: Mode=Asynchronous, Baud=9600, Word=8, Parity=None, Stop=1
 *         引脚 PA9=TX PA10=RX, NVIC 勾选 "USART1 global interrupt"
 * GPIO 输出 (推挽): PB0=空调 PB1=客厅灯 PB2=卧室灯 PB3=客厅窗帘 PB4=卧室窗帘
 *                   PB5=LED帧指示(收到合法帧翻转)
 * 其他全部默认。此文件替换工程里的 main.c 或 include 进 main.c。
 *
 * ================= 寄存器表 (与主机 modbus.h 严格一致) =================
 * 0x0000 空调开关     0=关 1=开       -> PB0
 * 0x0001 温度 x10     只读, 自测固定25.6C
 * 0x0002 湿度 x10     只读, 自测固定60.5%
 * 0x0003 客厅灯       0=关 1=开       -> PB1
 * 0x0004 卧室灯       0=关 1=开       -> PB2
 * 0x0005 客厅窗帘     0=关 1=开       -> PB3 (可扩展0~100开度)
 * 0x0006 卧室窗帘     0=关 1=开       -> PB4 (可扩展0~100开度)
 *
 * 帧接收: RX中断逐字节 + 5ms静默判帧(9600波特T3.5≈4ms, 取5ms)
 * 支持功能码: 03 读保持寄存器 / 06 写单寄存器
 * ================================================================
 */
#include "main.h"
#include <string.h>
#include <stdint.h>

static void SystemClock_Config(void);
static void MX_GPIO_Init(void);
static void MX_USART1_UART_Init(void);

#define SLAVE_ADDR   0x01
#define REG_NUM      7
#define FRAME_GAP_MS 5          /* 总线静默超过此值 = 一帧结束 */
#define RX_MAX       32

extern UART_HandleTypeDef huart1;

/* 保持寄存器: 初始温度25.6C 湿度60.5% */
static uint16_t holding[REG_NUM] = { 0, 256, 605, 0, 0, 0, 0 };

/* 寄存器 -> GPIO 映射 (索引0~6; 温湿度无引脚填0) */
static const struct { GPIO_TypeDef *port; uint16_t pin; } reg_gpio[REG_NUM] = {
    { GPIOB, GPIO_PIN_0 },   /* 0 空调     */
    { 0, 0 },                /* 1 温度     */
    { 0, 0 },                /* 2 湿度     */
    { GPIOB, GPIO_PIN_1 },   /* 3 客厅灯   */
    { GPIOB, GPIO_PIN_2 },   /* 4 卧室灯   */
    { GPIOB, GPIO_PIN_3 },   /* 5 客厅窗帘 */
    { GPIOB, GPIO_PIN_4 },   /* 6 卧室窗帘 */
};

static uint8_t  rxbuf[RX_MAX];
static volatile uint8_t  rxlen = 0;
static volatile uint8_t  frame_done = 0;
static volatile uint32_t last_byte_tick = 0;
static uint8_t  byte;                    /* 单字节接收缓冲 */
static uint8_t  txbuf[RX_MAX + 8];

/* CRC16-Modbus, 与主机同算法 */
static uint16_t crc16(const uint8_t *buf, int len)
{
    uint16_t crc = 0xFFFF;
    for (int i = 0; i < len; i++) {
        crc ^= buf[i];
        for (int j = 0; j < 8; j++) {
            if (crc & 1) crc = (uint16_t)((crc >> 1) ^ 0xA001);
            else         crc >>= 1;
        }
    }
    return crc;
}

static void append_crc(uint8_t *frame, int len)
{
    uint16_t crc = crc16(frame, len);
    frame[len]     = (uint8_t)(crc & 0xFF);
    frame[len + 1] = (uint8_t)(crc >> 8);
}

static void apply_output(uint16_t reg)
{
    if (reg >= REG_NUM) return;
    if (reg_gpio[reg].port == 0) return;
    HAL_GPIO_WritePin(reg_gpio[reg].port, reg_gpio[reg].pin,
                      holding[reg] ? GPIO_PIN_SET : GPIO_PIN_RESET);
}

static void apply_all_outputs(void)
{
    for (int r = 0; r < REG_NUM; r++) apply_output((uint16_t)r);
}

/* 解析一帧并组应答; 返回应答长度(0=不回) */
static int build_response(uint8_t *req, int len)
{
    if (len < 4) return 0;
    if (req[0] != SLAVE_ADDR) return 0;              /* 非本机地址: 丢弃 */

    uint16_t calc = crc16(req, len - 2);
    uint16_t recv = (uint16_t)(req[len - 2] | (req[len - 1] << 8));
    if (calc != recv) return 0;                      /* CRC错: Modbus规定从机不应答 */

    uint16_t reg  = (uint16_t)(req[2] << 8 | req[3]);
    uint8_t  func = req[1];

    if (func == 0x03 && len == 8) {                  /* 读保持寄存器 */
        uint16_t num = (uint16_t)(req[4] << 8 | req[5]);
        if (reg + num > REG_NUM || num == 0) {       /* 非法地址: 回异常码02 */
            txbuf[0] = SLAVE_ADDR; txbuf[1] = 0x83;
            txbuf[2] = 0x02;
            append_crc(txbuf, 3);
            return 5;
        }
        txbuf[0] = SLAVE_ADDR;
        txbuf[1] = 0x03;
        txbuf[2] = (uint8_t)(num * 2);
        for (int i = 0; i < num; i++) {
            txbuf[3 + i * 2] = (uint8_t)(holding[reg + i] >> 8);
            txbuf[4 + i * 2] = (uint8_t)(holding[reg + i] & 0xFF);
        }
        append_crc(txbuf, 3 + num * 2);
        return 3 + num * 2 + 2;
    }

    if (func == 0x06 && len == 8) {                  /* 写单寄存器 */
        uint16_t val = (uint16_t)(req[4] << 8 | req[5]);
        if (reg >= REG_NUM || reg == 1 || reg == 2) { /* 只读寄存器: 异常码02 */
            txbuf[0] = SLAVE_ADDR; txbuf[1] = 0x86;
            txbuf[2] = 0x02;
            append_crc(txbuf, 3);
            return 5;
        }
        holding[reg] = val;
        apply_output(reg);
        memcpy(txbuf, req, (size_t)len);             /* 应答=回显 */
        return len;
    }

    txbuf[0] = SLAVE_ADDR;                           /* 不支持的功能码: 异常码01 */
    txbuf[1] = (uint8_t)(func | 0x80);
    txbuf[2] = 0x01;
    append_crc(txbuf, 3);
    return 5;
}

/* ---------- CubeMX 回调 ---------- */
void HAL_UART_RxCpltCallback(UART_HandleTypeDef *huart)
{
    if (huart->Instance == USART1) {
        if (rxlen < RX_MAX) rxbuf[rxlen++] = byte;
        last_byte_tick = HAL_GetTick();
        HAL_UART_Receive_IT(&huart1, &byte, 1);     /* 重新挂收 */
    }
}

/* ---------- main ---------- */
int main(void)
{
    HAL_Init();
    SystemClock_Config();
    MX_GPIO_Init();
    MX_USART1_UART_Init();

    apply_all_outputs();
    HAL_UART_Receive_IT(&huart1, &byte, 1);

    while (1) {
        /* 静默超过5ms且缓冲非空 -> 一帧收完 */
        __disable_irq();
        uint8_t have = rxlen;
        uint32_t tick = last_byte_tick;
        __enable_irq();

        if (have > 0 && !frame_done &&
            (HAL_GetTick() - tick) > FRAME_GAP_MS) {
            frame_done = 1;
        }

        if (frame_done) {
            __disable_irq();
            uint8_t len = rxlen;
            rxlen = 0;
            __enable_irq();

            int resp = build_response(rxbuf, len);
            if (resp > 0) {
                HAL_UART_Transmit(&huart1, txbuf, (uint16_t)resp, 100);
                HAL_GPIO_TogglePin(GPIOB, GPIO_PIN_5);   /* 帧指示LED */
            }
            frame_done = 0;
        }
    }
}

/* ================= CubeMX 生成函数 (示例给出, 以你工程实际生成为准) ================= */
void SystemClock_Config(void)
{
    RCC_OscInitTypeDef RCC_OscInitStruct = {0};
    RCC_ClkInitTypeDef RCC_ClkInitStruct = {0};

    __HAL_RCC_AFIO_CLK_ENABLE();
    __HAL_RCC_PWR_CLK_ENABLE();

    RCC_OscInitStruct.OscillatorType = RCC_OSCILLATORTYPE_HSE;
    RCC_OscInitStruct.HSEState = RCC_HSE_ON;
    RCC_OscInitStruct.HSEPredivValue = RCC_HSE_PREDIV_DIV1;
    RCC_OscInitStruct.PLL.PLLState = RCC_PLL_ON;
    RCC_OscInitStruct.PLL.PLLSource = RCC_PLLSOURCE_HSE;
    RCC_OscInitStruct.PLL.PLLMUL = RCC_PLL_MUL9;      /* 8MHz x 9 = 72MHz */
    HAL_RCC_OscConfig(&RCC_OscInitStruct);

    RCC_ClkInitStruct.ClockType = RCC_CLOCKTYPE_HCLK | RCC_CLOCKTYPE_SYSCLK
                                | RCC_CLOCKTYPE_PCLK1 | RCC_CLOCKTYPE_PCLK2;
    RCC_ClkInitStruct.SYSCLKSource = RCC_SYSCLKSOURCE_PLLCLK;
    RCC_ClkInitStruct.AHBCLKDivider = RCC_SYSCLK_DIV1;
    RCC_ClkInitStruct.APB1CLKDivider = RCC_HCLK_DIV2;
    RCC_ClkInitStruct.APB2CLKDivider = RCC_HCLK_DIV1;
    HAL_RCC_ClockConfig(&RCC_ClkInitStruct, FLASH_LATENCY_2);
}

static void MX_GPIO_Init(void)
{
    GPIO_InitTypeDef GPIO_InitStruct = {0};
    __HAL_RCC_GPIOA_CLK_ENABLE();
    __HAL_RCC_GPIOB_CLK_ENABLE();

    HAL_GPIO_WritePin(GPIOB, GPIO_PIN_0 | GPIO_PIN_1 | GPIO_PIN_2
                          | GPIO_PIN_3 | GPIO_PIN_4 | GPIO_PIN_5, GPIO_PIN_RESET);

    GPIO_InitStruct.Pin = GPIO_PIN_0 | GPIO_PIN_1 | GPIO_PIN_2
                        | GPIO_PIN_3 | GPIO_PIN_4 | GPIO_PIN_5;
    GPIO_InitStruct.Mode = GPIO_MODE_OUTPUT_PP;
    GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_LOW;
    HAL_GPIO_Init(GPIOB, &GPIO_InitStruct);
}

static void MX_USART1_UART_Init(void)
{
    huart1.Instance = USART1;
    huart1.Init.BaudRate = 9600;
    huart1.Init.WordLength = UART_WORDLENGTH_8B;
    huart1.Init.StopBits = UART_STOPBITS_1;
    huart1.Init.Parity = UART_PARITY_NONE;
    huart1.Init.Mode = UART_MODE_TX_RX;
    huart1.Init.HwFlowCtl = UART_HWCONTROL_NONE;
    huart1.Init.OverSampling = UART_OVERSAMPLING_16;
    HAL_UART_Init(&huart1);

    /* 注意: 若用CubeMX生成, 这些空函数体由CubeMX填好, 直接用生成的即可 */
}

void HAL_MspInit(void)
{
    __HAL_RCC_AFIO_CLK_ENABLE();
    HAL_NVIC_SetPriorityGrouping(NVIC_PRIORITYGROUP_4);
    __HAL_AFIO_REMAP_SWJ_NOJTAG();     /* 释放PB3/PB4给普通IO (窗帘两路!) */
}

void HAL_UART_MspInit(UART_HandleTypeDef *huart)
{
    GPIO_InitTypeDef GPIO_InitStruct = {0};
    if (huart->Instance == USART1) {
        __HAL_RCC_USART1_CLK_ENABLE();
        __HAL_RCC_GPIOA_CLK_ENABLE();
        GPIO_InitStruct.Pin = GPIO_PIN_9;
        GPIO_InitStruct.Mode = GPIO_MODE_AF_PP;
        GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_HIGH;
        HAL_GPIO_Init(GPIOA, &GPIO_InitStruct);
        GPIO_InitStruct.Pin = GPIO_PIN_10;
        GPIO_InitStruct.Mode = GPIO_MODE_INPUT;
        GPIO_InitStruct.Pull = GPIO_PULLUP;
        HAL_GPIO_Init(GPIOA, &GPIO_InitStruct);
        HAL_NVIC_SetPriority(USART1_IRQn, 5, 0);
        HAL_NVIC_EnableIRQ(USART1_IRQn);
    }
}

void USART1_IRQHandler(void)
{
    HAL_UART_IRQHandler(&huart1);
}

void Error_Handler(void)
{
    __disable_irq();
    while (1) { }
}

#ifdef USE_FULL_ASSERT
void assert_failed(uint8_t *file, uint32_t line) { }
#endif
