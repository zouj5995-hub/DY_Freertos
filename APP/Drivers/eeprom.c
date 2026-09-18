/*******************************************************************************
 * 文件   ：eeprom.c
 * 功能   ：AT24Cxx 系列 EEPROM 读写实现（基于软件 I2C）
 * 说明   ：AT24C256 使用 2 字节地址、页大小 64 字节、写周期约 5ms；
 *          整片写入按页拆分，避免跨页回卷并显著缩短写入耗时
 ******************************************************************************/

/* Includes ------------------------------------------------------------------*/
#include "eeprom.h"
#include "board.h"
#include "soft_i2c.h"
#include "delay_us.h"
#include "FreeRTOS.h"
#include "task.h"

/* Private define ------------------------------------------------------------*/
#define EEPROM_ADDR_BYTES   2U              // AT24C256 使用 2 字节地址
#define EEPROM_TEST_ADDR    0x7F00U         // 自检使用的高地址（远离规则区）
#define EEPROM_TEST_LEN     4U              // 自检写入字节数

/* Private variables ---------------------------------------------------------*/
static const uint8_t s_test_pattern[EEPROM_TEST_LEN] = {0xA5U, 0x5AU, 0x3CU, 0xC3U};   // 自检模式

/* Private functions prototypes ----------------------------------------------*/
static void eeprom_delay_ms(uint32_t ms);                                   // 延时（按调度器状态选择方式）
static bool eeprom_write_page(uint16_t addr, const uint8_t *data, uint16_t len);   // 写入不超过一页的数据

/* Private functions ---------------------------------------------------------*/

/*******************************************************************************
 * 函数名：eeprom_delay_ms
 * 功  能：毫秒级延时，等待 EEPROM 内部写周期
 * 参  数：ms —— 延时毫秒数
 * 返回值：无
 * 说  明：调度器已运行时用 vTaskDelay 让出 CPU，否则退化为 HAL_Delay
 ******************************************************************************/
static void eeprom_delay_ms(uint32_t ms)
{
    /*==============================
     *  #1. 按调度器状态选择延时方式
     *==============================*/
    if (xTaskGetSchedulerState() == taskSCHEDULER_RUNNING)      // 调度器在跑
    {
        vTaskDelay(pdMS_TO_TICKS(ms));                          // 让出 CPU
    }
    else                                                        // 调度器未启动
    {
        delay_us(ms * 1000U);                                   // 用 DWT 忙等，不依赖 HAL 时基
    }
}

/*******************************************************************************
 * 函数名：eeprom_write_page
 * 功  能：向 EEPROM 写入一段不超过一页的数据
 * 参  数：addr —— 起始地址（调用方保证不跨页）
 *         data —— 数据指针
 *         len  —— 数据长度（≤ 页大小）
 * 返回值：true 表示写入成功，false 表示器件无应答
 * 说  明：一次起始 + 地址 + 连续数据 + 停止，这是页写时序
 ******************************************************************************/
static bool eeprom_write_page(uint16_t addr, const uint8_t *data, uint16_t len)
{
    uint16_t i;

    /*==============================
     *  #1. 发送起始条件与器件地址（写）
     *==============================*/
    soft_i2c_start();                                           // 起始
    if (soft_i2c_write_byte((uint8_t)(BOARD_EEPROM_ADDR | 0x00U)) == false)
    {
        soft_i2c_stop();                                        // 无应答：结束总线
        return false;                                           // 器件未响应
    }

    /*==============================
     *  #2. 发送 2 字节存储地址（高字节在前）
     *==============================*/
    if (soft_i2c_write_byte((uint8_t)(addr >> 8)) == false)     // 地址高字节
    {
        soft_i2c_stop();
        return false;
    }

    if (soft_i2c_write_byte((uint8_t)(addr & 0x00FFU)) == false) // 地址低字节
    {
        soft_i2c_stop();
        return false;
    }

    /*==============================
     *  #3. 连续写入本页数据
     *==============================*/
    for (i = 0U; i < len; i++)
    {
        if (soft_i2c_write_byte(data[i]) == false)               // 逐字节写入
        {
            soft_i2c_stop();
            return false;
        }
    }

    /*==============================
     *  #4. 停止条件并等待内部写周期完成
     *==============================*/
    soft_i2c_stop();                                            // 停止：器件开始写
    eeprom_delay_ms(BOARD_EEPROM_WRITE_MS);                     // 等写周期

    return true;                                                // 写入成功
}

/* Exported functions --------------------------------------------------------*/

/*******************************************************************************
 * 函数名：eeprom_init
 * 功  能：初始化 EEPROM 及其软件 I2C 引脚
 * 参  数：无
 * 返回值：无
 * 说  明：必须在首次读写之前调用
 ******************************************************************************/
void eeprom_init(void)
{
    /*==============================
     *  #1. 初始化软件 I2C 引脚
     *==============================*/
    soft_i2c_init();                                            // SDA/SCL 置高，总线空闲
}

/*******************************************************************************
 * 函数名：eeprom_write
 * 功  能：从指定地址开始写入若干字节（自动按页拆分）
 * 参  数：addr —— 起始地址
 *         data —— 数据指针
 *         len  —— 数据长度
 * 返回值：true 表示全部写入成功，false 表示中途失败
 * 说  明：AT24C256 页大小为 64 字节，跨页写会回卷，因此必须拆页
 ******************************************************************************/
bool eeprom_write(uint16_t addr, const uint8_t *data, uint16_t len)
{
    uint16_t page_left;
    uint16_t chunk;

    /*==============================
     *  #1. 参数检查
     *==============================*/
    if ((data == NULL) || (len == 0U))
    {
        return false;                                           // 参数无效
    }

    /*==============================
     *  #2. 循环按页写入
     *==============================*/
    while (len > 0U)
    {
        page_left = (uint16_t)(BOARD_EEPROM_PAGE_SIZE - (addr % BOARD_EEPROM_PAGE_SIZE));   // 本页剩余空间
        chunk     = (len < page_left) ? len : page_left;                                    // 本次写入长度

        if (eeprom_write_page(addr, data, chunk) == false)      // 写一页
        {
            return false;                                       // 写失败
        }

        addr += chunk;                                          // 地址前移
        data += chunk;                                          // 指针前移
        len  -= chunk;                                          // 剩余长度减少
    }

    return true;                                                // 全部写完
}

/*******************************************************************************
 * 函数名：eeprom_read
 * 功  能：从指定地址开始读出若干字节
 * 参  数：addr —— 起始地址
 *         data —— 接收缓冲区
 *         len  —— 读取长度
 * 返回值：true 表示读取成功，false 表示器件无应答
 * 说  明：读操作需要"写地址 + 重复起始 + 读器件地址"三步
 ******************************************************************************/
bool eeprom_read(uint16_t addr, uint8_t *data, uint16_t len)
{
    uint16_t i;

    /*==============================
     *  #1. 参数检查
     *==============================*/
    if ((data == NULL) || (len == 0U))
    {
        return false;                                           // 参数无效
    }

    /*==============================
     *  #2. 写器件地址与存储地址（设定读指针）
     *==============================*/
    soft_i2c_start();                                           // 起始
    if (soft_i2c_write_byte((uint8_t)(BOARD_EEPROM_ADDR | 0x00U)) == false)
    {
        soft_i2c_stop();
        return false;                                           // 器件未响应
    }

    if (soft_i2c_write_byte((uint8_t)(addr >> 8)) == false)     // 地址高字节
    {
        soft_i2c_stop();
        return false;
    }

    if (soft_i2c_write_byte((uint8_t)(addr & 0x00FFU)) == false) // 地址低字节
    {
        soft_i2c_stop();
        return false;
    }

    /*==============================
     *  #3. 重复起始后切换到读模式
     *==============================*/
    soft_i2c_start();                                           // 重复起始
    if (soft_i2c_write_byte((uint8_t)(BOARD_EEPROM_ADDR | 0x01U)) == false)
    {
        soft_i2c_stop();
        return false;                                           // 器件未响应
    }

    /*==============================
     *  #4. 逐字节读取，最后一个字节不给应答
     *==============================*/
    for (i = 0U; i < len; i++)
    {
        data[i] = soft_i2c_read_byte((i + 1U) < len);           // 最后一个字节回 NACK
    }

    soft_i2c_stop();                                            // 停止

    return true;                                                // 读取成功
}

/*******************************************************************************
 * 函数名：eeprom_self_test
 * 功  能：EEPROM 读写自检
 * 参  数：无
 * 返回值：true 表示读写一致（通过），false 表示失败
 * 说  明：在远离规则区的高地址写入已知模式再读回比对
 ******************************************************************************/
bool eeprom_self_test(void)
{
    uint8_t read_back[EEPROM_TEST_LEN];

    /*==============================
     *  #1. 写入测试模式
     *==============================*/
    if (eeprom_write(EEPROM_TEST_ADDR, s_test_pattern, EEPROM_TEST_LEN) == false)
    {
        return false;                                           // 写入失败
    }

    /*==============================
     *  #2. 读回并比对
     *==============================*/
    if (eeprom_read(EEPROM_TEST_ADDR, read_back, EEPROM_TEST_LEN) == false)
    {
        return false;                                           // 读取失败
    }

    for (uint8_t i = 0U; i < EEPROM_TEST_LEN; i++)
    {
        if (read_back[i] != s_test_pattern[i])                  // 逐字节比对
        {
            return false;                                       // 数据不一致
        }
    }

    return true;                                                // 自检通过
}
