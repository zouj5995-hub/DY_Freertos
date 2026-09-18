/*******************************************************************************
 * 文件   ：rule_store.c
 * 功能   ：供电策略（35 条规则）存储层实现
 * 说明   ：负责规则表在"应用层结构"与"EEPROM 9 字节记录"之间的转换；
 *          写入时顺带算好结束时间存进去，保持与旧版本记录格式一致
 ******************************************************************************/

/* Includes ------------------------------------------------------------------*/
#include "rule_store.h"
#include "eeprom.h"
#include <stddef.h>
#include <string.h>

/* Private define ------------------------------------------------------------*/
#define RULE_SEC_PER_DAY    (24UL * 3600UL)     // 一天的秒数

/* Private functions prototypes ----------------------------------------------*/
static bool rule_is_device_code_valid(uint8_t code);                                // 判断目标设备码是否合法
static void rule_record_to_item(const uint8_t *rec, rule_item_t *item);             // EEPROM 记录转应用结构
static void rule_item_to_record(const rule_item_t *item, uint8_t *rec);             // 应用结构转 EEPROM 记录

/* Private functions ---------------------------------------------------------*/

/*******************************************************************************
 * 函数名：rule_is_device_code_valid
 * 功  能：判断目标设备码是否合法
 * 参  数：code —— 目标设备码
 * 返回值：true 表示合法，false 表示非法或无效
 * 说  明：用于识别 EEPROM 数据损坏，把非法码统一当成无效规则
 ******************************************************************************/
static bool rule_is_device_code_valid(uint8_t code)
{
    /*==============================
     *  #1. 逐类判断合法码
     *==============================*/
    if (code <= RULE_DEVICE_BK3)                                    // 0x00 - 0x07：单设备
    {
        return true;
    }

    if ((code >= RULE_DEVICE_PC_RADAR_SONAR_CAMERA) &&              // 0x0A - 0x0D：组合设备
        (code <= RULE_DEVICE_PC_SONAR))
    {
        return true;
    }

    return false;                                                   // 其它码视为非法
}

/*******************************************************************************
 * 函数名：rule_record_to_item
 * 功  能：把 EEPROM 中的 9 字节记录转换成应用层规则结构
 * 参  数：rec  —— 9 字节记录
 *         item —— 输出规则
 * 返回值：无
 * 说  明：记录格式（与旧版本一致）：
 *         0:时 1:分 2:秒 3-4:工作时长(小端) 5:结束小时 6:结束分 7:结束秒 8:目标设备
 ******************************************************************************/
static void rule_record_to_item(const uint8_t *rec, rule_item_t *item)
{
    /*==============================
     *  #1. 逐字段搬运（结束时间不入应用结构）
     *==============================*/
    item->start_h       = rec[0];                                   // 开始时
    item->start_m       = rec[1];                                   // 开始分
    item->start_s       = rec[2];                                   // 开始秒
    item->work_sec      = (uint16_t)rec[3] | ((uint16_t)rec[4] << 8);   // 工作时长（小端）
    item->target_device = rec[8];                                   // 目标设备（第 9 字节）

    /*==============================
     *  #2. 非法设备码统一按无效处理
     *==============================*/
    if (rule_is_device_code_valid(item->target_device) == false)
    {
        item->target_device = RULE_DEVICE_INVALID;                  // 视为无效规则
    }
}

/*******************************************************************************
 * 函数名：rule_item_to_record
 * 功  能：把应用层规则结构转换成 EEPROM 的 9 字节记录
 * 参  数：item —— 输入规则
 *         rec  —— 输出的 9 字节记录
 * 返回值：无
 * 说  明：顺带计算结束时间（含进位与跨天取模），保持旧版本记录格式
 ******************************************************************************/
static void rule_item_to_record(const rule_item_t *item, uint8_t *rec)
{
    uint32_t total_sec;
    uint32_t total_min;
    uint8_t  carry_min;
    uint8_t  carry_hour;

    /*==============================
     *  #1. 先计算结束时间（秒 -> 分 -> 时逐级进位）
     *==============================*/
    total_sec  = (uint32_t)item->start_s + (uint32_t)item->work_sec;    // 秒累加
    carry_min  = (uint8_t)(total_sec / 60UL);                           // 秒进到分
    total_min  = (uint32_t)item->start_m + (uint32_t)carry_min;         // 分累加
    carry_hour = (uint8_t)(total_min / 60UL);                           // 分进到时

    /*==============================
     *  #2. 按记录格式逐字段填充
     *==============================*/
    rec[0] = item->start_h;                                             // 开始时
    rec[1] = item->start_m;                                             // 开始分
    rec[2] = item->start_s;                                             // 开始秒
    rec[3] = (uint8_t)(item->work_sec & 0x00FFU);                       // 工作时长低字节
    rec[4] = (uint8_t)((item->work_sec >> 8) & 0x00FFU);                // 工作时长高字节
    rec[5] = (uint8_t)((item->start_h + carry_hour) % 24U);             // 结束小时（跨天取模）
    rec[6] = (uint8_t)(total_min % 60UL);                               // 结束分
    rec[7] = (uint8_t)(total_sec % 60UL);                               // 结束秒
    rec[8] = item->target_device;                                       // 目标设备
}

/* Exported functions --------------------------------------------------------*/

/*******************************************************************************
 * 函数名：rule_store_init
 * 功  能：初始化规则存储层
 * 参  数：无
 * 返回值：无
 * 说  明：内部会初始化 EEPROM 与软件 I2C
 ******************************************************************************/
void rule_store_init(void)
{
    /*==============================
     *  #1. 初始化 EEPROM 及其总线
     *==============================*/
    eeprom_init();                                                      // 初始化软件 I2C
}

/*******************************************************************************
 * 函数名：rule_store_load
 * 功  能：从 EEPROM 载入整张规则表
 * 参  数：rules —— 输出规则数组
 *         count —— 需要载入的条数
 * 返回值：true 表示全部载入成功，false 表示中途读取失败
 * 说  明：读取过程中会过滤非法设备码，防止损坏数据被当成有效规则
 ******************************************************************************/
bool rule_store_load(rule_item_t *rules, uint8_t count)
{
    uint8_t rec[RULE_RECORD_SIZE];

    /*==============================
     *  #1. 参数检查
     *==============================*/
    if ((rules == NULL) || (count == 0U))
    {
        return false;                                                   // 参数无效
    }

    /*==============================
     *  #2. 逐条读取并转换
     *==============================*/
    for (uint8_t i = 0U; i < count; i++)
    {
        if (eeprom_read((uint16_t)(RULE_EEPROM_ADDR + (uint16_t)i * RULE_RECORD_SIZE),
                        rec, RULE_RECORD_SIZE) == false)                // 读一条记录
        {
            return false;                                               // 读取失败
        }

        rule_record_to_item(rec, &rules[i]);                            // 转换并过滤非法码
    }

    return true;                                                        // 全部载入成功
}

/*******************************************************************************
 * 函数名：rule_store_save
 * 功  能：把整张规则表写入 EEPROM
 * 参  数：rules —— 规则数组
 *         count —— 需要保存的条数
 * 返回值：true 表示全部写入成功，false 表示中途失败
 * 说  明：按记录格式转换后连续写入；写入耗时约 (条数 × 9 / 页大小) × 写周期
 ******************************************************************************/
bool rule_store_save(const rule_item_t *rules, uint8_t count)
{
    uint8_t rec[RULE_RECORD_SIZE];

    /*==============================
     *  #1. 参数检查
     *==============================*/
    if ((rules == NULL) || (count == 0U))
    {
        return false;                                                   // 参数无效
    }

    /*==============================
     *  #2. 逐条转换后写入
     *==============================*/
    for (uint8_t i = 0U; i < count; i++)
    {
        rule_item_to_record(&rules[i], rec);                            // 转换成记录

        if (eeprom_write((uint16_t)(RULE_EEPROM_ADDR + (uint16_t)i * RULE_RECORD_SIZE),
                         rec, RULE_RECORD_SIZE) == false)               // 写一条记录
        {
            return false;                                               // 写入失败
        }
    }

    return true;                                                        // 全部写入成功
}

/*******************************************************************************
 * 函数名：rule_store_set_default
 * 功  能：生成安全默认规则
 * 参  数：rules —— 输出规则数组
 *         count —— 规则条数
 * 返回值：无
 * 说  明：安全默认 = 全部规则无效，即"不按策略自动供电"，
 *         避免 EEPROM 空白或损坏时误开设备
 ******************************************************************************/
void rule_store_set_default(rule_item_t *rules, uint8_t count)
{
    /*==============================
     *  #1. 参数检查
     *==============================*/
    if ((rules == NULL) || (count == 0U))
    {
        return;                                                         // 参数无效
    }

    /*==============================
     *  #2. 全部置为无效规则
     *==============================*/
    memset(rules, 0, sizeof(rule_item_t) * count);                      // 先清零

    for (uint8_t i = 0U; i < count; i++)
    {
        rules[i].target_device = RULE_DEVICE_INVALID;                   // 标记为无效
    }
}
