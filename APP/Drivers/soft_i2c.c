/*******************************************************************************
 * 文件   ：soft_i2c.c
 * 功能   ：软件模拟 I2C 实现
 * 说明   ：引脚取自 board.h；位延时用 delay_us（DWT）；
 *          SDA 需要在输入与输出之间切换，因此每次收发都动态改引脚模式
 ******************************************************************************/

/* Includes ------------------------------------------------------------------*/
#include "soft_i2c.h"
#include "board.h"
#include "delay_us.h"

/* Private define ------------------------------------------------------------*/
#define SOFT_I2C_DELAY_US   2U          // 每位延时（约 250kHz 以内，慢一点更稳）

/* Private functions prototypes ----------------------------------------------*/
static void    soft_i2c_scl_write(uint8_t level);       // 写 SCL 电平
static void    soft_i2c_sda_write(uint8_t level);       // 写 SDA 电平
static uint8_t soft_i2c_sda_read(void);                 // 读 SDA 电平
static void    soft_i2c_sda_dir_out(void);              // SDA 切换为输出
static void    soft_i2c_sda_dir_in(void);               // SDA 切换为输入

/* Private functions ---------------------------------------------------------*/

/*******************************************************************************
 * 函数名：soft_i2c_scl_write
 * 功  能：写 SCL 电平并保持一个位延时
 * 参  数：level —— GPIO_PIN_SET 或 GPIO_PIN_RESET
 * 返回值：无
 ******************************************************************************/
static void soft_i2c_scl_write(uint8_t level)
{
    /*==============================
     *  #1. 输出电平并延时半个位周期
     *==============================*/
    HAL_GPIO_WritePin(BOARD_EEPROM_SCL_PORT, BOARD_EEPROM_SCL_PIN, (GPIO_PinState)level);  // 写 SCL
    delay_us(SOFT_I2C_DELAY_US);                                                            // 位延时
}

/*******************************************************************************
 * 函数名：soft_i2c_sda_write
 * 功  能：写 SDA 电平并保持一个位延时
 * 参  数：level —— GPIO_PIN_SET 或 GPIO_PIN_RESET
 * 返回值：无
 ******************************************************************************/
static void soft_i2c_sda_write(uint8_t level)
{
    /*==============================
     *  #1. 输出电平并延时半个位周期
     *==============================*/
    HAL_GPIO_WritePin(BOARD_EEPROM_SDA_PORT, BOARD_EEPROM_SDA_PIN, (GPIO_PinState)level);  // 写 SDA
    delay_us(SOFT_I2C_DELAY_US);                                                            // 位延时
}

/*******************************************************************************
 * 函数名：soft_i2c_sda_read
 * 功  能：读取 SDA 电平
 * 参  数：无
 * 返回值：GPIO_PIN_SET 或 GPIO_PIN_RESET
 ******************************************************************************/
static uint8_t soft_i2c_sda_read(void)
{
    uint8_t level;

    /*==============================
     *  #1. 读引脚电平并延时
     *==============================*/
    level = (uint8_t)HAL_GPIO_ReadPin(BOARD_EEPROM_SDA_PORT, BOARD_EEPROM_SDA_PIN);         // 读 SDA
    delay_us(SOFT_I2C_DELAY_US);                                                            // 位延时

    return level;                                                                           // 返回电平
}

/*******************************************************************************
 * 函数名：soft_i2c_sda_dir_out
 * 功  能：把 SDA 切换为推挽输出
 * 参  数：无
 * 返回值：无
 * 说  明：从器件读数据前必须切成输入，读完后要切回输出
 ******************************************************************************/
static void soft_i2c_sda_dir_out(void)
{
    GPIO_InitTypeDef init = {0};

    /*==============================
     *  #1. 重新配置 SDA 为推挽输出
     *==============================*/
    init.Pin   = BOARD_EEPROM_SDA_PIN;                      // SDA 引脚
    init.Mode  = GPIO_MODE_OUTPUT_PP;                       // 推挽输出
    init.Pull  = GPIO_NOPULL;                               // 不需要上下拉
    init.Speed = GPIO_SPEED_FREQ_HIGH;                      // 高速
    HAL_GPIO_Init(BOARD_EEPROM_SDA_PORT, &init);            // 应用配置
}

/*******************************************************************************
 * 函数名：soft_i2c_sda_dir_in
 * 功  能：把 SDA 切换为输入
 * 参  数：无
 * 返回值：无
 * 说  明：I2C 是开漏总线，读应答与读数据前必须先释放（切成输入）
 ******************************************************************************/
static void soft_i2c_sda_dir_in(void)
{
    GPIO_InitTypeDef init = {0};

    /*==============================
     *  #1. 重新配置 SDA 为输入
     *==============================*/
    init.Pin  = BOARD_EEPROM_SDA_PIN;                       // SDA 引脚
    init.Mode = GPIO_MODE_INPUT;                            // 输入模式
    init.Pull = GPIO_NOPULL;                                // 外部已有上拉
    HAL_GPIO_Init(BOARD_EEPROM_SDA_PORT, &init);            // 应用配置
}

/* Exported functions --------------------------------------------------------*/

/*******************************************************************************
 * 函数名：soft_i2c_init
 * 功  能：初始化软件 I2C 引脚
 * 参  数：无
 * 返回值：无
 * 说  明：SCL 始终为输出；SDA 默认输出，读到数据时会临时切换
 ******************************************************************************/
void soft_i2c_init(void)
{
    GPIO_InitTypeDef init = {0};

    /*==============================
     *  #1. 打开对应端口时钟
     *==============================*/
    if (BOARD_EEPROM_SCL_PORT == GPIOG)                     // 当前板子 SCL 在 GPIOG
    {
        __HAL_RCC_GPIOG_CLK_ENABLE();                       // 使能 GPIOG 时钟
    }
    else if (BOARD_EEPROM_SCL_PORT == GPIOB)                // 兼容其它端口
    {
        __HAL_RCC_GPIOB_CLK_ENABLE();                       // 使能 GPIOB 时钟
    }

    /*==============================
     *  #2. 配置 SCL 与 SDA 为推挽输出
     *==============================*/
    init.Pin   = BOARD_EEPROM_SCL_PIN | BOARD_EEPROM_SDA_PIN;   // 两个引脚
    init.Mode  = GPIO_MODE_OUTPUT_PP;                           // 推挽输出
    init.Pull  = GPIO_NOPULL;                                   // 不需要内部上下拉
    init.Speed = GPIO_SPEED_FREQ_HIGH;                          // 高速
    HAL_GPIO_Init(BOARD_EEPROM_SCL_PORT, &init);                // 应用配置

    /*==============================
     *  #3. 总线空闲状态：两根线都为高
     *==============================*/
    soft_i2c_sda_write(GPIO_PIN_SET);                           // SDA 拉高
    soft_i2c_scl_write(GPIO_PIN_SET);                           // SCL 拉高
}

/*******************************************************************************
 * 函数名：soft_i2c_start
 * 功  能：产生 I2C 起始条件
 * 参  数：无
 * 返回值：无
 * 说  明：SCL 为高时 SDA 由高变低即为起始
 ******************************************************************************/
void soft_i2c_start(void)
{
    /*==============================
     *  #1. 输出起始时序
     *==============================*/
    soft_i2c_sda_dir_out();                                     // 确保 SDA 为输出
    soft_i2c_sda_write(GPIO_PIN_SET);                           // SDA 拉高
    soft_i2c_scl_write(GPIO_PIN_SET);                           // SCL 拉高
    soft_i2c_sda_write(GPIO_PIN_RESET);                         // SCL 高时 SDA 拉低：起始
    soft_i2c_scl_write(GPIO_PIN_RESET);                         // SCL 拉低，准备发送
}

/*******************************************************************************
 * 函数名：soft_i2c_stop
 * 功  能：产生 I2C 停止条件
 * 参  数：无
 * 返回值：无
 * 说  明：SCL 为高时 SDA 由低变高即为停止
 ******************************************************************************/
void soft_i2c_stop(void)
{
    /*==============================
     *  #1. 输出停止时序
     *==============================*/
    soft_i2c_sda_dir_out();                                     // 确保 SDA 为输出
    soft_i2c_sda_write(GPIO_PIN_RESET);                         // SDA 拉低
    soft_i2c_scl_write(GPIO_PIN_SET);                           // SCL 拉高
    soft_i2c_sda_write(GPIO_PIN_SET);                           // SCL 高时 SDA 拉高：停止
}

/*******************************************************************************
 * 函数名：soft_i2c_write_byte
 * 功  能：向总线写一个字节并读取应答
 * 参  数：byte —— 待发送的字节
 * 返回值：true 表示收到应答（ACK），false 表示无应答（NACK）
 ******************************************************************************/
bool soft_i2c_write_byte(uint8_t byte)
{
    uint8_t i;
    uint8_t ack;

    /*==============================
     *  #1. 高位在前逐位输出
     *==============================*/
    soft_i2c_sda_dir_out();                                     // SDA 为输出

    for (i = 0U; i < 8U; i++)                                   // 逐位发送 8 位
    {
        if ((byte & (uint8_t)(0x80U >> i)) != 0U)               // 判断当前位
        {
            soft_i2c_sda_write(GPIO_PIN_SET);                   // 该位为 1
        }
        else
        {
            soft_i2c_sda_write(GPIO_PIN_RESET);                 // 该位为 0
        }

        soft_i2c_scl_write(GPIO_PIN_SET);                       // 拉高时钟：从机采样
        soft_i2c_scl_write(GPIO_PIN_RESET);                     // 拉低时钟：准备下一位
    }

    /*==============================
     *  #2. 释放总线并读取应答位
     *==============================*/
    soft_i2c_sda_dir_in();                                      // SDA 切输入以接收应答
    soft_i2c_sda_write(GPIO_PIN_SET);                           // 释放 SDA（外部上拉为高）
    soft_i2c_scl_write(GPIO_PIN_SET);                           // 拉高时钟：从机输出应答

    ack = (soft_i2c_sda_read() == (uint8_t)GPIO_PIN_RESET) ? 1U : 0U;   // 低电平为应答

    soft_i2c_scl_write(GPIO_PIN_RESET);                         // 拉低时钟结束应答
    soft_i2c_sda_dir_out();                                     // SDA 恢复输出

    return (ack != 0U);                                         // 返回应答结果
}

/*******************************************************************************
 * 函数名：soft_i2c_read_byte
 * 功  能：从总线读一个字节，并可选发送应答
 * 参  数：ack —— true 表示读完发送应答（继续读），false 表示不答（结束读）
 * 返回值：读到的字节
 ******************************************************************************/
uint8_t soft_i2c_read_byte(bool ack)
{
    uint8_t i;
    uint8_t byte = 0U;

    /*==============================
     *  #1. 释放 SDA 并逐位接收
     *==============================*/
    soft_i2c_sda_dir_in();                                      // SDA 切输入
    soft_i2c_sda_write(GPIO_PIN_SET);                           // 释放 SDA

    for (i = 0U; i < 8U; i++)                                   // 接收 8 位
    {
        soft_i2c_scl_write(GPIO_PIN_SET);                       // 拉高时钟：从机输出数据

        if (soft_i2c_sda_read() == (uint8_t)GPIO_PIN_SET)       // 采样当前位
        {
            byte |= (uint8_t)(0x80U >> i);                      // 该位为 1
        }

        soft_i2c_scl_write(GPIO_PIN_RESET);                     // 拉低时钟：准备下一位
    }

    /*==============================
     *  #2. 发送应答或不应答
     *==============================*/
    soft_i2c_sda_dir_out();                                     // SDA 切输出
    soft_i2c_sda_write(ack ? GPIO_PIN_RESET : GPIO_PIN_SET);    // 低电平表示应答
    soft_i2c_scl_write(GPIO_PIN_SET);                           // 拉高时钟
    soft_i2c_scl_write(GPIO_PIN_RESET);                         // 拉低时钟结束

    return byte;                                                // 返回接收到的字节
}
