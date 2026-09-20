/*******************************************************************************
 * 文件   ：lora_softuart.c
 * 功能   ：LoRa 模块（A39C）软件模拟串口驱动实现
 * 说明   ：TX/RX 均为普通 GPIO，靠微秒级位翻转收发（9600bps）；
 *          发送阻塞完成，接收由 lora_poll() 轮询采样。
 *          位时序靠中断临界区保证：一个字节约 1.04ms，被 tick 打断会导致
 *          接收方采样错位，所以逐字节进入临界区（字节之间允许有空隙）。
 ******************************************************************************/

/* Includes ------------------------------------------------------------------*/
#include "lora_softuart.h"
#include "board.h"
#include "delay_us.h"
#include "log.h"
#include <string.h>
#include "FreeRTOS.h"
#include "task.h"

/* Private define ------------------------------------------------------------*/
#define LORA_BIT_TIME_US    (1000000U / BOARD_LORA_BAUD)   // 一个位的时间（微秒）
#define LORA_POLL_MAX_MS    60U                          // 单次轮询最长占用时间（毫秒）：够“等起始位 + 收完一整帧”

/* Private variables ---------------------------------------------------------*/
static uint8_t    s_rx_buf[LORA_RX_BUF_SIZE];      // 接收缓冲
static uint16_t   s_rx_len     = 0U;               // 当前帧已收字节数
static bool       s_rx_ready   = false;            // 是否有完整帧待取
static TickType_t s_last_rx_tick = 0U;             // 最后一个字节的接收时刻
static uint32_t   s_rx_bytes   = 0U;               // 累计接收字节数（诊断）
static uint32_t   s_tx_bytes   = 0U;               // 累计发送字节数（诊断）

/* Private functions prototypes ----------------------------------------------*/
static bool    lora_rx_line_low(void);             // 读 RX 线是否为低（起始位检测）
static void    lora_tx_bit(bool high);             // 输出一个位并保持一个位时间
static uint8_t lora_rx_byte(void);                 // 采样一个字节（调用前已对齐到数据位）

/* Private functions ---------------------------------------------------------*/

/*******************************************************************************
 * 函数名：lora_rx_line_low
 * 功  能：读取软串口 RX 线当前是否为低电平
 * 参  数：无
 * 返回值：true 为低电平（可能是起始位），false 为高电平（空闲）
 ******************************************************************************/
static bool lora_rx_line_low(void)
{
    return ((BOARD_LORA_RX_PORT->IDR & BOARD_LORA_RX_PIN) == 0U);
}

/*******************************************************************************
 * 函数名：lora_tx_bit
 * 功  能：输出一个位并维持一个位时间
 * 参  数：high —— true 输出高电平，false 输出低电平
 * 返回值：无
 * 说  明：直接写 BSRR/BRR，比 HAL 库快，减少位宽误差
 ******************************************************************************/
static void lora_tx_bit(bool high)
{
    /*==============================
     *  #1. 置位并保持一个位时间
     *==============================*/
    if (high)
    {
        BOARD_LORA_TX_PORT->BSRR = BOARD_LORA_TX_PIN;   // 置高
    }
    else
    {
        BOARD_LORA_TX_PORT->BRR = BOARD_LORA_TX_PIN;    // 拉低
    }

    delay_us(LORA_BIT_TIME_US);
}

/*******************************************************************************
 * 函数名：lora_rx_byte
 * 功  能：采样一个字节（调用时已检测到起始位，且已等待半个位时间）
 * 参  数：无
 * 返回值：采样得到的字节
 * 说  明：本函数在外部中断里调用：中断上下文天然不会被任务打断，故不再
 *          额外关中断；DWT 延时不受关中断影响，位间隔依然精确
 ******************************************************************************/
static uint8_t lora_rx_byte(void)
{
    uint8_t data = 0U;
    uint8_t bit;

    /*==============================
     *  #1. 再等一个位时间，对齐到第 0 个数据位中心
     *==============================*/
    delay_us(LORA_BIT_TIME_US);

    /*==============================
     *  #2. 逐位采样（LSB 在前）
     *==============================*/
    for (bit = 0U; bit < 8U; bit++)
    {
        if ((BOARD_LORA_RX_PORT->IDR & BOARD_LORA_RX_PIN) != 0U)
        {
            data |= (uint8_t)(1U << bit);
        }

        delay_us(LORA_BIT_TIME_US);
    }

    return data;
}

/*******************************************************************************
 * 函数名：lora_rx_isr
 * 功  能：LoRa 接收中断服务：确认起始位后采样一个字节存入接收缓冲
 * 参  数：无
 * 返回值：无
 * 说  明：由 PB5 下降沿中断触发。先等半个位再确认一次以排除毛刺；
 *          整个过程约 1ms，期间不返回（中断上下文）
 ******************************************************************************/
void lora_rx_isr(void)
{
    /*==============================
     *  #1. 先等半个位，排除窄脉冲干扰
     *==============================*/
    delay_us(LORA_BIT_TIME_US / 2U);

    if (lora_rx_line_low() == false)
    {
        return;                                         // 半位后已回高，判为毛刺
    }

    /*==============================
     *  #2. 缓冲满或上一帧尚未取走时，丢弃本次数据
     *==============================*/
    if ((s_rx_len >= LORA_RX_BUF_SIZE) || (s_rx_ready == true))
    {
        return;
    }

    /*==============================
     *  #3. 采样一个字节存入缓冲
     *==============================*/
    s_rx_buf[s_rx_len++] = lora_rx_byte();
    s_rx_bytes++;                                       // 累计接收字节数（诊断）
    s_last_rx_tick = xTaskGetTickCountFromISR();        // 记录时刻，供帧结束判定
}

/*******************************************************************************
 * 函数名：HAL_GPIO_EXTI_Callback
 * 功  能：GPIO 外部中断回调：把中断分派给对应的处理函数
 * 参  数：GPIO_Pin —— 触发中断的引脚
 * 返回值：无
 * 说  明：覆盖 HAL 的弱定义；本工程只用到 LoRa RX 这一个外部中断
 ******************************************************************************/
void HAL_GPIO_EXTI_Callback(uint16_t GPIO_Pin)
{
    if (GPIO_Pin == BOARD_LORA_RX_PIN)
    {
        lora_rx_isr();                                  // LoRa 软串口起始位
    }
}

/* Exported functions --------------------------------------------------------*/

/*******************************************************************************
 * 函数名：lora_init
 * 功  能：初始化 LoRa 软串口引脚，并把模块切到透传（一般）工作模式
 * 参  数：无
 * 返回值：无
 * 说  明：TX 空闲保持高电平；RX/AUX 用上拉输入，避免悬空误触发起始位
 ******************************************************************************/
void lora_init(void)
{
    GPIO_InitTypeDef init = {0};

    /*==============================
     *  #1. 使能 GPIOB 时钟
     *==============================*/
    __HAL_RCC_GPIOB_CLK_ENABLE();

    /*==============================
     *  #2. TX 推挽输出（空闲高）
     *==============================*/
    init.Pin   = BOARD_LORA_TX_PIN;
    init.Mode  = GPIO_MODE_OUTPUT_PP;
    init.Pull  = GPIO_NOPULL;
    init.Speed = GPIO_SPEED_FREQ_HIGH;
    HAL_GPIO_Init(BOARD_LORA_TX_PORT, &init);
    HAL_GPIO_WritePin(BOARD_LORA_TX_PORT, BOARD_LORA_TX_PIN, GPIO_PIN_SET);

    /*==============================
     *  #3. RX 配成下降沿外部中断：软件串口靠中断精确捕获起始位，
     *     纯轮询每 1ms 才看一次，而起始位只有约 104µs，必然频繁踩空
     *==============================*/
    init.Pin   = BOARD_LORA_RX_PIN;
    init.Mode  = GPIO_MODE_IT_FALLING;
    init.Pull  = GPIO_PULLUP;
    init.Speed = GPIO_SPEED_FREQ_HIGH;
    HAL_GPIO_Init(BOARD_LORA_RX_PORT, &init);

    HAL_NVIC_SetPriority(EXTI9_5_IRQn, 5U, 0U);        // PB5 属于 EXTI9_5 中断组
    HAL_NVIC_EnableIRQ(EXTI9_5_IRQn);

    /*==============================
     *  #3.1 AUX 做上拉输入
     *==============================*/
    init.Pin   = BOARD_LORA_AUX_PIN;
    init.Mode  = GPIO_MODE_INPUT;
    init.Pull  = GPIO_PULLUP;
    init.Speed = GPIO_SPEED_FREQ_LOW;
    HAL_GPIO_Init(BOARD_LORA_AUX_PORT, &init);

    /*==============================
     *  #4. MD0/MD1 推挽输出并设置工作模式（一般/透传）
     *==============================*/
    init.Pin   = BOARD_LORA_MD0_PIN;
    init.Mode  = GPIO_MODE_OUTPUT_PP;
    init.Pull  = GPIO_NOPULL;
    init.Speed = GPIO_SPEED_FREQ_LOW;
    HAL_GPIO_Init(BOARD_LORA_MD0_PORT, &init);
    HAL_GPIO_WritePin(BOARD_LORA_MD0_PORT, BOARD_LORA_MD0_PIN, BOARD_LORA_MD0_LEVEL);

    init.Pin = BOARD_LORA_MD1_PIN;
    HAL_GPIO_Init(BOARD_LORA_MD1_PORT, &init);
    HAL_GPIO_WritePin(BOARD_LORA_MD1_PORT, BOARD_LORA_MD1_PIN, BOARD_LORA_MD1_LEVEL);

    LOG_INFO("LoRa 软串口已初始化：TX=PB6 RX=PB5 MD0=1 MD1=0（透传模式），波特率 %u",
             (unsigned)BOARD_LORA_BAUD);
}

/*******************************************************************************
 * 函数名：lora_send
 * 功  能：阻塞发送一段数据（软件位翻转，UART 8N1 格式）
 * 参  数：data —— 待发送数据；len —— 字节数
 * 返回值：无
 * 说  明：逐字节进入临界区，字节之间允许间隙（UART 允许字节间任意空闲）；
 *          若模块正忙（AUX 为低）则放弃本次发送，避免长时间阻塞
 ******************************************************************************/
void lora_send(const uint8_t *data, uint16_t len)
{
    uint16_t i;
    uint8_t  bit;

    /*==============================
     *  #1. 参数检查
     *==============================*/
    if ((data == NULL) || (len == 0U))
    {
        return;
    }

    /*==============================
     *  #2. 模块忙闲检查：AUX 为低表示模块忙，最多等 20ms 再照发
     *==============================*/
    {
        uint32_t wait_us = 0U;

        while ((HAL_GPIO_ReadPin(BOARD_LORA_AUX_PORT, BOARD_LORA_AUX_PIN) == GPIO_PIN_RESET) &&
               (wait_us < 20000U))
        {
            delay_us(100U);                             // 等 100µs 再看一眼
            wait_us += 100U;
        }
    }

    /*==============================
     *  #3. 逐字节位翻转：起始位 + 8 位数据（LSB 在前）+ 停止位
     *==============================*/
    for (i = 0U; i < len; i++)
    {
        taskENTER_CRITICAL();                           // 一个字节内不许被打断

        lora_tx_bit(false);                             // 起始位

        for (bit = 0U; bit < 8U; bit++)
        {
            lora_tx_bit((data[i] & (uint8_t)(1U << bit)) != 0U);
        }

        lora_tx_bit(true);                              // 停止位

        taskEXIT_CRITICAL();
    }

    s_tx_bytes += len;                                  // 累计发送字节数（诊断）
}

/*******************************************************************************
 * 函数名：lora_poll
 * 功  能：接收轮询：判定一帧是否接收完毕（采样实际由 lora_rx_isr 在中断里完成）
 * 参  数：无
 * 返回值：无
 * 说  明：起始位由 PB5 下降沿中断精确捕获，本函数只负责“线路安静足够久
 *          就把这一帧交给上层”，可以低频调用，不再依赖轮询时机是否踩准
 ******************************************************************************/
void lora_poll(void)
{
    taskENTER_CRITICAL();

    /*==============================
     *  #1. 尚无待取帧且线路已安静足够久，则本帧接收完毕
     *==============================*/
    if ((s_rx_len > 0U) && (s_rx_ready == false) &&
        ((xTaskGetTickCount() - s_last_rx_tick) >= pdMS_TO_TICKS(LORA_FRAME_IDLE_MS)))
    {
        s_rx_ready = true;                              // 交给上层取走
    }

    taskEXIT_CRITICAL();
}

/*******************************************************************************
 * 函数名：lora_rx_ready
 * 功  能：查询是否已收到一帧完整数据
 * 参  数：无
 * 返回值：true 表示有完整帧待取
 ******************************************************************************/
bool lora_rx_ready(void)
{
    return s_rx_ready;
}

/*******************************************************************************
 * 函数名：lora_rx_take
 * 功  能：取走已接收的整帧数据
 * 参  数：buf —— 输出缓冲；max_len —— 缓冲大小
 * 返回值：实际取走的字节数（0 表示当前无完整帧）
 * 说  明：取走后清空接收状态，准备接收下一帧
 ******************************************************************************/
uint16_t lora_rx_take(uint8_t *buf, uint16_t max_len)
{
    uint16_t len;

    /*==============================
     *  #1. 参数与状态检查
     *==============================*/
    if ((buf == NULL) || (s_rx_ready == false) || (s_rx_len == 0U))
    {
        return 0U;
    }

    /*==============================
     *  #2. 拷贝数据（按调用方缓冲上限截断）
     *==============================*/
    len = (s_rx_len < max_len) ? s_rx_len : max_len;
    memcpy(buf, s_rx_buf, len);

    /*==============================
     *  #3. 清空接收状态
     *==============================*/
    s_rx_len   = 0U;
    s_rx_ready = false;

    return len;
}

/*******************************************************************************
 * 函数名：lora_get_rx_bytes
 * 功  能：读累计接收字节数（诊断用）
 * 参  数：无
 * 返回值：累计接收字节数
 ******************************************************************************/
uint32_t lora_get_rx_bytes(void)
{
    return s_rx_bytes;
}

/*******************************************************************************
 * 函数名：lora_get_tx_bytes
 * 功  能：读累计发送字节数（诊断用）
 * 参  数：无
 * 返回值：累计发送字节数
 ******************************************************************************/
uint32_t lora_get_tx_bytes(void)
{
    return s_tx_bytes;
}
