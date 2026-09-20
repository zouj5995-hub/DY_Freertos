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
#define LORA_POLL_MAX_MS    300U                         // 单次轮询最长占用时间（毫秒）

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
 * 功  能：采样一个字节（调用时已确认检测到起始位且已等待半个位时间）
 * 参  数：无
 * 返回值：采样得到的字节
 * 说  明：采样期间进入临界区，保证 8 个数据位的间隔精确
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
    taskENTER_CRITICAL();

    for (bit = 0U; bit < 8U; bit++)
    {
        if ((BOARD_LORA_RX_PORT->IDR & BOARD_LORA_RX_PIN) != 0U)
        {
            data |= (uint8_t)(1U << bit);
        }

        delay_us(LORA_BIT_TIME_US);
    }

    taskEXIT_CRITICAL();

    return data;
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
     *  #3. RX 与 AUX 做上拉输入
     *==============================*/
    init.Pin   = BOARD_LORA_RX_PIN;
    init.Mode  = GPIO_MODE_INPUT;
    init.Pull  = GPIO_PULLUP;
    init.Speed = GPIO_SPEED_FREQ_LOW;
    HAL_GPIO_Init(BOARD_LORA_RX_PORT, &init);

    init.Pin   = BOARD_LORA_AUX_PIN;
    init.Mode  = GPIO_MODE_INPUT;
    init.Pull  = GPIO_PULLUP;
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
     *  #2. 模块空闲检查（AUX 为低表示模块忙）
     *==============================*/
    if (HAL_GPIO_ReadPin(BOARD_LORA_AUX_PORT, BOARD_LORA_AUX_PIN) == GPIO_PIN_RESET)
    {
        LOG_WARNING("LoRa 模块忙，本次发送已放弃");
        return;
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
 * 功  能：接收轮询：空闲超时判定帧结束；检测起始位并采样一个字节入缓冲
 * 参  数：无
 * 返回值：无
 * 说  明：由通信任务周期调用；检测到起始位时本函数会占用约 1 毫秒
 ******************************************************************************/
void lora_poll(void)
{
    uint8_t data;

    /*==============================
     *  #1. 空闲超时判定：线路安静且缓冲有数据，则一帧接收完毕
     *==============================*/
    if ((s_rx_len > 0U) && ((xTaskGetTickCount() - s_last_rx_tick) >= pdMS_TO_TICKS(LORA_FRAME_IDLE_MS)))
    {
        s_rx_ready = true;                              // 交给上层取走
    }

    /*==============================
     *  #2. 起始位检测：先等半个位，确认不是毛刺
     *==============================*/
    if (lora_rx_line_low() == false)
    {
        return;                                         // 线路空闲
    }

    delay_us(LORA_BIT_TIME_US / 2U);

    if (lora_rx_line_low() == false)
    {
        return;                                         // 半个位后已回到高，判为毛刺
    }

    /*==============================
     *  #3. 采样一个字节并存入缓冲
     *==============================*/
    data = lora_rx_byte();

    if (s_rx_len < LORA_RX_BUF_SIZE)
    {
        s_rx_buf[s_rx_len++] = data;
    }
    else
    {
        s_rx_len = 0U;                                  // 缓冲溢出，丢弃本帧
    }

    s_rx_bytes++;                                       // 累计接收字节数（诊断）
    s_last_rx_tick = xTaskGetTickCount();
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
